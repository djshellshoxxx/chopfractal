#pragma once
// Headless view models for the editor. The JUCE editor only draws these; all geometry, text, and layout
// decisions are made here so they can be unit tested without a GUI. Everything is derived from immutable
// snapshots: views never mutate project state. Threading: UI thread.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>
#include <chopfractal/source_chop/chop_map.hpp>
#include <chopfractal/variation_history/history.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chopfractal::ui {

// ---- waveform and time mapping ----
struct PeakBins {
  std::vector<float> min;
  std::vector<float> max;
};
// Min/max of the channel average over `bins` equal slices of `range` (clamped to the clip).
PeakBins computePeaks(const float* const* channels, int numChannels, std::int64_t frames, SampleRange range, int bins);

struct ViewWindow {
  std::int64_t startFrame = 0;
  std::int64_t endFrame = 1;  // exclusive
  double widthPx = 1.0;
};
// Computed from the exact window each time, so repeated zooming/scrolling never accumulates drift.
double frameToX(const ViewWindow& w, std::int64_t frame);
std::int64_t xToFrame(const ViewWindow& w, double x);
ViewWindow zoomAround(const ViewWindow& w, std::int64_t anchorFrame, double factor, std::int64_t totalFrames);  // factor > 1 zooms in
ViewWindow scrolledBy(const ViewWindow& w, double deltaPx, std::int64_t totalFrames);

struct MarkerView {
  ChopId id;
  double x = 0.0;
  bool visible = false;  // inside the current window
  bool enabled = true;
  bool manual = true;
  std::string label;
  std::uint32_t color = 0;
};
std::vector<MarkerView> buildMarkerViews(const source::ChopMap& map, const ViewWindow& window);

// ---- pattern ----
struct EventRect {
  EventId id;
  EventId parent;       // invalid for top-level events
  int bar = 0;
  int beat = 0;
  int depth = 0;        // 0 = top level, 1+ = nested inside a zoomed hit
  double x0 = 0.0;      // fraction of the whole pattern length
  double x1 = 0.0;
  int chopIndex = -1;   // index into the chop snapshot, for colour/lane assignment (never the only indicator)
  float level = 1.f;
  bool locked = false;
  bool userOwned = false;
  bool hasChild = false;
  bool childActive = false;
  std::string description;  // accessible text
};
struct PatternView {
  std::int64_t lengthTicks = 0;
  std::vector<double> barLines;   // fractions of the pattern length
  std::vector<double> beatLines;
  std::vector<bool> barLocked;
  std::vector<EventRect> rects;
};
using RoleLookup = std::function<std::string(ChopId)>;
PatternView buildPatternView(const pattern::Pattern& p, const ChopSnapshot& chops, const RoleLookup& roleOf = {});

// "Phrase", "Bar 2", "Beat 3", "Hit 1", "Child 2", ... down to the event; empty if the id is unknown.
std::vector<std::string> breadcrumbFor(const pattern::Pattern& p, EventId id);

// ---- history tree ----
struct TreeNodeView {
  history::NodeId id = history::kNoNode;
  history::NodeId parent = history::kNoNode;
  int depth = 0;   // distance from a root
  int column = 0;  // leaves get consecutive columns; a parent sits above its first child
  bool active = false;
  bool favorite = false;
  std::string label;
};
std::vector<TreeNodeView> layoutHistoryTree(const std::vector<history::NodeSummary>& nodes, history::NodeId active);

}  // namespace chopfractal::ui
