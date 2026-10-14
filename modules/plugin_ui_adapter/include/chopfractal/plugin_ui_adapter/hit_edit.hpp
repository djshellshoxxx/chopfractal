#pragma once
// Pure geometry for direct hit editing in the pattern grid (no JUCE): pixel <-> tick conversion, grid
// snapping, splitting an absolute tick into bar + position, and lane lookup. The editor routes the results
// into pattern::moveEvent / resizeEvent / addEvent / duplicateEvent; nothing here mutates project state.
#include <chopfractal/chop_contracts/time.hpp>

#include <cstdint>

namespace chopfractal::ui {

struct BarPos {
  int bar = 0;
  Ticks startInBar = 0;
};

// x measured from the left edge of an area `width` pixels wide that shows `lengthTicks`. Clamped to
// [0, lengthTicks - 1]; non-finite input and non-positive sizes give 0.
Ticks xToTick(double x, double width, Ticks lengthTicks);
// `grid` must be > 0; otherwise the tick is returned unchanged.
Ticks snapDown(Ticks tick, Ticks grid);
Ticks snapNearest(Ticks tick, Ticks grid);
// Splits an absolute tick; the bar index is clamped to [0, barCount - 1].
BarPos splitTick(Ticks absolute, Ticks barTicks, int barCount);
// Lane 0 at the top, clamped to [0, lanes - 1]; returns -1 when there are no lanes or the input is not finite.
int laneAtY(double y, double height, int lanes);

}  // namespace chopfractal::ui
