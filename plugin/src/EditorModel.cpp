#include "EditorModel.h"

#include <algorithm>
#include <cmath>

namespace cf = chopfractal;

EditorModel::EditorModel(ChopFractalProcessor& processor) : proc_(processor) {}

bool EditorModel::refresh(bool force, int waveWidthPx) {
  waveWidthPx_ = waveWidthPx;
  bool rebuilt = false;
  bool selectionChanged = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    juce::String sig;
    sig << juce::String::toHexString(reinterpret_cast<std::uintptr_t>(s.playback().get())) << "|" << static_cast<int>(s.history().size()) << "|"
        << static_cast<juce::int64>(s.history().active()) << "|" << (s.chopMap() ? static_cast<int>(s.chopMap()->markers().size()) : -1);
    sourceMissing_ = s.sourceMissing();
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
    if (pb && pb->source && (next.peaks.min.empty() || peaksSource_ != pb->source.get() || peaksWidth_ != waveWidthPx)) {
      peaksSource_ = pb->source.get();
      peaksWidth_ = waveWidthPx;
      const auto ptrs = std::vector<const float*>{pb->source->channel(0), pb->source->channel(pb->source->channels - 1)};
      next.peaks = cf::ui::computePeaks(ptrs.data(), pb->source->channels, pb->source->frames, {0, pb->source->frames}, std::max(1, waveWidthPx));
    }
    if (!pb || !pb->source) next.peaks = {};
    if (s.chopMap()) {
      next.markers = cf::ui::buildMarkerViews(*s.chopMap(), cf::ui::ViewWindow{0, std::max<std::int64_t>(1, next.sourceFrames), static_cast<double>(waveWidthPx)});
      if (s.chops()) next.chops = s.chops()->chops;
      if (s.pattern() && s.chops()) {
        next.pattern = cf::ui::buildPatternView(*s.pattern(), *s.chops(), [&](cf::ChopId id) { return s.roleMap().roleOf(id); });
        next.orbit = cf::ui::buildOrbitView(*s.pattern(), *s.chops());
        next.barTicks = cf::pattern::barTicks(s.pattern()->settings);
        next.gridTicks = cf::gridTicks(s.pattern()->settings.grid);
        next.barCount = static_cast<int>(s.pattern()->bars.size());
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
    selectionChanged = selectionChanged || hitsBefore != selectedHits_.size() || chopsBefore != selectedChops_.size();
  });

  emit(Change::Evolve);  // every tick: the Feature panel compares the session's Evolve state with its own controls
  if (rebuilt) {
    emit(Change::Setup);
    if (selectionChanged) emit(Change::Selection);
    emit(Change::View);
  }
  const bool embed = embedSource();
  if (!flagsPublished_ || publishedSourceMissing_ != sourceMissing_ || publishedEmbed_ != embed) {
    flagsPublished_ = true;
    publishedSourceMissing_ = sourceMissing_;
    publishedEmbed_ = embed;
    emit(Change::Flags);
  }
  // Input capture changes state on the audio thread: publish when the state or the elapsed tenth of a second changes.
  const auto capture = proc_.captureInfo();
  const juce::String captureKey = juce::String(static_cast<int>(capture.state)) + "|" + juce::String(capture.seconds, 1) + "|" + juce::String(capture.targetSeconds, 1);
  if (captureKey != lastCaptureKey_) {
    lastCaptureKey_ = captureKey;
    emit(Change::Capture);
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
  const float laneHeight = static_cast<float>(canvas.getHeight()) / static_cast<float>(std::max<std::size_t>(1, view_.chops.size()));
  for (const auto& hit : view_.pattern.rects) {
    const juce::Rectangle<float> bounds(canvas.getX() + static_cast<float>(hit.x0) * canvas.getWidth(), canvas.getY() + hit.chopIndex * laneHeight,
                                        static_cast<float>(hit.x1 - hit.x0) * canvas.getWidth(), laneHeight);
    if (hit.depth == 0 && (click ? bounds.contains(end.toFloat()) : bounds.intersects(band.toFloat()))) selectedHits_.push_back(hit.id);
  }
  if (!selectedHits_.empty()) selected_ = selectedHits_.front();
  announceSelection();
}

void EditorModel::setSeed(std::uint64_t seed) {
  seed_ = seed;
  emit(Change::Seed);
}

void EditorModel::setSetup(cf::composition::SetupSuggestion s) {
  setup_ = std::move(s);
  haveSetup_ = true;
  emit(Change::Setup);
}

void EditorModel::setEditMode(bool on) {
  editMode_ = on;
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
