#include <chopfractal/plugin_ui_adapter/hit_edit.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::ui {

Ticks xToTick(double x, double width, Ticks lengthTicks) {
  if (!std::isfinite(x) || !(width > 0.0) || lengthTicks <= 0) return 0;
  const double t = std::floor(x / width * static_cast<double>(lengthTicks));
  if (t <= 0.0) return 0;
  if (t >= static_cast<double>(lengthTicks - 1)) return lengthTicks - 1;
  return static_cast<Ticks>(t);
}

Ticks snapDown(Ticks tick, Ticks grid) {
  if (grid <= 0 || tick < 0) return tick;
  return tick / grid * grid;
}

Ticks snapNearest(Ticks tick, Ticks grid) {
  if (grid <= 0 || tick < 0) return tick;
  return (tick + grid / 2) / grid * grid;
}

BarPos splitTick(Ticks absolute, Ticks barTicks, int barCount) {
  if (barTicks <= 0 || barCount <= 0) return {};
  const Ticks a = std::max<Ticks>(0, absolute);
  const Ticks bar = std::min<Ticks>(barCount - 1, a / barTicks);
  return {static_cast<int>(bar), std::min<Ticks>(barTicks - 1, a - bar * barTicks)};
}

int laneAtY(double y, double height, int lanes) {
  if (lanes <= 0 || !std::isfinite(y) || !(height > 0.0)) return -1;
  const int lane = static_cast<int>(std::floor(y / height * lanes));
  return std::max(0, std::min(lanes - 1, lane));
}

}  // namespace chopfractal::ui
