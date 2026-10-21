// Measures a 1 kHz tone at two levels and computes the gain that brings the quieter one to the louder one.
#include <chopfractal/level_match/level_match.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  const int rate = 48000;
  std::vector<float> loud(rate * 3), quiet(rate * 3);
  for (int i = 0; i < rate * 3; ++i) {
    const float s = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * i / rate));
    loud[static_cast<std::size_t>(i)] = 0.5f * s;
    quiet[static_cast<std::size_t>(i)] = 0.2f * s;
  }
  const float* l[1] = {loud.data()};
  const float* q[1] = {quiet.data()};
  const auto a = chopfractal::levelmatch::measure(l, 1, rate * 3, rate);
  const auto b = chopfractal::levelmatch::measure(q, 1, rate * 3, rate);
  const auto m = chopfractal::levelmatch::computeMatch(a, b, 0.0, true, -1.0);
  std::printf("loud %.2f LUFS, quiet %.2f LUFS, gain %.1f dB\n", a.lufs, b.lufs, m.gainDb);
  return m.valid && std::fabs(m.gainDb - 8.0) < 0.2 ? 0 : 1;
}
