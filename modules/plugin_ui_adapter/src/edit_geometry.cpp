#include <chopfractal/plugin_ui_adapter/edit_geometry.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::ui {
namespace {
constexpr std::int64_t kEnergyWindow = 64;

float mono(const float* const* ch, int n, std::int64_t i) {
  float s = 0.f;
  for (int c = 0; c < n; ++c) s += ch[c][i];
  return s / static_cast<float>(n);
}
}  // namespace

std::int64_t snapMarkerFrame(const float* const* channels, int numChannels, std::int64_t frames, std::int64_t frame, SnapMode mode, std::int64_t radiusFrames) {
  if (mode == SnapMode::Off || !channels || numChannels <= 0 || frames <= 1 || radiusFrames < 1) return frame;
  const std::int64_t lo = std::max<std::int64_t>(1, frame - radiusFrames);
  const std::int64_t hi = std::min<std::int64_t>(frames - 1, frame + radiusFrames);
  if (lo > hi) return frame;
  if (mode == SnapMode::ZeroCrossing) {
    std::int64_t best = frame;
    std::int64_t bestDist = -1;
    for (std::int64_t i = lo; i <= hi; ++i) {
      const float a = mono(channels, numChannels, i - 1), b = mono(channels, numChannels, i);
      const bool cross = (a <= 0.f && b > 0.f) || (a >= 0.f && b < 0.f);
      if (!cross) continue;
      const std::int64_t dist = i > frame ? i - frame : frame - i;
      if (bestDist < 0 || dist < bestDist) {  // ascending scan: on a tie the lower index wins
        best = i;
        bestDist = dist;
      }
    }
    return best;
  }
  // Transient: energy of the next 64 frames minus the energy of the previous 64, maximised.
  auto energy = [&](std::int64_t from, std::int64_t to) {
    double e = 0.0;
    const std::int64_t a = std::max<std::int64_t>(0, from), b = std::min<std::int64_t>(frames, to);
    for (std::int64_t i = a; i < b; ++i) {
      const double v = mono(channels, numChannels, i);
      e += v * v;
    }
    return e;
  };
  std::int64_t best = frame;
  double bestD = 0.0;
  std::int64_t bestDist = 0;
  bool found = false;
  for (std::int64_t i = lo; i <= hi; ++i) {
    const double d = energy(i, i + kEnergyWindow) - energy(i - kEnergyWindow, i);
    if (d <= 0.0) continue;
    const std::int64_t dist = i > frame ? i - frame : frame - i;
    if (!found || d > bestD || (d == bestD && (dist < bestDist || (dist == bestDist && i < best)))) {
      found = true;
      best = i;
      bestD = d;
      bestDist = dist;
    }
  }
  return found ? best : frame;
}

std::int64_t snapRadiusFrames(double framesPerPixel) {
  if (!std::isfinite(framesPerPixel) || framesPerPixel <= 0.0) return 1;
  return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(12.0 * framesPerPixel)), 1, 2048);
}

std::int64_t clampMarkerFrame(std::int64_t frame, std::int64_t prev, std::int64_t next, std::int64_t minRegion) {
  const std::int64_t lo = prev + minRegion, hi = next - minRegion;
  if (hi < lo) return std::clamp(frame, prev, std::max(prev, next));
  return std::clamp(frame, lo, hi);
}

Ticks snapTickToGrid(Ticks tick, Ticks gridTicks, bool enabled) {
  if (!enabled || gridTicks < 1 || tick < 0) return tick;
  return (tick + gridTicks / 2) / gridTicks * gridTicks;
}

LaneGeometry laneGeometry(double viewHeight, int lanes, double minLaneHeight, double scroll) {
  LaneGeometry g;
  g.viewHeight = std::max(0.0, viewHeight);
  g.lanes = std::max(0, lanes);
  if (g.lanes == 0) return g;
  const double fit = g.viewHeight / g.lanes;
  g.laneHeight = fit >= minLaneHeight ? fit : minLaneHeight;
  g.scroll = clampLaneScroll(g, scroll);
  return g;
}

double clampLaneScroll(const LaneGeometry& g, double scroll) {
  if (!std::isfinite(scroll)) return 0.0;
  return std::clamp(scroll, 0.0, g.maxScroll());
}

double laneTop(const LaneGeometry& g, int lane) { return lane * g.laneHeight - g.scroll; }

int laneAtScrolledY(const LaneGeometry& g, double y) {
  if (g.lanes <= 0 || g.laneHeight <= 0.0 || !std::isfinite(y)) return -1;
  return std::clamp(static_cast<int>(std::floor((y + g.scroll) / g.laneHeight)), 0, g.lanes - 1);
}

double scrollToRevealLane(const LaneGeometry& g, int lane) {
  if (g.lanes <= 0) return 0.0;
  lane = std::clamp(lane, 0, g.lanes - 1);
  const double top = lane * g.laneHeight, bottom = top + g.laneHeight;
  if (top < g.scroll) return clampLaneScroll(g, top);
  if (bottom > g.scroll + g.viewHeight) return clampLaneScroll(g, bottom - g.viewHeight);
  return g.scroll;
}

Thumb laneScrollThumb(const LaneGeometry& g) {
  if (!g.scrollable()) return {};
  const double h = std::max(16.0, g.viewHeight * g.viewHeight / g.contentHeight());
  const double travel = g.viewHeight - h;
  return {travel * (g.scroll / g.maxScroll()), h};
}

double scrollForThumbTop(const LaneGeometry& g, double thumbTop) {
  const Thumb t = laneScrollThumb(g);
  const double travel = g.viewHeight - t.height;
  if (t.height <= 0.0 || travel <= 0.0) return 0.0;
  return clampLaneScroll(g, std::clamp(thumbTop, 0.0, travel) / travel * g.maxScroll());
}

HitMove planHitMove(const HitMoveInput& in, const LaneGeometry& lanes) {
  HitMove m;
  m.lane = std::max(0, in.originalLane);
  if (in.barTicks <= 0 || in.barCount <= 0 || !(in.widthPx > 0.0)) return m;
  const Ticks total = in.barTicks * in.barCount;
  const Ticks pointerTick = xToTick(in.pointerX, in.widthPx, total);
  Ticks start = std::max<Ticks>(0, pointerTick - in.grabOffsetTicks);
  start = snapTickToGrid(start, in.gridTicks, in.snap);
  const Ticks last = std::max<Ticks>(0, total - std::min<Ticks>(std::max<Ticks>(in.gridTicks, kMinHitTicks), std::max<Ticks>(in.hitDuration, kMinHitTicks)));
  if (start > last) start = in.snap && in.gridTicks > 0 ? snapDown(last, in.gridTicks) : last;
  const BarPos pos = splitTick(start, in.barTicks, in.barCount);
  m.bar = pos.bar;
  m.startInBar = pos.startInBar;
  m.startAbs = static_cast<Ticks>(pos.bar) * in.barTicks + pos.startInBar;
  const int lane = laneAtScrolledY(lanes, in.pointerY);
  if (lane >= 0) m.lane = lane;
  return m;
}

Ticks planHitResize(Ticks barTicks, int barCount, Ticks gridTicks, bool snap, Ticks startInBar, int bar, double pointerX, double widthPx) {
  if (barTicks <= 0 || barCount <= 0 || !(widthPx > 0.0)) return kMinHitTicks;
  const Ticks total = barTicks * barCount;
  const Ticks absStart = static_cast<Ticks>(bar) * barTicks + startInBar;
  Ticks end = xToTick(pointerX, widthPx, total + 1);
  end = snapTickToGrid(end, gridTicks, snap);
  const Ticks room = barTicks - startInBar;
  const Ticks minDur = snap && gridTicks > 0 ? std::min(room, std::max(kMinHitTicks, gridTicks)) : std::min(room, kMinHitTicks);
  return std::clamp(end - absStart, minDur, std::max(minDur, room));
}

}  // namespace chopfractal::ui
