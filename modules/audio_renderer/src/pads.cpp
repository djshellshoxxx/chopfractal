#include <chopfractal/audio_renderer/pads.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::render {

float velocityGain(VelocityCurve curve, int velocity) {
  const int v = std::min(127, std::max(1, velocity));
  const double x = static_cast<double>(v) / 127.0;
  switch (curve) {
    case VelocityCurve::Linear: return static_cast<float>(x);
    case VelocityCurve::Soft: return static_cast<float>(std::sqrt(x));
    case VelocityCurve::Hard: return static_cast<float>(x * x);
    case VelocityCurve::Fixed: return 1.f;
  }
  return 1.f;
}

}  // namespace chopfractal::render
