// Minimal consumer: stretch a 1 s tone (96 BPM loop) to 120 BPM with one anchored chop boundary.
#include <chopfractal/time_stretch/stretch.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::stretch;
  constexpr int sr = 48000;
  std::vector<float> tone(sr);
  for (int i = 0; i < sr; ++i) tone[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(6.283185307179586 * 220.0 * i / sr));

  const double ratio = lengthRatio(96.0, 120.0);  // 0.8: the loop gets shorter
  if (!checkRatio(ratio).ok()) return 1;
  const std::int64_t outFrames = static_cast<std::int64_t>(std::llround(sr * ratio));
  auto plan = makePlan(sr, outFrames, {sr / 2});  // a chop boundary at 0.5 s stays exactly on its scaled position
  if (!plan.ok()) return 1;

  const float* channels[1] = {tone.data()};
  auto out = stretch(Input{channels, 1, sr, sr}, plan.value(), Quality::Standard);
  if (!out.ok() || static_cast<std::int64_t>(out.value().size()) != outFrames) return 1;
  std::printf("%lld -> %lld frames, boundary at %lld\n", static_cast<long long>(sr), static_cast<long long>(outFrames),
              static_cast<long long>(plan.value().outBounds[1]));
  return 0;
}
