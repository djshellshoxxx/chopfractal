// A/B morph preview and commit (docs/specs/roadmap/F10-ab-morph.md). Message thread only.
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/pattern_engine/morph.hpp>

#include <cmath>

namespace chopfractal::composition {

Status ProjectSession::storeSnapshot(std::size_t slot) {
  Status s = patterns_.storeSnapshot(slot);
  if (s.ok() && morphActive_) {
    Status r = refreshMorph();
    if (!r.ok()) notice(Notice::Level::Warning, r.error().message);  // the previous preview keeps playing
  }
  return s;
}

const pattern::Pattern* ProjectSession::displayPattern() const {
  if (morphActive_ && morphPreview_) return morphPreview_.get();
  if (sceneOverride_) return sceneOverride_.get();
  return pattern();
}

void ProjectSession::endMorph(bool announce) {
  if (!morphActive_) return;
  morphActive_ = false;
  morphPreview_.reset();
  if (announce) notice(Notice::Level::Info, "Morph ended because the pattern was changed.");
}

Status ProjectSession::refreshMorph() {
  auto a = patterns_.snapshot(0), b = patterns_.snapshot(1);
  if (!a || !b) return makeError(ErrorCode::InvalidArgument, "Store A and B first.");
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  auto m = pattern::morph(pattern::sanitize(*a, *chops_), pattern::sanitize(*b, *chops_), morphT_, *chops_, false);
  if (!m.ok()) return m.error();
  Status v = installPlayback(m.value());
  if (!v.ok()) return v;
  morphPreview_ = std::make_shared<const pattern::Pattern>(std::move(m.value()));
  return {};
}

Status ProjectSession::beginMorph(double t) {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  if (sequencer_.running() || startPending_ != 0) return makeError(ErrorCode::Conflict, "Stop the scenes before morphing.");
  if (!patterns_.hasSnapshot(0) || !patterns_.hasSnapshot(1)) return makeError(ErrorCode::InvalidArgument, "Store A and B first.");
  if (evolve_.running()) return makeError(ErrorCode::Conflict, "Switch Evolve off before morphing.");
  if (!(t >= 0.0 && t <= 1.0)) return makeError(ErrorCode::InvalidArgument, "the morph amount must be between 0 and 1");
  const double previousT = morphT_;
  morphT_ = t;
  Status s = refreshMorph();
  if (!s.ok()) {
    morphT_ = previousT;
    return s;
  }
  morphActive_ = true;
  return {};
}

Status ProjectSession::setMorph(double t) {
  if (!morphActive_) return makeError(ErrorCode::InvalidArgument, "start a morph first");
  if (!(t >= 0.0 && t <= 1.0)) return makeError(ErrorCode::InvalidArgument, "the morph amount must be between 0 and 1");
  const double previousT = morphT_;
  morphT_ = t;
  Status s = refreshMorph();
  if (!s.ok()) morphT_ = previousT;
  return s;
}

Status ProjectSession::commitMorph(const std::string& label) {
  if (!morphActive_) return makeError(ErrorCode::InvalidArgument, "there is no morph to commit");
  auto a = patterns_.snapshot(0), b = patterns_.snapshot(1);
  if (!a || !b || !chops_) return makeError(ErrorCode::InvalidArgument, "Store A and B first.");
  auto q = pattern::morph(pattern::sanitize(*a, *chops_), pattern::sanitize(*b, *chops_), morphT_, *chops_, true);
  if (!q.ok()) return q.error();
  const std::string name = label.empty() ? "Morph A>B " + std::to_string(std::lround(morphT_ * 100.0)) + "%" : label;
  committingMorph_ = true;
  Status s = finalize(std::move(q), name.c_str());
  committingMorph_ = false;
  if (!s.ok()) return s;
  notice(Notice::Level::Info, "Committed the morph as a new variation.");
  return {};
}

void ProjectSession::cancelMorph() {
  if (!morphActive_) return;
  endMorph(false);
  republish();
  notice(Notice::Level::Info, "Morph cancelled; your pattern is unchanged.");
}

}  // namespace chopfractal::composition
