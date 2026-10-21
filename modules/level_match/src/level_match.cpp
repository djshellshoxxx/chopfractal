#include <chopfractal/level_match/level_match.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chopfractal::levelmatch {
namespace {
constexpr double kPi = 3.14159265358979323846;

struct State {
  double z1 = 0, z2 = 0;
  double run(const Biquad& q, double x) {  // transposed direct form II
    const double y = q.b0 * x + z1;
    z1 = q.b1 * x - q.a1 * y + z2;
    z2 = q.b2 * x - q.a2 * y;
    return y;
  }
};
}  // namespace

Biquad kWeightShelf(double fs) {
  const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
  const double k = std::tan(kPi * f0 / fs);
  const double vh = std::pow(10.0, g / 20.0);
  const double vb = std::pow(vh, 0.4996667741545416);
  const double a0 = 1.0 + k / q + k * k;
  Biquad b;
  b.b0 = (vh + vb * k / q + k * k) / a0;
  b.b1 = 2.0 * (k * k - vh) / a0;
  b.b2 = (vh - vb * k / q + k * k) / a0;
  b.a1 = 2.0 * (k * k - 1.0) / a0;
  b.a2 = (1.0 - k / q + k * k) / a0;
  return b;
}

Biquad kWeightHighPass(double fs) {
  const double f0 = 38.13547087602444, q = 0.5003270373238773;
  const double k = std::tan(kPi * f0 / fs);
  const double a0 = 1.0 + k / q + k * k;
  Biquad b;
  b.b0 = 1.0;
  b.b1 = -2.0;
  b.b2 = 1.0;
  b.a1 = 2.0 * (k * k - 1.0) / a0;
  b.a2 = (1.0 - k / q + k * k) / a0;
  return b;
}

Loudness measure(const float* const* planar, int channels, std::int64_t frames, int sampleRate) {
  Loudness out;
  if (!planar || channels < 1 || channels > 2 || frames <= 0 || sampleRate < 8000) return out;
  for (int c = 0; c < channels; ++c)
    if (!planar[c]) return out;
  const int nch = 2;  // mono is expanded to two identical channels
  for (int c = 0; c < channels; ++c)
    for (std::int64_t i = 0; i < frames; ++i) {
      const float x = planar[c][i];
      if (std::isfinite(x)) out.samplePeak = std::max(out.samplePeak, static_cast<double>(std::fabs(x)));
    }
  if (static_cast<double>(frames) / sampleRate < kMinMeasureSeconds) return out;

  const Biquad shelf = kWeightShelf(sampleRate), hp = kWeightHighPass(sampleRate);
  const std::int64_t hop = std::max<std::int64_t>(1, sampleRate / 10);  // 100 ms; a block is four hops
  const std::int64_t hops = (frames + hop - 1) / hop;
  std::vector<double> hopEnergy(static_cast<std::size_t>(hops), 0.0);  // sum over channels of the sum of squares in the hop
  for (int c = 0; c < nch; ++c) {
    const float* src = planar[channels == 1 ? 0 : c];
    State s1, s2;
    for (std::int64_t i = 0; i < frames; ++i) {
      const float xf = src[i];
      const double x = std::isfinite(xf) ? static_cast<double>(xf) : 0.0;
      const double y = s2.run(hp, s1.run(shelf, x));
      hopEnergy[static_cast<std::size_t>(i / hop)] += y * y;
    }
  }
  const double blockFrames = static_cast<double>(4 * hop);
  std::vector<double> blocks;  // mean-square power per block (channel weights are 1.0)
  if (frames < 4 * hop) {
    double e = 0;
    for (double h : hopEnergy) e += h;
    blocks.push_back(e / blockFrames);
  } else {
    const std::int64_t fullHops = frames / hop;  // hops completely filled
    for (std::int64_t b = 0; b + 4 <= fullHops; ++b) {
      const double e = hopEnergy[static_cast<std::size_t>(b)] + hopEnergy[static_cast<std::size_t>(b + 1)] + hopEnergy[static_cast<std::size_t>(b + 2)] + hopEnergy[static_cast<std::size_t>(b + 3)];
      blocks.push_back(e / blockFrames);
    }
  }
  auto lufs = [](double power) { return -0.691 + 10.0 * std::log10(power); };
  const double absGate = std::pow(10.0, (kSilenceLufs + 0.691) / 10.0);
  double sum = 0;
  std::int64_t n = 0;
  for (double p : blocks)
    if (p > absGate) {
      sum += p;
      ++n;
    }
  if (n == 0) return out;
  double gated = sum / static_cast<double>(n);
  if (frames >= 4 * hop) {  // the relative gate needs more than one block
    const double relGate = std::pow(10.0, (lufs(gated) - 10.0 + 0.691) / 10.0);
    sum = 0;
    n = 0;
    for (double p : blocks)
      if (p > absGate && p > relGate) {
        sum += p;
        ++n;
      }
    if (n > 0) gated = sum / static_cast<double>(n);
  }
  out.lufs = lufs(gated);
  out.valid = true;
  return out;
}

MatchResult computeMatch(const Loudness& source, const Loudness& pattern, double trimDb, bool limiterOn, double ceilingDb) {
  (void)ceilingDb;  // with the limiter on the +/-12 dB clamp is the only cap; the ceiling belongs to the limiter
  MatchResult r;
  if (!source.valid || !pattern.valid || !std::isfinite(trimDb)) return r;
  r.valid = true;
  r.requestedDb = source.lufs - pattern.lufs + trimDb;
  double g = r.requestedDb;
  if (g > kMaxBoostDb) {
    g = kMaxBoostDb;
    r.clamped = true;
  } else if (g < -kMaxCutDb) {
    g = -kMaxCutDb;
    r.clamped = true;
  }
  g = std::round(g * 10.0) / 10.0;
  if (!limiterOn && pattern.samplePeak > 0.0) {
    const double cap = -0.1 - 20.0 * std::log10(pattern.samplePeak);  // keeps the pattern peak at or below -0.1 dBFS
    if (g > cap) {
      g = std::floor(cap * 10.0) / 10.0;  // round down so the rounding never eats the headroom
      g = std::max(g, -kMaxCutDb);
      r.headroomLimited = true;
    }
  }
  r.gainDb = g;
  return r;
}

}  // namespace chopfractal::levelmatch
