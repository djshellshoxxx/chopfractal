// Bit-exact regression of the v1 render path. The hashes were recorded from the renderer before the
// slice-quality, limiter and gesture-effect work (RND); with every new feature off the output must not move.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

std::uint64_t fnv(const std::vector<std::vector<float>>& planar) {
  std::uint64_t h = 1469598103934665603ull;
  for (const auto& ch : planar)
    for (float f : ch) {
      std::uint32_t u;
      std::memcpy(&u, &f, 4);
      for (int i = 0; i < 4; ++i) {
        h ^= (u >> (8 * i)) & 0xFFu;
        h *= 1099511628211ull;
      }
    }
  return h;
}

// A deterministic synthetic drum loop: kick (decaying sine sweep), snare (noise + tone), hat (hp noise), one
// sustained pad; 2 bars at 120 BPM would be 4 s, the loop here is 2 s at 48 kHz, stereo.
SourcePtr drumSource() {
  auto s = std::make_shared<SourceData>();
  s->channels = 2;
  s->sampleRate = 48000;
  s->frames = 96000;
  s->samples.assign(static_cast<std::size_t>(2 * s->frames), 0.f);
  Rng rng(7);
  // Only IEEE basic operations (no libm), so the recorded hashes hold on every platform and compiler.
  auto put = [&](std::int64_t at, std::int64_t len, int kind) {
    float lp = 0.f;
    float y1 = 0.f, y2 = 0.f, env = 1.f;
    const float c1 = kind == 0 ? 1.99f : 1.93f;  // 2 cos(w) of a slow (kick) or mid (snare tone) resonator
    const float decay = kind == 0 ? 0.99978f : (kind == 1 ? 0.9996f : 0.9988f);
    y1 = 0.1f;
    for (std::int64_t i = 0; i < len && at + i < s->frames; ++i) {
      const float n = static_cast<float>(rng.uniform01() * 2.0 - 1.0);
      const float y = c1 * y1 - y2;
      y2 = y1;
      y1 = y;
      env *= decay;
      float x = 0.f;
      if (kind == 0) x = 6.f * y * env;
      if (kind == 1) x = 0.5f * n * env + 2.f * y * env;
      if (kind == 2) {
        lp += 0.2f * (n - lp);
        x = 0.35f * (n - lp) * env;
      }
      s->samples[static_cast<std::size_t>(at + i)] = x;
      s->samples[static_cast<std::size_t>(s->frames + at + i)] = x * 0.9f;
    }
  };
  put(0, 9000, 0);
  put(12000, 12000, 1);
  put(24000, 3000, 2);
  put(36000, 9000, 0);
  put(48000, 12000, 1);
  put(60000, 3000, 2);
  put(72000, 3000, 2);
  put(84000, 3000, 2);
  return s;
}

FlatEventList drumEvents(bool fx) {
  FlatEventList l;
  auto add = [&](std::uint64_t id, std::int64_t a, std::int64_t b, Ticks start, Ticks dur) -> FlatEvent& {
    FlatEvent e;
    e.id = EventId{id};
    e.chop = ChopId{id};
    e.region = {a, b};
    e.start = start;
    e.duration = dur;
    l.push_back(e);
    return l.back();
  };
  add(1, 0, 9000, 0, 480);
  add(2, 12000, 24000, 960, 480);
  add(3, 24000, 27000, 1440, 240);
  add(4, 36000, 45000, 1920, 480);
  add(5, 48000, 60000, 2880, 480);
  FlatEvent& r = add(6, 60000, 63000, 3360, 480);
  r.tx.retrigger = 4;
  FlatEvent& rv = add(7, 48000, 60000, 3600, 240);
  rv.tx.reverse = true;
  if (fx) {
    l[0].fx.filter = FilterType::LowPass;
    l[0].fx.cutoff = 0.5f;
    l[0].fx.resonance = 0.4f;
    l[1].fx.glideSemitones = -7.f;
    l[2].fx.crush = 0.6f;
  }
  return l;
}

std::shared_ptr<const Playback> build(bool fx) {
  auto r = makePlayback(drumSource(), drumEvents(fx), 3840);
  CHECK(r.ok());
  return r.ok() ? r.value() : nullptr;
}

std::uint64_t renderHash(bool fx, int block, double sr, const RenderParams& p) {
  OfflineSettings s;
  s.blockSize = block;
  s.cycles = 2;
  s.tailSeconds = 0.3;
  s.sampleRate = sr;
  s.params = p;
  auto r = renderOffline(build(fx), s);
  CHECK(r.ok());
  return r.ok() ? fnv(r.value()) : 0;
}

}  // namespace

CHOP_TEST(v1_render_is_bit_exact_with_every_new_feature_off) {
  RenderParams p;
  const std::uint64_t a = renderHash(false, 512, 48000.0, p);
  const std::uint64_t b = renderHash(false, 333, 44100.0, p);
  CHECK_EQ(a, 8752441730684038491ull);
  CHECK_EQ(b, 12867116372974070439ull);
}
