#pragma once
// Shared fixtures for the armed-switch tests. Only IEEE basic operations (no libm), so recorded hashes hold on every platform.
#include <chopfractal/audio_renderer/renderer.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace armedfx {
using namespace chopfractal;
using namespace chopfractal::render;

inline SourcePtr ramps(std::int64_t frames = 48000) {
  auto s = std::make_shared<SourceData>();
  s->channels = 1;
  s->sampleRate = 48000;
  s->frames = frames;
  s->samples.resize(static_cast<std::size_t>(frames));
  for (std::int64_t i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i % 4800) / 4800.f;
    s->samples[static_cast<std::size_t>(i)] = (t * 2.f - 1.f) * 0.5f + 0.25f * static_cast<float>((i / 4800) % 3);
  }
  return s;
}

inline FlatEvent evt(std::uint64_t id, std::int64_t regionBegin, std::int64_t regionEnd, Ticks start, Ticks dur) {
  FlatEvent e;
  e.id = EventId{id};
  e.chop = ChopId{1};
  e.region = {regionBegin, regionEnd};
  e.start = start;
  e.duration = dur;
  return e;
}

inline std::shared_ptr<const Playback> pbOf(const SourcePtr& src, const FlatEventList& ev, Ticks len) {
  auto r = makePlayback(src, ev, len);
  return r.ok() ? r.value() : nullptr;
}

inline void hashInto(std::uint64_t& h, const float* p, int n) {
  for (int i = 0; i < n; ++i) {
    std::uint32_t u;
    std::memcpy(&u, p + i, 4);
    for (int k = 0; k < 4; ++k) {
      h ^= (u >> (8 * k)) & 0xFFu;
      h *= 1099511628211ull;
    }
  }
}

// Recorded from the renderer before the armed switch existed: playing with ppq, a main-mailbox replacement, a seek, free-run, stop.
inline std::uint64_t noArmScenarioHash() {
  const SourcePtr src = ramps();
  FlatEventList a, b;
  for (std::uint64_t i = 0; i < 8; ++i) a.push_back(evt(i + 1, static_cast<std::int64_t>(i) * 3000, static_cast<std::int64_t>(i) * 3000 + 6000, static_cast<Ticks>(i * 480), 700));
  for (std::uint64_t i = 0; i < 6; ++i) b.push_back(evt(i + 1, static_cast<std::int64_t>(i) * 2500, static_cast<std::int64_t>(i) * 2500 + 5000, static_cast<Ticks>(i * 640 + 100), 900));
  const auto pa = pbOf(src, a, 3840);
  const auto pb = pbOf(src, b, 3840);
  Renderer r;
  Config cfg;
  cfg.maxBlock = 512;
  r.prepare(cfg);
  r.mailbox().publish(pa);
  const int blk = 333;
  std::vector<float> oL(blk), oR(blk), x(blk, 0.f);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  RenderParams p;
  std::uint64_t h = 1469598103934665603ull;
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  tb.bpm = 120.0;
  double ppq = 0.0;
  const double perBlock = blk * 120.0 / (60.0 * 48000.0);
  for (int i = 0; i < 80; ++i) {
    if (i == 20) r.mailbox().publish(pb);
    if (i == 40) ppq += 7.0;  // a seek
    if (i == 60) tb.positionValid = false;
    if (i == 70) tb.playing = false;
    tb.ppq = ppq;
    r.process(tb, p, in, out, 2, blk);
    hashInto(h, oL.data(), blk);
    hashInto(h, oR.data(), blk);
    ppq += perBlock;
  }
  return h;
}
}  // namespace armedfx
