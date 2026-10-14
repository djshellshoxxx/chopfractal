#include <chopfractal/audio_renderer/pads.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::render {

float velocityGain(VelocityCurve curve, int velocity) {
  const float x = static_cast<float>(std::clamp(velocity, 1, 127)) / 127.f;
  switch (curve) {
    case VelocityCurve::Linear: return x;
    case VelocityCurve::Soft: return std::sqrt(x);
    case VelocityCurve::Hard: return x * x;
    case VelocityCurve::Fixed: return 1.f;
  }
  return x;
}

}  // namespace chopfractal::render
