#pragma once
// Specified, platform-independent pseudo-random generator (SplitMix64). Output depends only on the
// seed and call order, never on the standard library. Floating point use is limited to exact
// operations (53-bit integer to double, multiplication by a power of two).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chopfractal {

constexpr std::uint64_t mix64(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

constexpr std::uint64_t hashCombine(std::uint64_t a, std::uint64_t b) {
  return mix64(a ^ (mix64(b) + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2)));
}

// Derive an independent stream key for a scope so that changing one scope never shifts the draws
// seen by another ("re-rolling" one bar must not re-roll unrelated sections).
constexpr std::uint64_t deriveKey(std::uint64_t seed, std::uint64_t a, std::uint64_t b = 0, std::uint64_t c = 0) {
  return hashCombine(hashCombine(hashCombine(seed, a), b), c);
}

class Rng {
 public:
  explicit constexpr Rng(std::uint64_t seed = 0) : state_(seed) {}

  constexpr std::uint64_t next() {
    state_ += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  // Uniform in [0, 1) with 53 bits of precision.
  double uniform01() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

  // Uniform integer in [0, n). Modulo bias is below 2^-40 for n <= 2^24 and is part of the spec.
  std::uint64_t uniform(std::uint64_t n) {
    const std::uint64_t v = next();
    return n == 0 ? 0 : v % n;
  }

  // Always consumes exactly one draw, regardless of weights.
  std::size_t weightedPick(const std::vector<double>& weights) {
    const double u = uniform01();
    double sum = 0.0;
    for (double w : weights) sum += (w > 0.0 ? w : 0.0);
    if (!(sum > 0.0)) return 0;
    const double target = u * sum;
    double acc = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
      acc += (weights[i] > 0.0 ? weights[i] : 0.0);
      if (target < acc) return i;
    }
    return weights.size() - 1;
  }

 private:
  std::uint64_t state_;
};

}  // namespace chopfractal
