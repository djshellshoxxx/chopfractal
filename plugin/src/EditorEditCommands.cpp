// Direct-editing commands (G2 / F02): hit gestures, inspector and effects-rack edits, marker edits and snapping, the
// detection preview. Each pattern command is one ProjectSession::edit (one undo step) through EditorCommands::editHits.
#include <algorithm>
#include <iterator>
#include <cmath>

#include "EditorCommands.h"

namespace cf = chopfractal;

namespace {
using PatternResult = cf::Result<cf::pattern::Pattern>;

// Applies `step` to every id in turn; stops at the first refusal so nothing is partially applied.
PatternResult forEachHit(const cf::pattern::Pattern& p, const std::vector<cf::EventId>& ids, const std::function<PatternResult(const cf::pattern::Pattern&, cf::EventId)>& step) {
  if (ids.empty()) return cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit first.");
  cf::pattern::Pattern edited = p;
  for (const auto id : ids) {
    auto r = step(edited, id);
    if (!r.ok()) return r.error();
    edited = std::move(r.value());
  }
  return edited;
}
}  // namespace

juce::String EditorCommands::hitGuard(cf::EventId id) const {
  const auto& v = model_.view();
  for (const auto& r : v.pattern.rects)
    if (r.id == id) {
      if (!r.topLevel) return kMsgNested;
      const bool barLocked = r.bar >= 0 && r.bar < static_cast<int>(v.pattern.barLocked.size()) && v.pattern.barLocked[static_cast<std::size_t>(r.bar)];
      if (r.locked || barLocked) return kMsgLocked;
      return {};
    }
  return "That hit no longer exists.";
}

cf::Ticks EditorCommands::snapGridTicks() const {
  const int div = model_.extras().ui.snapDivision;
  if (div > 0) return std::max<cf::Ticks>(1, 4 * cf::kTicksPerQuarter / div);
  return std::max<cf::Ticks>(1, model_.view().gridTicks);
}

void EditorCommands::moveHit(cf::EventId id, int bar, cf::Ticks startInBar, int lane) {
  if (const auto m = hitGuard(id); m.isNotEmpty()) return say(m);
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> PatternResult {
    const cf::Event* ev = cf::pattern::findEvent(p, id);
    if (!ev) return cf::makeError(cf::ErrorCode::NotFound, "That hit no longer exists.");
    auto r = cf::pattern::moveEvent(p, id, bar, startInBar);
    if (!r.ok() || lane < 0 || lane >= static_cast<int>(chops.chops.size()) || chops.chops[static_cast<std::size_t>(lane)].id == ev->chop) return r;
    return cf::pattern::setEventChop(r.value(), id, chops.chops[static_cast<std::size_t>(lane)].id, chops);  // dropping on another lane changes the chop
  });
}

void EditorCommands::resizeHit(cf::EventId id, cf::Ticks newDuration) {
  if (const auto m = hitGuard(id); m.isNotEmpty()) return say(m);
  bool hadChild = false;
  for (const auto& r : model_.view().pattern.rects)
    if (r.id == id) {
      hadChild = r.hasChild && r.durationTick != newDuration;
      if (model_.view().barTicks > 0 && model_.view().barTicks - r.startInBar < cf::ui::kMinHitTicks) return say("No room after this hit in its bar.");
    }
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) { return cf::pattern::resizeEvent(p, id, newDuration); });
  if (hadChild) say("Zoom content of this hit was removed because its length changed.");
}

void EditorCommands::duplicateHit(cf::EventId id, int bar, cf::Ticks startInBar, int lane) {
  if (const auto m = hitGuard(id); m.isNotEmpty() && m != kMsgLocked) return say(m);  // a locked hit can still be copied
  auto copy = std::make_shared<cf::EventId>();
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> PatternResult {
    const cf::Event* ev = cf::pattern::findEvent(p, id);
    if (!ev) return cf::makeError(cf::ErrorCode::NotFound, "That hit no longer exists.");
    if (static_cast<int>(cf::pattern::eventCount(p)) >= p.settings.maxEvents)
      return cf::makeError(cf::ErrorCode::LimitExceeded, "The pattern already has the maximum of " + std::to_string(p.settings.maxEvents) + " hits.");
    auto r = cf::pattern::duplicateEvent(p, id, bar, startInBar, copy.get());
    if (!r.ok() || lane < 0 || lane >= static_cast<int>(chops.chops.size()) || chops.chops[static_cast<std::size_t>(lane)].id == ev->chop) return r;
    return cf::pattern::setEventChop(r.value(), *copy, chops.chops[static_cast<std::size_t>(lane)].id, chops);
  });
  if (copy->valid()) model_.select(*copy);
}

void EditorCommands::addHit(int bar, cf::Ticks startInBar, int lane) {
  const cf::Ticks grid = snapGridTicks();
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> PatternResult {
    if (lane < 0 || lane >= static_cast<int>(chops.chops.size())) return cf::makeError(cf::ErrorCode::OutOfRange, "There is no chop in that lane.");
    if (static_cast<int>(cf::pattern::eventCount(p)) >= p.settings.maxEvents)
      return cf::makeError(cf::ErrorCode::LimitExceeded, "The pattern already has the maximum of " + std::to_string(p.settings.maxEvents) + " hits.");
    const cf::Ticks barT = cf::pattern::barTicks(p.settings);
    const cf::Ticks dur = std::min(grid, barT - startInBar);
    return cf::pattern::addEvent(p, bar, chops.chops[static_cast<std::size_t>(lane)].id, startInBar, dur, chops);
  });
}

void EditorCommands::deleteHit(cf::EventId id) {
  if (const auto m = hitGuard(id); m.isNotEmpty()) return say(m);
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) { return cf::pattern::deleteEvent(p, id); });
  if (model_.selected() == id) model_.clearSelection();
}

void EditorCommands::setHitEnabled(cf::EventId id, bool enabled) {
  if (const auto m = hitGuard(id); m.isNotEmpty()) return say(m);
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) { return cf::pattern::setEventEnabled(p, id, enabled); });
}

void EditorCommands::setHitLocked(cf::EventId id, bool locked) { setHitsLocked({id}, locked); }

void EditorCommands::setHitsLocked(const std::vector<cf::EventId>& ids, bool locked) {
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
    return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) { return cf::pattern::setLock(q, {cf::ScopeLevel::Event, 0, 0, id}, locked); });
  });
}

void EditorCommands::resetHit(cf::EventId id) {
  if (const auto m = hitGuard(id); m.isNotEmpty()) return say(m);
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> PatternResult {
    auto a = cf::pattern::setEventTransform(p, id, cf::EventTransform{});
    if (!a.ok()) return a;
    auto b = cf::pattern::setEventFx(a.value(), id, cf::EventFx{});
    if (!b.ok()) return b;
    auto c = cf::pattern::setEventGestureFx(b.value(), id, cf::pattern::GestureFx{});
    if (!c.ok()) return c;
    return cf::pattern::setEventProbability(c.value(), id, 1.f);
  });
}

void EditorCommands::setHitsTransform(const std::vector<cf::EventId>& ids, const std::function<void(cf::EventTransform&)>& mutate) {
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
    return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) -> PatternResult {
      const cf::Event* e = cf::pattern::findEvent(q, id);
      if (!e) return cf::makeError(cf::ErrorCode::NotFound, "That hit no longer exists.");
      cf::EventTransform tx = e->tx;
      mutate(tx);
      return cf::pattern::setEventTransform(q, id, tx);
    });
  });
}

void EditorCommands::setHitsFx(const std::vector<cf::EventId>& ids, const std::function<void(cf::EventFx&)>& mutate) {
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
    return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) -> PatternResult {
      const cf::Event* e = cf::pattern::findEvent(q, id);
      if (!e) return cf::makeError(cf::ErrorCode::NotFound, "That hit no longer exists.");
      cf::EventFx fx = e->fx;
      mutate(fx);
      return cf::pattern::setEventFx(q, id, fx);
    });
  });
}

void EditorCommands::setHitsGesture(const std::vector<cf::EventId>& ids, const std::function<void(cf::pattern::GestureFx&)>& mutate) {
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
    return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) -> PatternResult {
      const cf::Event* e = cf::pattern::findEvent(q, id);
      if (!e) return cf::makeError(cf::ErrorCode::NotFound, "That hit no longer exists.");
      cf::pattern::GestureFx g{e->echo, e->tremPan, e->stutter, e->roll};
      mutate(g);
      return cf::pattern::setEventGestureFx(q, id, g);
    });
  });
}

void EditorCommands::setHitsFxState(const std::vector<cf::EventId>& ids, const std::function<void(cf::ui::HitFx&)>& mutate) {
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
    return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) -> PatternResult {
      const cf::Event* e = cf::pattern::findEvent(q, id);
      if (!e) return cf::makeError(cf::ErrorCode::NotFound, "That hit no longer exists.");
      cf::ui::HitFx state = cf::ui::hitFxOf(*e);
      mutate(state);
      auto a = cf::pattern::setEventFx(q, id, state.fx);
      if (!a.ok()) return a;
      return cf::pattern::setEventGestureFx(a.value(), id, state.gesture);
    });
  });
}

void EditorCommands::setRoleOfHits(const std::vector<cf::EventId>& ids, const juce::String& role) {
  run([=](cf::composition::ProjectSession& s) -> cf::Status {
    if (ids.empty() || !s.pattern()) return cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit first; its chop gets the role (used by the next Generate or Mutate).");
    std::vector<cf::ChopId> chops;
    for (const auto id : ids) {
      const cf::Event* e = cf::pattern::findEvent(*s.pattern(), id);
      if (e && std::find(chops.begin(), chops.end(), e->chop) == chops.end()) chops.push_back(e->chop);
    }
    for (const auto chop : chops) {
      auto st = s.assignRole(chop, role.toStdString());
      if (!st.ok()) return st;
    }
    return cf::Status{};
  });
}

void EditorCommands::setHitsProbability(const std::vector<cf::EventId>& ids, float probability) {
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
    return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) { return cf::pattern::setEventProbability(q, id, probability); });
  });
}

void EditorCommands::previewHitsTransform(const std::vector<cf::EventId>& ids, const std::function<void(cf::EventTransform&)>& mutate) {
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    s.previewEdit([&](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
      return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) -> PatternResult {
        const cf::Event* e = cf::pattern::findEvent(q, id);
        if (!e) return cf::makeError(cf::ErrorCode::NotFound, "That hit no longer exists.");
        cf::EventTransform tx = e->tx;
        mutate(tx);
        return cf::pattern::setEventTransform(q, id, tx);
      });
    });
  });
}

void EditorCommands::previewHitsProbability(const std::vector<cf::EventId>& ids, float probability) {
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    s.previewEdit([&](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
      return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) { return cf::pattern::setEventProbability(q, id, probability); });
    });
  });
}

void EditorCommands::setHitsLane(const std::vector<cf::EventId>& ids, int lane) {
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) {
    return forEachHit(p, ids, [&](const cf::pattern::Pattern& q, cf::EventId id) -> PatternResult {
      if (lane < 0 || lane >= static_cast<int>(chops.chops.size())) return cf::makeError(cf::ErrorCode::OutOfRange, "There is no chop in that lane.");
      return cf::pattern::setEventChop(q, id, chops.chops[static_cast<std::size_t>(lane)].id, chops);
    });
  });
}

// ---------------------------------------------------------------------------------------------
// markers
// ---------------------------------------------------------------------------------------------

void EditorCommands::moveMarker(cf::ChopId id, std::int64_t frame) {
  run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.moveMarker(id, frame); }); });
}

void EditorCommands::deleteMarker(cf::ChopId id) {
  run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.deleteMarker(id); }); });
}

void EditorCommands::setMarkerEnabled(cf::ChopId id, bool enabled) {
  run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.setEnabled(id, enabled); }); });
}

void EditorCommands::renameMarker(cf::ChopId id, const std::string& name) {
  run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.setLabel(id, name); }); });
}

void EditorCommands::trimMarker(cf::ChopId id, std::uint32_t trimStart, std::uint32_t trimEnd) {
  run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.setTrim(id, trimStart, trimEnd); }); });
}

void EditorCommands::undoMarkers() {
  run([](cf::composition::ProjectSession& s) { return s.undoMarkers(); });
}

void EditorCommands::redoMarkers() {
  run([](cf::composition::ProjectSession& s) { return s.redoMarkers(); });
}

bool EditorCommands::canUndoMarkers() const {
  bool can = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) { can = s.canUndoMarkers(); });
  return can;
}

bool EditorCommands::canRedoMarkers() const {
  bool can = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) { can = s.canRedoMarkers(); });
  return can;
}

void EditorCommands::setMarkerSnap(int mode) {
  model_.setExtras([mode](cf::composition::ProjectExtras& e) { e.ui.markerSnap = std::max(0, std::min(2, mode)); });
  model_.setStatus(mode == 0 ? "Marker snap: off" : mode == 1 ? "Marker snap: transient" : "Marker snap: zero crossing");
}

void EditorCommands::setGridSnap(bool on) {
  model_.setExtras([on](cf::composition::ProjectExtras& e) { e.ui.gridSnap = on; });
}

void EditorCommands::setSnapDivision(int division) {
  static const int kAllowed[] = {0, 4, 8, 16, 32};
  if (std::find(std::begin(kAllowed), std::end(kAllowed), division) == std::end(kAllowed)) return;
  model_.setExtras([division](cf::composition::ProjectExtras& e) { e.ui.snapDivision = division; });
}

std::int64_t EditorCommands::snapFrame(std::int64_t frame, double framesPerPixel, bool bypass) const {
  const auto mode = static_cast<cf::ui::SnapMode>(std::max(0, std::min(2, model_.extras().ui.markerSnap)));
  if (bypass || mode == cf::ui::SnapMode::Off) return frame;
  std::int64_t out = frame;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    const auto pb = s.playback();
    if (!pb || !pb->source || pb->source->channels < 1) return;
    const float* ch[2] = {pb->source->channel(0), pb->source->channel(pb->source->channels - 1)};
    out = cf::ui::snapMarkerFrame(ch, std::min(2, pb->source->channels), pb->source->frames, frame, mode, cf::ui::snapRadiusFrames(framesPerPixel));
  });
  return out;
}

// ---------------------------------------------------------------------------------------------
// detection preview
// ---------------------------------------------------------------------------------------------

void EditorCommands::refreshDetectPreview(const cf::composition::DetectOptions& options) {
  EditorModel::DetectPreview preview;
  cf::Status status;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    auto r = s.previewChops(options);
    if (!r.ok()) {
      status = r.error();
      return;
    }
    preview.options = options;
    for (const auto& m : r.value()) preview.frames.push_back(m.position);
    std::sort(preview.frames.begin(), preview.frames.end());
    preview.existing = s.chopMap() ? static_cast<int>(s.chopMap()->markers().size()) : 0;
    preview.active = true;
  });
  if (!status.ok()) {
    model_.clearDetectPreview();
    model_.setStatus(juce::String(status.error().message));
    return;
  }
  model_.setDetectPreview(std::move(preview));
  model_.setStatus("Detection proposes " + juce::String(static_cast<int>(model_.detectPreview().frames.size())) + " chops (" + juce::String(model_.detectPreview().existing) + " now). Apply or cancel.");
}

bool EditorCommands::beginDetect(DetectChoice choice) {
  detectMode_ = static_cast<int>(choice);
  bool hasMarkers = false;
  cf::composition::DetectOptions options;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    hasMarkers = s.chopMap() && !s.chopMap()->markers().empty();
    options = detectOptionsFor(detectMode_, s);
  });
  if (!hasMarkers) {  // nothing to merge with: apply straight away
    model_.clearDetectPreview();
    detectChops(choice);
    return false;
  }
  refreshDetectPreview(options);
  return model_.detectPreview().active;
}

void EditorCommands::setDetectMerge(bool merge) {
  if (!model_.detectPreview().active) return;
  auto options = model_.detectPreview().options;
  options.merge = merge ? cf::source::MergeMode::Merge : cf::source::MergeMode::Replace;
  refreshDetectPreview(options);
}

void EditorCommands::applyDetect() {
  if (!model_.detectPreview().active) return;
  const auto options = model_.detectPreview().options;
  model_.clearDetectPreview();
  run([=](cf::composition::ProjectSession& s) { return s.applyChops(options); });
}

void EditorCommands::cancelDetect() {
  if (!model_.detectPreview().active) return;
  model_.clearDetectPreview();
  model_.setStatus("Detection cancelled; your markers are unchanged.");
}

// ---------------------------------------------------------------------------------------------
// rules editor
// ---------------------------------------------------------------------------------------------

bool EditorCommands::applyRules(const cf::roles::RuleSet& rules) {
  const auto st = run([=](cf::composition::ProjectSession& s) { return s.setRules(rules); });
  if (st.ok()) model_.setStatus("Rules applied. They take effect at the next Generate or Mutate.");
  return st.ok();
}

void EditorCommands::testRules(const cf::roles::RuleSet& rules) {
  juce::String message;
  const cf::host::ParamValues params = proc_.currentParams();
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::Settings settings;
    const bool haveSettings = s.pattern() != nullptr;
    if (haveSettings) settings = s.pattern()->settings;
    else settings.density = params.get(cf::host::kDensity);
    auto r = s.previewRules(rules, settings);
    if (!r.ok()) {
      message = juce::String(r.error().message);
      return;
    }
    const auto& p = r.value();
    const juce::String prefix = haveSettings ? "Test: " : "Test (default settings): ";
    if (p.blocked == 0) {
      message = prefix + "no rule blocks anything with the current settings.";
      return;
    }
    juce::String parts;
    for (const auto& kv : p.blockedByRule) {
      if (parts.isNotEmpty()) parts << ", ";
      parts << juce::String(kv.second) << " by " << (kv.first ? "rule " + juce::String(static_cast<int>(kv.first)) : juce::String("a rule"));
    }
    message = prefix + juce::String(p.blocked) + (p.blocked == 1 ? " change would be blocked" : " changes would be blocked") + (p.blockedByRule.size() > 1 ? ": " : " ") + (p.blockedByRule.size() > 1 ? parts : "by " + (p.blockedByRule.front().first ? "rule " + juce::String(static_cast<int>(p.blockedByRule.front().first)) : juce::String("a rule"))) + ". Nothing was changed.";
  });
  model_.setStatus(message);
}

juce::String EditorCommands::explainSelected() {
  const auto id = model_.selected();
  if (!id.valid()) {
    model_.setStatus("Select a hit first; Why? explains that hit.");
    return {};
  }
  juce::String text;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    auto r = s.explainEvent(id);
    text = r.ok() ? juce::String(r.value().text) : juce::String(r.error().message);
  });
  model_.setStatus(text);
  return text;
}

bool EditorCommands::setChopRole(cf::ChopId chop, const juce::String& role) {
  const auto st = run([=](cf::composition::ProjectSession& s) { return role.isEmpty() ? s.clearRole(chop) : s.assignRole(chop, role.toStdString()); });
  return st.ok();
}

void EditorCommands::selectBeat(int bar, int beat) {
  const auto& v = model_.view();
  const cf::Ticks beatTicks = std::max<cf::Ticks>(1, v.barTicks / 4);
  for (const auto& r : v.pattern.rects)
    if (r.topLevel && r.bar == bar && (r.startInBar / beatTicks) == beat) {
      model_.clearSelection();
      model_.select(r.id);
      return;
    }
  model_.setStatus("There is no hit in that beat now.");
}

// ---------------------------------------------------------------------------------------------
// history drawer
// ---------------------------------------------------------------------------------------------

void EditorCommands::setNodeFavorite(cf::history::NodeId id, bool favorite) {
  run([=](cf::composition::ProjectSession& s) { return s.setFavorite(id, favorite); });
}

void EditorCommands::renameNode(cf::history::NodeId id, const std::string& name) {
  if (name.empty()) {
    model_.setStatus("A variation needs a name.");
    return;
  }
  run([=](cf::composition::ProjectSession& s) { return s.renameNode(id, name); });
}

void EditorCommands::deleteBranch(cf::history::NodeId id) {
  run([=](cf::composition::ProjectSession& s) { return s.deleteBranch(id); });
}

juce::String EditorCommands::compareText(cf::history::NodeId a, cf::history::NodeId b) {
  juce::String text;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    auto r = s.compareNodes(a, b);
    text = r.ok() ? juce::String(r.value()) : juce::String(r.error().message);
  });
  model_.setStatus(text);
  return text;
}

void EditorCommands::beginMorph(double t) {
  run([=](cf::composition::ProjectSession& s) { return s.beginMorph(t); });
}

void EditorCommands::setMorph(double t) {
  run([=](cf::composition::ProjectSession& s) { return s.setMorph(t); });
}

void EditorCommands::commitMorph() {
  run([=](cf::composition::ProjectSession& s) { return s.commitMorph(); });
}

void EditorCommands::cancelMorph() {
  run([=](cf::composition::ProjectSession& s) {
    s.cancelMorph();
    return cf::Status{};
  });
}
