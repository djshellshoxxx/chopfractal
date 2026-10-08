#pragma once
// Orbit View model: the whole phrase as concentric rings (one per chop) with a sweeping playhead. Pure
// geometry; the editor only draws it. 12 o'clock is the pattern start and time runs clockwise.
// Nested (zoomed) hits are drawn as thinner arcs inside their parent. Threading: UI thread.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <cstdint>
#include <vector>

namespace chopfractal::ui {

struct OrbitArc {
  EventId id;
  EventId parent;            // invalid for top-level hits
  int ring = 0;              // chop index; ring 0 is the outermost
  int depth = 0;             // 0 = top level, 1+ = nested inside a zoomed hit
  double startAngle = 0.0;   // radians clockwise from 12 o'clock, [0, 2*pi)
  double sweep = 0.0;        // radians
  std::int64_t startTick = 0;     // absolute within the pattern
  std::int64_t durationTick = 0;
  float level = 1.f;
  bool locked = false;       // effective: the hit or any ancestor scope is locked
  bool hasChild = false;
  bool childActive = false;  // true: the hit's children play instead of the hit itself
  FilterType filter = FilterType::Off;  // effect markers for drawing
  bool glide = false;
  bool crunch = false;
};

struct OrbitView {
  std::int64_t lengthTicks = 0;
  int rings = 0;
  std::vector<double> barAngles;
  std::vector<double> beatAngles;
  std::vector<bool> barLocked;
  std::vector<OrbitArc> arcs;  // time order; parents precede their children
};

OrbitView buildOrbitView(const pattern::Pattern& p, const ChopSnapshot& chops);

// Radians in [0, 2*pi) for a position in quarter notes; wraps at the loop length; non-finite input gives 0.
double playheadAngle(double positionQuarters, std::int64_t lengthTicks);

// Glow for a hit under the playhead: 1 at the hit's start, falling linearly to 0 at its end, 0 outside it.
double hitPulse(const OrbitArc& arc, double positionTicks, std::int64_t lengthTicks);

struct Radii {
  double inner = 0.0;
  double outer = 0.0;
};
// Radial extent of an arc inside a view of outer radius `radius` (the centre hole takes 25%).
Radii arcRadii(const OrbitView& view, const OrbitArc& arc, double radius);

// Point is relative to the centre with y pointing down. Returns the innermost arc under it, or an
// invalid id. Zero-sweep or out-of-ring points never match.
EventId hitTest(const OrbitView& view, double x, double y, double radius);

}  // namespace chopfractal::ui
