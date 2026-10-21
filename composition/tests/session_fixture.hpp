// Shared helpers for the session-level feature tests (F13, F10, F09, F18): a synthetic drum loop, a ready session,
// temp directories, file reading and an FNV-1a hash. Header-only so each test file stays self-contained.
#pragma once
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "chop_test.hpp"

namespace fixture {
using namespace chopfractal;
using namespace chopfractal::composition;

// Transcendentals go through double and round once to float so the source hash is the same on every libm (see state_fixture.hpp).
inline float dsin(float x) { return static_cast<float>(std::sin(static_cast<double>(x))); }
inline float dexp(float x) { return static_cast<float>(std::exp(static_cast<double>(x))); }

inline std::shared_ptr<const render::SourceData> loopAudio() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k) {
    const int kind = k % 4 == 0 ? 0 : (k % 4 == 2 ? 1 : 2);
    for (int i = 0; i < 5000; ++i) {
      const float t = static_cast<float>(i);
      float v = 0.f;
      if (kind == 0) v = dsin(t * 0.0105f) * dexp(-t / 1500.f);
      if (kind == 1) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * dexp(-t / 900.f);
      if (kind == 2) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * dexp(-t / 250.f) * 0.6f;
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = v * 0.8f;
    }
  }
  return d;
}

inline pattern::Settings settings(std::uint64_t seed = 42, int bars = 2) {
  pattern::Settings s;
  s.bars = bars;
  s.density = 0.7;
  s.seed = seed;
  s.variation = {0.6, 0.6, 0.6, 0.7};
  return s;
}

// Loads the loop, detects chops and (optionally) generates a pattern.
inline bool ready(ProjectSession& s, bool generate = true, std::uint64_t seed = 42, int bars = 2) {
  CHECK(s.loadSource(loopAudio(), "loop", "/music/loop.wav").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  if (generate) CHECK(s.generate(settings(seed, bars)).ok());
  return s.hasSource() && (!generate || s.pattern() != nullptr);
}

inline std::string tmpDir(const char* group, const char* name) {
  auto p = std::filesystem::temp_directory_path() / group / name;
  std::filesystem::remove_all(p);
  std::filesystem::create_directories(p);
  return p.string();
}

inline std::vector<std::uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

inline std::uint64_t fnv1a(const std::vector<std::uint8_t>& v) {
  std::uint64_t h = 1469598103934665603ull;
  for (std::uint8_t b : v) {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}
}  // namespace fixture
