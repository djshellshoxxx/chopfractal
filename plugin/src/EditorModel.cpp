#include "EditorModel.h"

#include <algorithm>
#include <chopfractal/plugin_ui_adapter/session_views.hpp>
#include <cmath>

namespace cf = chopfractal;

EditorModel::EditorModel(ChopFractalProcessor& processor) : proc_(processor) {
  proc_.withSession([this](cf::composition::ProjectSession& s) { extras_ = s.extras(); });  // an editor opened on a restored project starts from its extras
  publishedExtras_ = extras_;  // the panels read extras() while they are constructed, so the first refresh has nothing to announce
  extrasPublished_ = true;
}

bool EditorModel::refresh(bool force, int waveWidthPx) {
  waveWidthPx_ = waveWidthPx;
  bool rebuilt = false;
  bool selectionChanged = false;
  cf::composition::ProjectExtras sessionExtras;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    juce::String sig;
    sig << juce::String::toHexString(reinterpret_cast<std::uintptr_t>(s.playback().get())) << "|" << static_cast<juce::int64>(waveStart_) << "-" << static_cast<juce::int64>(waveEnd_) << "|" << static_cast<int>(s.history().size()) << "|"
        << static_cast<juce::int64>(s.history().active()) << "|" << (s.chopMap() ? static_cast<int>(s.chopMap()->markers().size()) : -1) << "|"
        << (s.morphActive() ? juce::String(s.morphAmount(), 4) : juce::String("-")) << "|" << (s.displayPattern() != s.pattern() ? "p" : "-");
    sourceMissing_ = s.sourceMissing();
    sessionExtras = s.extras();
    evolveRunning_ = s.evolveRunning();
    evolve_ = s.evolveSettings();
    if (!force && sig == signature_) return;
    rebuilt = true;
    signature_ = sig;
    EditorView next;
    next.revision = view_.revision + 1;
    next.peaks = std::move(view_.peaks);
    next.barTicks = view_.barTicks;
    next.gridTicks = view_.gridTicks;
    next.barCount = view_.barCount;
    const auto pb = s.playback();
    if (s.chopMap()) next.sourceFrames = s.chopMap()->info().frames;
    // The zoom window belongs to one source and one length; anything else shows the whole clip again.
    if (!pb || !pb->source || waveSource_ != pb->source.get() || waveEnd_ > next.sourceFrames) {
      waveSource_ = pb && pb->source ? pb->source.get() : nullptr;
      waveStart_ = 0;
      waveEnd_ = 0;
    }
    const std::int64_t winStart = waveEnd_ > waveStart_ ? waveStart_ : 0;
    const std::int64_t winEnd = waveEnd_ > waveStart_ ? waveEnd_ : std::max<std::int64_t>(1, next.sourceFrames);
    next.window = cf::ui::ViewWindow{winStart, winEnd, static_cast<double>(std::max(1, waveWidthPx))};
    if (pb && pb->source && (next.peaks.min.empty() || peaksSource_ != pb->source.get() || peaksWidth_ != waveWidthPx || peaksStart_ != winStart || peaksEnd_ != winEnd)) {
      peaksSource_ = pb->source.get();
      peaksWidth_ = waveWidthPx;
      peaksStart_ = winStart;
      peaksEnd_ = winEnd;
      const auto ptrs = std::vector<const float*>{pb->source->channel(0), pb->source->channel(pb->source->channels - 1)};
      next.peaks = cf::ui::computePeaks(ptrs.data(), pb->source->channels, pb->source->frames, {winStart, winEnd}, std::max(1, waveWidthPx));
    }
    if (!pb || !pb->source) next.peaks = {};
    if (s.chopMap()) {
      next.markers = cf::ui::buildMarkerViews(*s.chopMap(), next.window);
      if (s.chops()) next.chops = s.chops()->chops;
      if (const auto* shown = s.displayPattern(); shown && s.chops()) {
        next.pattern = cf::ui::buildPatternView(*shown, *s.chops(), [&](cf::ChopId id) { return s.roleMap().roleOf(id); });
        next.orbit = cf::ui::buildOrbitView(*shown, *s.chops());
        next.barTicks = cf::pattern::barTicks(shown->settings);
        next.gridTicks = cf::gridTicks(shown->settings.grid);
        next.barCount = static_cast<int>(shown->bars.size());
        if (s.morphActive()) next.previewLabel = juce::String::fromUTF8(cf::ui::buildMorphView({true, true, true, s.morphAmount(), false}).badgeText.c_str());
        else if (shown != s.pattern()) next.previewLabel = "Scene preview";
      }
    }
    next.tree = cf::ui::layoutHistoryTree(s.history().listAll(), s.history().active());
    for (const auto& n : next.tree) {
      next.treeCols = std::max(next.treeCols, n.column);
      next.treeRows = std::max(next.treeRows, n.depth);
    }
    next.hasPattern = s.pattern() != nullptr;
    next.hasSource = next.sourceFrames > 0;
    view_ = std::move(next);
    laneScroll_ = cf::ui::clampLaneScroll(lanes(), laneScroll_);  // fewer chops can shrink the scroll range
    haveSetup_ = false;  // Smart Setup suggestions belong to the chops they were made for
    const auto& rects = view_.pattern.rects;
    if (selected_.valid() && std::none_of(rects.begin(), rects.end(), [&](const cf::ui::EventRect& r) { return r.id == selected_; })) {
      selected_ = cf::EventId{};
      selectionChanged = true;
    }
    const auto hitsBefore = selectedHits_.size(), chopsBefore = selectedChops_.size();
    selectedHits_.erase(std::remove_if(selectedHits_.begin(), selectedHits_.end(), [&](cf::EventId id) {
      return std::none_of(rects.begin(), rects.end(), [&](const cf::ui::EventRect& r) { return r.id == id; });
    }), selectedHits_.end());
    selectedChops_.erase(std::remove_if(selectedChops_.begin(), selectedChops_.end(), [&](cf::ChopId id) {
      return std::none_of(view_.chops.begin(), view_.chops.end(), [&](const cf::ChopInfo& chop) { return chop.id == id; });
    }), selectedChops_.end());
    if (selectedMarker_.valid() && std::none_of(view_.markers.begin(), view_.markers.end(), [&](const cf::ui::MarkerView& m) { return m.id == selectedMarker_; })) {
      selectedMarker_ = cf::ChopId{};
      selectionChanged = true;
    }
    selectionChanged = selectionChanged || hitsBefore != selectedHits_.size() || chopsBefore != selectedChops_.size();
  });

  if (!extrasPublished_ || !(sessionExtras == publishedExtras_)) {
    // A project load (or anything else that replaced the session's extras behind the panels' backs) re-syncs the widgets.
    extrasPublished_ = true;
    extras_ = sessionExtras;
    publishedExtras_ = sessionExtras;
    emit(Change::Seed);
    emit(Change::Extras);
  }
  emit(Change::Evolve);  // every tick: the Feature panel compares the session's Evolve state with its own controls
  if (rebuilt) {
    emit(Change::Setup);
    if (selectionChanged) emit(Change::Selection);
    emit(Change::View);
  }
  const bool embed = embedSource();
  const ExportStatus exportNow = proc_.exportStatus();
  const bool exporting = exportNow.state == ExportStatus::State::Running;
  if (exporting)  // the worker reports a fraction; the tick turns it into the status text
    proc_.setStatusMessage("Exporting " + proc_.exportDisplayName() + ": " + juce::String(static_cast<int>(exportNow.fraction * 100.f)) + "%");
  if (!flagsPublished_ || publishedSourceMissing_ != sourceMissing_ || publishedEmbed_ != embed || publishedExport_ != exporting) {
    flagsPublished_ = true;
    publishedSourceMissing_ = sourceMissing_;
    publishedEmbed_ = embed;
    publishedExport_ = exporting;
    emit(Change::Flags);
  }
  // Messages arrive from other threads' work too (file loading, Evolve, restore): publish the status when it changes.
  const juce::String statusKey = proc_.statusMessage() + (usedFallbackTempo() ? "|t" : "") + (usedFallbackMeter() ? "|m" : "") + juce::String(manualBpm());
  if (statusKey != lastStatusKey_) {
    lastStatusKey_ = statusKey;
    emit(Change::Status);
  }
  return rebuilt;
}

void EditorModel::select(cf::EventId id) {
  if (selected_ == id) return;
  selected_ = id;
  emit(Change::Selection);
}

void EditorModel::clearSelection() {
  if (!selected_.valid() && selectedHits_.empty() && selectedChops_.empty()) return;
  selected_ = cf::EventId{};
  selectedHits_.clear();
  selectedChops_.clear();
  emit(Change::Selection);
}

int EditorModel::selectedBar() const {
  for (const auto& r : view_.pattern.rects)
    if (r.id == selected_) return r.bar;
  return 0;
}

int EditorModel::selectedChopIndex() const {
  for (const auto& r : view_.pattern.rects)
    if (r.id == selected_) return r.chopIndex;
  return -1;
}

void EditorModel::selectSectionsInWave(int startX, int endX, bool click, int canvasWidth) {
  selectedHits_.clear();
  selectedChops_.clear();
  selected_ = {};
  const double lo = static_cast<double>(std::min(startX, endX)) / canvasWidth * view_.sourceFrames;
  const double hi = static_cast<double>(std::max(startX, endX)) / canvasWidth * view_.sourceFrames;
  for (const auto& chop : view_.chops)
    if (click ? (lo >= chop.range.start && lo < chop.range.end) : (hi >= chop.range.start && lo < chop.range.end)) selectedChops_.push_back(chop.id);
  for (const auto& hit : view_.pattern.rects)
    if (hit.depth == 0 && hit.chopIndex >= 0 && hit.chopIndex < static_cast<int>(view_.chops.size()) &&
        std::find(selectedChops_.begin(), selectedChops_.end(), view_.chops[static_cast<std::size_t>(hit.chopIndex)].id) != selectedChops_.end())
      selectedHits_.push_back(hit.id);
  if (!selectedHits_.empty()) selected_ = selectedHits_.front();
  announceSelection();
}

void EditorModel::selectSectionsInPattern(juce::Rectangle<int> band, juce::Point<int> end, bool click, juce::Rectangle<int> canvas) {
  selectedHits_.clear();
  selectedChops_.clear();
  selected_ = {};
  const auto lanes = this->lanes();
  const float laneHeight = static_cast<float>(lanes.laneHeight > 0.0 ? lanes.laneHeight : canvas.getHeight() / static_cast<double>(std::max<std::size_t>(1, view_.chops.size())));
  const float scroll = static_cast<float>(lanes.scroll);
  for (const auto& hit : view_.pattern.rects) {
    const juce::Rectangle<float> bounds(canvas.getX() + static_cast<float>(hit.x0) * canvas.getWidth(), canvas.getY() + hit.chopIndex * laneHeight - scroll,
                                        static_cast<float>(hit.x1 - hit.x0) * canvas.getWidth(), laneHeight);
    if (hit.depth == 0 && (click ? bounds.contains(end.toFloat()) : bounds.intersects(band.toFloat()))) selectedHits_.push_back(hit.id);
  }
  if (!selectedHits_.empty()) selected_ = selectedHits_.front();
  announceSelection();
}

void EditorModel::setWaveWindow(std::int64_t startFrame, std::int64_t endFrame) {
  const std::int64_t total = view_.sourceFrames;
  if (total <= 0) return;
  const std::int64_t span = std::min(total, std::max<std::int64_t>(std::min<std::int64_t>(256, total), endFrame - startFrame));
  const std::int64_t start = std::max<std::int64_t>(0, std::min(startFrame, total - span));
  const bool whole = start == 0 && span >= total;
  const std::int64_t newStart = whole ? 0 : start, newEnd = whole ? 0 : start + span;
  if (newStart == waveStart_ && newEnd == waveEnd_) return;
  waveStart_ = newStart;
  waveEnd_ = newEnd;
  rebuild();
}

void EditorModel::zoomWaveAround(double x, double factor) {
  if (view_.sourceFrames <= 0) return;
  const auto w = view_.window;
  const std::int64_t anchor = cf::ui::xToFrame(w, x);
  const double span = static_cast<double>(w.endFrame - w.startFrame);
  const double minFactor = span / static_cast<double>(view_.sourceFrames);       // cannot zoom out past the whole clip
  const double maxFactor = span / static_cast<double>(std::min<std::int64_t>(256, view_.sourceFrames));  // nor in past 256 frames
  const double f = std::max(minFactor, std::min(maxFactor, factor));
  const auto z = cf::ui::zoomAround(w, anchor, f, view_.sourceFrames);
  setWaveWindow(z.startFrame, z.endFrame);
}

void EditorModel::scrollWaveBy(double deltaPx) {
  if (view_.sourceFrames <= 0) return;
  const auto s = cf::ui::scrolledBy(view_.window, deltaPx, view_.sourceFrames);
  setWaveWindow(s.startFrame, s.endFrame);
}

void EditorModel::selectMarker(cf::ChopId id) {
  if (selectedMarker_ == id) return;
  selectedMarker_ = id;
  emit(Change::Selection);
}

void EditorModel::setLaneView(double viewHeight, double minLaneHeight) {
  if (laneViewH_ == viewHeight && minLaneH_ == minLaneHeight) return;
  laneViewH_ = viewHeight;
  minLaneH_ = minLaneHeight;
  laneScroll_ = lanes().scroll;  // re-clamp after a resize
  emit(Change::Scroll);
}

cf::ui::LaneGeometry EditorModel::lanes() const { return cf::ui::laneGeometry(laneViewH_, static_cast<int>(view_.chops.size()), minLaneH_, laneScroll_); }

void EditorModel::setLaneScroll(double scroll) {
  const double next = cf::ui::clampLaneScroll(lanes(), scroll);
  if (next == laneScroll_) return;
  laneScroll_ = next;
  emit(Change::Scroll);
}

void EditorModel::revealLane(int lane) { setLaneScroll(cf::ui::scrollToRevealLane(lanes(), lane)); }

void EditorModel::setDetectPreview(DetectPreview p) {
  detect_ = std::move(p);
  emit(Change::Detect);
}

void EditorModel::clearDetectPreview() {
  if (!detect_.active && detect_.frames.empty()) return;
  detect_ = {};
  emit(Change::Detect);
}

bool EditorModel::setExtras(const std::function<void(cf::composition::ProjectExtras&)>& mutate) {
  cf::composition::ProjectExtras next = extras_;
  mutate(next);
  if (next == extras_) return true;
  cf::Status status;
  proc_.withSession([&](cf::composition::ProjectSession& s) { status = s.setExtras(next); });
  if (!status.ok()) return false;  // invalid (for example an empty motif): the session keeps the previous value
  extras_ = next;
  publishedExtras_ = next;  // the panels made this change themselves; they need no re-sync
  proc_.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
  return true;
}

void EditorModel::setSeed(std::uint64_t seed) {
  if (seed > 999999999999ull) seed %= 1000000000000ull;  // the field holds at most 12 digits
  setExtras([seed](cf::composition::ProjectExtras& e) { e.seed = seed; });
  emit(Change::Seed);
}

void EditorModel::setSetup(cf::composition::SetupSuggestion s) {
  setup_ = std::move(s);
  haveSetup_ = true;
  emit(Change::Setup);
}

void EditorModel::setEditMode(bool on) {
  setExtras([on](cf::composition::ProjectExtras& e) { e.ui.editHits = on; });
  emit(Change::Flags);
}

juce::String EditorModel::statusLine() const {
  juce::String line = proc_.statusMessage();
  if (usedFallbackTempo()) line << (line.isEmpty() ? "" : "\n") << "Using local tempo: " << juce::String(manualBpm(), 1) << " BPM";
  if (usedFallbackMeter()) line << "   |   Assuming 4/4";
  return line;
}

void EditorModel::setStatus(const juce::String& text) {
  proc_.setStatusMessage(text);
  emit(Change::Status);
}

void EditorModel::announceSelection() {
  setStatus(juce::String(static_cast<int>(selectedChops_.size())) + " source sections, " + juce::String(static_cast<int>(selectedHits_.size())) +
            " pattern hits selected. Zoom In subdivides selected hits.");
  emit(Change::Selection);
}

chopfractal::ui::InspectorView EditorModel::inspector() {
  const auto ids = targetHits();
  chopfractal::ui::InspectorView v;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (s.pattern() && s.chops()) v = cf::ui::buildInspectorView(*s.pattern(), ids, *s.chops(), [&](cf::ChopId id) { return s.roleMap().roleOf(id); });
    else v = cf::ui::buildInspectorView(cf::pattern::Pattern{}, {}, cf::ChopSnapshot{});
  });
  return v;
}

chopfractal::ui::FxRackView EditorModel::fxRack() {
  const auto ids = targetHits();
  chopfractal::ui::FxRackView v;
  proc_.withSession([&](cf::composition::ProjectSession& s) { v = cf::ui::buildFxRackView(s.pattern() ? *s.pattern() : cf::pattern::Pattern{}, ids); });
  return v;
}

bool EditorModel::firstHitFx(chopfractal::ui::HitFx& out) {
  const auto ids = targetHits();
  bool found = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (ids.empty() || !s.pattern()) return;
    if (const auto* e = cf::pattern::findEvent(*s.pattern(), ids.front())) {
      out = cf::ui::hitFxOf(*e);
      found = true;
    }
  });
  return found;
}

EditorModel::RulesSnapshot EditorModel::rulesSnapshot() {
  RulesSnapshot out;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    out.rules = s.rules();
    out.roles = s.roleMap();
    out.notes = s.lastRuleNotes();
    if (s.pattern()) out.patternBars = static_cast<int>(s.pattern()->bars.size());
    if (s.chops()) {
      out.chops = s.chops()->chops;
      out.sampleRate = s.chops()->sampleRate;
    }
  });
  return out;
}

bool EditorModel::nodeSummary(cf::history::NodeId id, cf::history::NodeSummary& out) {
  bool found = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& n : s.history().listAll())
      if (n.id == id) {
        out = n;
        found = true;
      }
  });
  return found;
}

bool EditorModel::nodePatternView(cf::history::NodeId id, cf::ui::PatternView& out) {
  bool ok = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (!s.chops()) return;
    auto snap = s.history().getSnapshot(id);
    if (!snap.ok() || !snap.value().payload) return;
    auto p = cf::pattern::deserialize(snap.value().payload->data(), snap.value().payload->size());
    if (!p.ok()) return;
    out = cf::ui::buildPatternView(p.value(), *s.chops());
    ok = true;
  });
  return ok;
}

EditorModel::SnapshotState EditorModel::snapshotState() {
  SnapshotState st;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    st.hasA = s.hasSnapshot(0);
    st.hasB = s.hasSnapshot(1);
    st.morphActive = s.morphActive();
    st.morphAmount = s.morphAmount();
  });
  return st;
}

cf::history::NodeId EditorModel::activeNode() const {
  for (const auto& n : view_.tree)
    if (n.active) return n.id;
  return cf::history::kNoNode;
}
