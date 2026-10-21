#pragma once
// Pure geometry for direct editing (G2 / F02): marker snapping, hit move and resize planning, scrolled lane layout and
// the clamps that keep a dragged marker between its neighbours. No JUCE, no state: the panels feed pointer positions in
// and apply the results through the editor commands. UI thread only.
#include <chopfractal/chop_contracts/time.hpp>
#include <chopfractal/plugin_ui_adapter/hit_edit.hpp>

#include <cstdint>

namespace chopfractal::ui {

// ---- marker snapping ----
enum class SnapMode : std::uint8_t { Off = 0, Transient = 1, ZeroCrossing = 2 };

// Moves `frame` to the strongest energy rise (Transient: d(i) = E(i, i+64) - E(i-64, i) on the channel average) or to the
// nearest sign change (ZeroCrossing) within `radiusFrames`; returns `frame` itself when nothing qualifies, when the mode
// is Off, the clip is empty or the radius is below 1. Never moves further than the radius. Deterministic.
std::int64_t snapMarkerFrame(const float* const* channels, int numChannels, std::int64_t frames, std::int64_t frame, SnapMode mode, std::int64_t radiusFrames);
// Radius used by the waveform: 12 pixels of the current zoom, clamped to 1..2048 frames.
std::int64_t snapRadiusFrames(double framesPerPixel);
// The legal range of a dragged marker: it cannot cross a neighbour or leave a region shorter than `minRegion` frames.
// `prev` / `next` are the neighbouring marker positions (use 0 and the clip length at the ends).
std::int64_t clampMarkerFrame(std::int64_t frame, std::int64_t prev, std::int64_t next, std::int64_t minRegion);

// ---- pattern grid ----
// Nearest multiple of `gridTicks` (ties round up); `enabled == false` or a grid below 1 is the identity.
Ticks snapTickToGrid(Ticks tick, Ticks gridTicks, bool enabled);
constexpr Ticks kMinHitTicks = 60;  // 1/64 note, the engine's minimum hit length

struct HitMoveInput {
  Ticks barTicks = 0;
  int barCount = 0;
  Ticks gridTicks = 0;
  bool snap = true;
  Ticks hitDuration = 0;
  Ticks grabOffsetTicks = 0;  // where inside the hit the pointer took hold
  double pointerX = 0.0, widthPx = 1.0;
  double pointerY = 0.0;      // in the lane viewport's coordinates
  int originalLane = 0;
};
struct HitMove {
  int bar = 0;
  Ticks startInBar = 0;
  Ticks startAbs = 0;
  int lane = 0;
};

// Scrolled lane layout. Lanes are `laneHeight` tall; the content is `lanes * laneHeight`; `scroll` shifts it up.
struct LaneGeometry {
  double viewHeight = 0.0;
  int lanes = 0;
  double laneHeight = 0.0;
  double scroll = 0.0;
  double contentHeight() const { return laneHeight * lanes; }
  double maxScroll() const { return contentHeight() > viewHeight ? contentHeight() - viewHeight : 0.0; }
  bool scrollable() const { return maxScroll() > 0.5; }
};
// Lanes fill the view when they fit at `minLaneHeight`; otherwise each is `minLaneHeight` tall and the content scrolls.
LaneGeometry laneGeometry(double viewHeight, int lanes, double minLaneHeight, double scroll);
double laneTop(const LaneGeometry&, int lane);                  // view coordinates, may be negative
int laneAtScrolledY(const LaneGeometry&, double y);             // -1 with no lanes; clamped to the first / last lane
double clampLaneScroll(const LaneGeometry&, double scroll);
double scrollToRevealLane(const LaneGeometry&, int lane);       // the smallest scroll change that shows the lane fully
// Scrollbar thumb as {top, height} in view coordinates (height 0 when everything fits).
struct Thumb { double top = 0.0, height = 0.0; };
Thumb laneScrollThumb(const LaneGeometry&);
double scrollForThumbTop(const LaneGeometry&, double thumbTop);

HitMove planHitMove(const HitMoveInput&, const LaneGeometry&);
// New duration for a right-edge drag. `pointerX` is where the right edge is now; the end snaps to the grid when `snap`.
Ticks planHitResize(Ticks barTicks, int barCount, Ticks gridTicks, bool snap, Ticks startInBar, int bar, double pointerX, double widthPx);

}  // namespace chopfractal::ui
