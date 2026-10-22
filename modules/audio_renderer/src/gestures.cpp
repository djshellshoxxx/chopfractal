#include <chopfractal/audio_renderer/gestures.hpp>

#include <algorithm>

namespace chopfractal::render {

std::int64_t stutterPeriodTicks(int rateIndex) {
  static constexpr std::int64_t kTicks[kStutterRates] = {480, 240, 120};
  return kTicks[std::min(kStutterRates - 1, std::max(0, rateIndex))];
}

bool gestureFromCc(int controller, int value, int frame, std::uint8_t rate, GestureEvent& out) {
  GestureKind kind;
  switch (controller) {
    case 20: kind = GestureKind::Stutter; break;
    case 21: kind = GestureKind::Reverse; break;
    case 22: kind = GestureKind::TapeStop; break;
    default: return false;
  }
  out.frame = frame;
  out.kind = kind;
  out.on = value >= 64;
  out.rate = rate;
  return true;
}

}  // namespace chopfractal::render
