// Chaos macro: one 0..1 control that pushes generation settings toward more variation.
// Pure and deterministic (no randomness of its own); chaos <= 0 returns the input untouched, so every
// project that never touches the control generates exactly as before.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::composition {
namespace {
double lerpToward(double v, double target, double t) { return v + (target - v) * t; }
}  // namespace

pattern::Settings applyChaos(const pattern::Settings& base, double chaos) {
  if (!std::isfinite(chaos) || chaos <= 0.0) return base;
  const double c = std::min(1.0, chaos);
  pattern::Settings s = base;
  // Variation at every level moves 80% of the way to "always new" at full chaos.
  s.variation.phrase = lerpToward(base.variation.phrase, 1.0, 0.8 * c);
  s.variation.bar = lerpToward(base.variation.bar, 1.0, 0.8 * c);
  s.variation.beat = lerpToward(base.variation.beat, 1.0, 0.8 * c);
  s.variation.event = lerpToward(base.variation.event, 1.0, 0.8 * c);
  // Density rises toward 0.6 (never lowered).
  s.density = lerpToward(base.density, std::max(base.density, 0.6), c);
  // Transforms and effects are switched on as chaos rises; they are never switched off.
  if (c >= 0.35) s.allowReverse = true;
  if (c >= 0.40) s.allowFilter = true;
  if (c >= 0.50) s.allowPitch = true;
  if (c >= 0.60) s.allowGlide = true;
  if (c >= 0.65) s.allowRetrigger = true;
  if (c >= 0.75) s.allowCrunch = true;
  s.fxIntensity = lerpToward(base.fxIntensity, 1.0, 0.7 * c);
  s.pitchRange = std::min(24, std::max(base.pitchRange, static_cast<int>(std::lround(12.0 * c))));
  s.maxRetrigger = std::min(limits::kMaxRetrigger, std::max(base.maxRetrigger, 2 + static_cast<int>(std::lround(6.0 * c))));
  s.maxShiftTicks = std::max(base.maxShiftTicks, static_cast<Ticks>(std::floor(static_cast<double>(gridTicks(base.grid) / 8) * c)));
  return s;
}

}  // namespace chopfractal::composition
