// F10 A/B morph at the session level: a preview playback built from the two stored slots, committed on request.
#include <chopfractal/composition/project_session.hpp>

#include <cmath>

namespace chopfractal::composition {
namespace {
constexpr char kEndedText[] = "Morph ended because the pattern was changed.";
constexpr char kNoFitText[] = "That snapshot no longer fits the current chops.";
}  // namespace

Status ProjectSession::storeSnapshot(std::size_t slot) {
  Status s = patterns_.storeSnapshot(slot);
  if (s.ok() && morphActive_) {
    Status r = refreshMorph();  // the preview follows the new slot content at once
    if (!r.ok()) {
      endMorph(false);
      notice(Notice::Level::Warning, r.error().message);
      Status back = republish();
      if (!back.ok()) return back;
    }
  }
  return s;
}

bool ProjectSession::endMorph(bool withNotice) {
  if (!morphActive_) return false;
  morphActive_ = false;
  morphPreview_.reset();
  if (withNotice) notice(Notice::Level::Info, kEndedText);
  return true;
}

Result<pattern::Pattern> ProjectSession::morphPattern(double t, bool freeze) const {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  const auto a = patterns_.slot(0);
  const auto b = patterns_.slot(1);
  if (!a || !b) return makeError(ErrorCode::NotFound, "Store A and B first.");
  // A slot stored under other markers can hold hits whose chops are gone: repair like a variation activation does.
  for (const auto& slot : {a, b}) {
    const pattern::Pattern fixed = pattern::sanitize(*slot, *chops_);
    if (!pattern::flatten(fixed, *chops_).ok()) return makeError(ErrorCode::NotFound, kNoFitText);
  }
  auto m = pattern::morph(*a, *b, t, *chops_, freeze);
  if (!m.ok()) return m.error();
  pattern::Pattern out = std::move(m.value());
  // Gesture effects follow every new pattern. A commit goes through finalize, which applies them itself.
  if (!freeze && fx_.any()) return pattern::applyFxPass(out, fx_);
  return out;
}

Status ProjectSession::refreshMorph() {
  auto m = morphPattern(morphT_, false);
  if (!m.ok()) return m.error();
  Status st = installPlayback(m.value());  // validate + publish; nothing else changes
  if (!st.ok()) return st;
  morphPreview_ = std::make_shared<const pattern::Pattern>(std::move(m.value()));
  return {};
}

Status ProjectSession::beginMorph(double t) {
  if (!std::isfinite(t) || t < 0.0 || t > 1.0) return makeError(ErrorCode::InvalidArgument, "the morph amount must be between 0 and 1");
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  if (!patterns_.hasSnapshot(0) || !patterns_.hasSnapshot(1)) return makeError(ErrorCode::NotFound, "Store A and B first.");
  if (evolveRunning()) return makeError(ErrorCode::Blocked, "Switch Evolve off before morphing.");
  if (scenePhase_ != ScenePhase::Off) return makeError(ErrorCode::Blocked, "Stop scene playback before morphing.");
  const bool wasActive = morphActive_;
  const double previousT = morphT_;
  morphActive_ = true;
  morphT_ = t;
  Status st = refreshMorph();
  if (!st.ok()) {
    morphActive_ = wasActive;
    morphT_ = previousT;
    return st;
  }
  return {};
}

Status ProjectSession::setMorph(double t) {
  if (!std::isfinite(t) || t < 0.0 || t > 1.0) return makeError(ErrorCode::InvalidArgument, "the morph amount must be between 0 and 1");
  if (!morphActive_) return makeError(ErrorCode::InvalidArgument, "start the morph first");
  const double previousT = morphT_;
  morphT_ = t;
  Status st = refreshMorph();
  if (!st.ok()) morphT_ = previousT;  // keep the previous preview; the caller shows the error
  return st;
}

const pattern::Pattern* ProjectSession::displayPattern() const {
  if (sceneOverride_) return sceneOverride_.get();
  if (morphActive_ && morphPreview_) return morphPreview_.get();
  return pattern();
}

Status ProjectSession::commitMorph(const std::string& label) {
  if (!morphActive_) return makeError(ErrorCode::InvalidArgument, "there is no morph to commit");
  auto frozen = morphPattern(morphT_, true);
  if (!frozen.ok()) return frozen.error();
  const std::string name = label.empty() ? "Morph A>B " + std::to_string(std::lround(morphT_ * 100.0)) + "%" : label;
  const double t = morphT_;
  const auto preview = morphPreview_;
  morphActive_ = false;  // finalize would otherwise report the pattern change as an interruption
  morphPreview_.reset();
  Status st = finalize(std::move(frozen.value()), name.c_str());
  if (!st.ok()) {
    morphActive_ = true;
    morphT_ = t;
    morphPreview_ = preview;
    return st;
  }
  notice(Notice::Level::Info, "Committed the morph as a new variation.");
  return {};
}

void ProjectSession::cancelMorph() {
  if (!endMorph(false)) return;
  republish();
}

}  // namespace chopfractal::composition
