// Groove Lab glue (F17): project state section, undo, and the playback build that applies the groove and the
// engine v2 renderer hints. The groove itself lives in the portable `groove` module.
#include <chopfractal/composition/project_session.hpp>

namespace chopfractal::composition {

namespace {
constexpr std::uint16_t kGrooveSchema = 1;

std::uint8_t chokeGroupOf(DrumRole r) {
  switch (r) {
    case DrumRole::Kick: return 1;
    case DrumRole::Snare:
    case DrumRole::Ghost: return 2;
    case DrumRole::Hat: return 3;
    case DrumRole::OpenHat: return 4;
    default: return 0;
  }
}

// The state section is absent while the groove equals the default object, so feature-free projects are unchanged.
Status storeGrooveSection(ProjectSession& session, const groove::Groove& g) {
  StateSection sec;
  sec.tag = kTagGroove;
  sec.schema = kGrooveSchema;
  if (!(g == groove::Groove{})) sec.bytes = groove::serialize(g);
  return session.setSection(std::move(sec));
}
}  // namespace

Status ProjectSession::decodeGroove(const StateSection& s) {
  if (s.schema != kGrooveSchema) return makeError(ErrorCode::Corrupt, "unknown groove section schema");
  const groove::ParseResult r = groove::parse(s.bytes.data(), s.bytes.size());
  if (!r.ok) return makeError(ErrorCode::Corrupt, "the groove section is not valid");
  groove_ = r.groove;
  grooveHistory_.reset(groove_);
  return republish();
}

Status ProjectSession::setGroove(const groove::Groove& in) {
  const groove::Groove g = groove::sanitized(in);
  if (g == groove_) return {};
  const groove::Groove previous = groove_;
  groove_ = g;
  Status st = republish();  // validated before it is kept
  if (!st.ok()) {
    groove_ = previous;
    return st;
  }
  grooveHistory_.commit(groove_);
  return storeGrooveSection(*this, groove_);
}

bool ProjectSession::undoGroove() {
  if (!grooveHistory_.undo()) return false;
  const groove::Groove previous = groove_;
  groove_ = grooveHistory_.current();
  if (!republish().ok()) {
    groove_ = previous;
    grooveHistory_.redo();
    return false;
  }
  storeGrooveSection(*this, groove_);
  return true;
}

bool ProjectSession::redoGroove() {
  if (!grooveHistory_.redo()) return false;
  const groove::Groove previous = groove_;
  groove_ = grooveHistory_.current();
  if (!republish().ok()) {
    groove_ = previous;
    grooveHistory_.undo();
    return false;
  }
  storeGrooveSection(*this, groove_);
  return true;
}

// Flatten, add the engine v2 renderer hints (choke groups, smooth fades), apply the groove, and prepare the playback.
Result<std::shared_ptr<const render::Playback>> ProjectSession::buildPlayback(const pattern::Pattern& p, bool withGroove) const {
  auto flat = pattern::flatten(p, *chops_);
  if (!flat.ok()) return flat.error();
  FlatEventList events = std::move(flat.value());
  const bool v2 = p.engineVersion >= pattern::kEngineVersionBreak;
  const bool grooveOn = withGroove && !groove::isNeutral(groove_);
  ChopSnapshotPtr roleView;
  if (v2 || grooveOn) roleView = annotatedChops();
  auto roleOf = [&](ChopId id) {
    const ChopInfo* c = roleView ? roleView->find(id) : nullptr;
    return c ? c->role : DrumRole::Unknown;
  };
  if (v2)
    for (FlatEvent& e : events) {
      e.chokeGroup = chokeGroupOf(roleOf(e.chop));
      e.smoothFades = true;
    }
  if (grooveOn) {
    const Ticks barT = pattern::barTicks(p.settings);
    const Ticks stepT = gridTicks(p.settings.grid);
    const int steps = stepT > 0 ? static_cast<int>(barT / stepT) : 16;
    groove::apply(groove_, events, pattern::lengthTicks(p), static_cast<int>(barT), steps, [&](ChopId id) { return groove::classOf(roleOf(id)); });
  }
  return render::makePlayback(sourceData_, events, pattern::lengthTicks(p));
}

Result<FlatEventList> ProjectSession::midiEvents() const {
  if (!playback_ || playback_->passThrough) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  if (groove::isNeutral(groove_) || groove_.bakeIntoMidi || !patterns_.hasPattern() || !sourceData_ || !chops_) return playback_->events;
  auto raw = buildPlayback(patterns_.current(), false);  // the same expansion as playback, minus the groove
  if (!raw.ok()) return raw.error();
  return raw.value()->events;
}

}  // namespace chopfractal::composition
