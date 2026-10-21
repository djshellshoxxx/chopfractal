#pragma once
// Shared deterministic fixtures for the state tests (test_state_v2.cpp, test_state_sections.cpp).
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace statefix {

// Eight drum-like hits over two seconds (same signal as the integration tests).
inline std::shared_ptr<const chopfractal::render::SourceData> drumLoop() {
  auto d = std::make_shared<chopfractal::render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  chopfractal::Rng rng(3);
  for (int k = 0; k < 8; ++k) {
    const int kind = k % 4 == 0 ? 0 : (k % 4 == 2 ? 1 : 2);
    for (int i = 0; i < 5000; ++i) {
      const float t = static_cast<float>(i);
      float v = 0.f;
      if (kind == 0) v = std::sin(t * 0.0105f) * std::exp(-t / 1500.f);
      if (kind == 1) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-t / 900.f);
      if (kind == 2) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-t / 250.f) * 0.6f;
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = v * 0.8f;
    }
  }
  return d;
}

inline chopfractal::pattern::Settings patternSettings(std::uint64_t seed = 42, int bars = 1) {
  chopfractal::pattern::Settings s;
  s.bars = bars;
  s.density = 0.7;
  s.seed = seed;
  s.variation = {0.6, 0.6, 0.6, 0.7};
  return s;
}

// Source loaded, chops detected; no pattern yet.
inline bool loadAndDetect(chopfractal::composition::ProjectSession& s) {
  if (!s.loadSource(drumLoop(), "loop", "/music/loop.wav").ok()) return false;
  chopfractal::composition::DetectOptions d;
  d.analysis.sensitivity = 0.8;
  return s.applyChops(d).ok();
}

// The fixture session: source, chops, one generated bar with seed 42.
inline bool fixtureSession(chopfractal::composition::ProjectSession& s, int bars = 1) {
  return loadAndDetect(s) && s.generate(patternSettings(42, bars)).ok();
}

inline std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t h = 1469598103934665603ull;
  for (std::uint8_t b : bytes) {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}

}  // namespace statefix
