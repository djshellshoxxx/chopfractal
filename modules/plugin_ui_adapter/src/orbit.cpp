#include <chopfractal/plugin_ui_adapter/orbit.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::ui {
namespace {

constexpr double kTwoPi = 6.28318530717958647692;

double angleOf(std::int64_t tick, std::int64_t length) { return kTwoPi * static_cast<double>(tick) / static_cast<double>(length); }

void addArcs(OrbitView& v, const std::vector<Event>& events, const ChopSnapshot& chops, std::int64_t base, int depth, EventId parent, bool lockedAbove) {
  for (const Event& e : events) {
    OrbitArc a;
    a.id = e.id;
    a.parent = parent;
    a.depth = depth;
    a.startTick = base + e.start;
    a.durationTick = e.duration;
    a.startAngle = angleOf(a.startTick, v.lengthTicks);
    a.sweep = angleOf(e.duration, v.lengthTicks);
    for (std::size_t i = 0; i < chops.chops.size(); ++i)
      if (chops.chops[i].id == e.chop) a.ring = static_cast<int>(i);
    a.level = e.tx.level;
    a.locked = lockedAbove || e.locked;
    a.hasChild = e.child != nullptr;
    a.childActive = e.child != nullptr && e.childActive;
    a.filter = e.fx.filter;
    a.glide = e.fx.glideSemitones != 0.f;
    a.crunch = e.fx.crush > 0.f;
    v.arcs.push_back(a);
    if (e.child) addArcs(v, e.child->events, chops, a.startTick, depth + 1, e.id, a.locked || e.child->locked);
  }
}

}  // namespace

OrbitView buildOrbitView(const pattern::Pattern& p, const ChopSnapshot& chops) {
  OrbitView v;
  v.lengthTicks = pattern::lengthTicks(p);
  v.rings = static_cast<int>(chops.chops.size());
  if (v.lengthTicks <= 0) return v;
  const std::int64_t barT = pattern::barTicks(p.settings), beatT = ticksPerBeat(p.settings.timeSignature);
  for (std::size_t b = 0; b < p.bars.size(); ++b) {
    const std::int64_t base = static_cast<std::int64_t>(b) * barT;
    v.barAngles.push_back(angleOf(base, v.lengthTicks));
    v.barLocked.push_back(p.phraseLocked || p.bars[b].locked);
    for (std::size_t t = 0; t < p.bars[b].beats.size(); ++t) {
      v.beatAngles.push_back(angleOf(base + static_cast<std::int64_t>(t) * beatT, v.lengthTicks));
      const auto& beat = p.bars[b].beats[t];
      addArcs(v, beat.events, chops, base, 0, EventId{}, p.phraseLocked || p.bars[b].locked || beat.locked);
    }
  }
  std::stable_sort(v.arcs.begin(), v.arcs.end(), [](const OrbitArc& a, const OrbitArc& b) {
    if (a.startTick != b.startTick) return a.startTick < b.startTick;
    return a.depth < b.depth;
  });
  return v;
}

double playheadAngle(double positionQuarters, std::int64_t lengthTicks) {
  if (!std::isfinite(positionQuarters) || lengthTicks <= 0) return 0.0;
  const double ticks = positionQuarters * static_cast<double>(kTicksPerQuarter);
  double wrapped = std::fmod(ticks, static_cast<double>(lengthTicks));
  if (wrapped < 0) wrapped += static_cast<double>(lengthTicks);
  const double a = kTwoPi * wrapped / static_cast<double>(lengthTicks);
  return a >= kTwoPi ? 0.0 : a;
}

double hitPulse(const OrbitArc& arc, double positionTicks, std::int64_t lengthTicks) {
  if (!std::isfinite(positionTicks) || lengthTicks <= 0 || arc.durationTick <= 0) return 0.0;
  double pos = std::fmod(positionTicks, static_cast<double>(lengthTicks));
  if (pos < 0) pos += static_cast<double>(lengthTicks);
  const double rel = pos - static_cast<double>(arc.startTick);
  if (rel < 0.0 || rel >= static_cast<double>(arc.durationTick)) return 0.0;
  return 1.0 - rel / static_cast<double>(arc.durationTick);
}

Radii arcRadii(const OrbitView& view, const OrbitArc& arc, double radius) {
  const int rings = std::max(1, view.rings);
  const double hole = 0.25 * radius, w = (radius - hole) / rings;
  const double outer = radius - arc.ring * w, inner = outer - w;
  const double inset = w * 0.15 * std::min(arc.depth, 3);
  return {inner + inset, outer - inset};
}

EventId hitTest(const OrbitView& view, double x, double y, double radius) {
  if (!std::isfinite(x) || !std::isfinite(y) || !(radius > 0.0) || view.lengthTicks <= 0) return EventId{};
  const double r = std::hypot(x, y);
  if (r > radius || r < 0.25 * radius) return EventId{};
  double ang = std::atan2(x, -y);  // 0 at 12 o'clock, clockwise positive
  if (ang < 0) ang += kTwoPi;
  const OrbitArc* best = nullptr;
  for (const OrbitArc& a : view.arcs) {
    if (a.sweep <= 0.0) continue;
    double rel = ang - a.startAngle;
    if (rel < 0) rel += kTwoPi;
    if (rel >= a.sweep) continue;
    const Radii rr = arcRadii(view, a, radius);
    if (r < rr.inner || r > rr.outer) continue;
    if (!best || a.depth >= best->depth) best = &a;
  }
  return best ? best->id : EventId{};
}

}  // namespace chopfractal::ui
