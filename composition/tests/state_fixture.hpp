#pragma once
// Shared deterministic fixtures for the state tests (test_state_v2.cpp, test_state_sections.cpp).
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
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

// Byte offset of the extras section inside the "composition" payload: flag(1) + evolve marker(1) + evolve block(36).
constexpr std::size_t kExtrasOffset = 38;

// Decodes the container, lets `fn` rewrite the "composition" payload bytes, and encodes again (valid checksum).
template <class F>
std::vector<std::uint8_t> patchComposition(const std::vector<std::uint8_t>& state, F&& fn, int schema = -1) {
  auto dec = chopfractal::codec::decode(state.data(), state.size());
  if (!dec.ok()) return {};
  auto& m = dec.value().modules["composition"];
  fn(m.bytes);
  if (schema >= 0) m.schemaVersion = static_cast<std::uint32_t>(schema);
  auto enc = chopfractal::codec::encode(dec.value());
  return enc.ok() ? enc.value() : std::vector<std::uint8_t>{};
}

inline std::vector<std::uint8_t> compositionPayload(const std::vector<std::uint8_t>& state) {
  auto dec = chopfractal::codec::decode(state.data(), state.size());
  if (!dec.ok()) return {};
  return dec.value().modules["composition"].bytes;
}

// A framed extras section: marker, count, then the given (tag, payload) blocks in the order given (no sorting, no checks).
inline std::vector<std::uint8_t> rawExtras(const std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>>& blocks, std::uint16_t count = 0xFFFF) {
  std::vector<std::uint8_t> out{0xE2};
  const std::uint16_t n = count == 0xFFFF ? static_cast<std::uint16_t>(blocks.size()) : count;
  out.push_back(static_cast<std::uint8_t>(n & 0xFF));
  out.push_back(static_cast<std::uint8_t>(n >> 8));
  for (const auto& b : blocks) {
    out.push_back(b.first);
    const std::uint32_t len = static_cast<std::uint32_t>(b.second.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
    out.insert(out.end(), b.second.begin(), b.second.end());
  }
  return out;
}

// v1 bytes of the composition payload of a session saved without extras (first 38 bytes).
inline std::vector<std::uint8_t> v1Head(const std::vector<std::uint8_t>& state) {
  auto p = compositionPayload(state);
  p.resize(std::min(p.size(), kExtrasOffset));
  return p;
}

}  // namespace statefix
