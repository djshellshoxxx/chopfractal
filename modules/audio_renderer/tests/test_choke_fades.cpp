// Engine v2 renderer hints: choke groups and smoothstep fades. Legacy events (hints zero) must render as before.
#include <chopfractal/audio_renderer/renderer.hpp>

#include <cmath>
#include <cstdint>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {
SourcePtr dc(std::int64_t frames = 48000) {
  auto s = std::make_shared<SourceData>();
  s->channels = 1;
  s->sampleRate = 48000;
  s->frames = frames;
  s->samples.assign(static_cast<std::size_t>(frames), 1.f);
  return s;
}

FlatEvent hit(Ticks start, Ticks dur, std::uint8_t group, bool smooth, std::uint32_t fadeIn = 0) {
  FlatEvent e;
  e.id = EventId{1};
  e.chop = ChopId{1};
  e.region = SampleRange{0, 40000};
  e.start = start;
  e.duration = dur;
  e.tx.level = 1.f;
  e.chokeGroup = group;
  e.smoothFades = smooth;
  e.fadeInFrames = fadeIn;
  return e;
}

// 120 bpm at 48 kHz: 25 frames per tick, 24000 frames per beat of 960 ticks.
std::vector<float> run(const FlatEventList& ev, int maxVoices = 8) {
  auto pb = makePlayback(dc(), ev, 3840);
  OfflineSettings s;
  s.sampleRate = 48000.0;
  s.bpm = 120.0;
  s.cycles = 1;
  s.tailSeconds = 0.0;
  (void)maxVoices;
  auto r = renderOffline(pb.value(), s);
  return r.value()[0];
}
}  // namespace

CHOP_TEST(a_second_kick_chokes_the_first_but_other_groups_ring_on) {
  // Two overlapping long hits: 1 s each, the second starts 0.25 s in (6000 frames = 240 ticks).
  const std::size_t probe = 15000;
  auto layered = run({hit(0, 960, 0, false), hit(240, 960, 0, false)});
  CHECK_NEAR(layered[probe], 2.0, 1e-4);
  auto choked = run({hit(0, 960, 1, true), hit(240, 960, 1, true)});
  CHECK_NEAR(choked[probe], 1.0, 1e-4);
  CHECK(choked[7000] < 2.0f);                   // already ducking within the fade after the second onset
  auto mixed = run({hit(0, 960, 1, true), hit(240, 960, 2, true)});
  CHECK_NEAR(mixed[probe], 2.0, 1e-4);          // kick and snare do not choke each other
  auto snares = run({hit(0, 960, 2, false), hit(240, 960, 2, false)});
  CHECK_NEAR(snares[probe], 1.0, 1e-4);
}

CHOP_TEST(a_closed_hat_chokes_open_hats_but_not_the_reverse) {
  const std::size_t probe = 15000;
  CHECK_NEAR(run({hit(0, 960, 4, true), hit(240, 960, 3, true)})[probe], 1.0, 1e-4);
  CHECK_NEAR(run({hit(0, 960, 3, true), hit(240, 960, 4, true)})[probe], 2.0, 1e-4);
  CHECK_NEAR(run({hit(0, 960, 4, true), hit(240, 960, 4, true)})[probe], 1.0, 1e-4);
}

CHOP_TEST(a_choke_fades_without_a_step) {
  auto out = run({hit(0, 960, 1, true), hit(240, 960, 1, true)});
  float worst = 0.f;
  for (std::size_t i = 6001; i < 6400; ++i) worst = std::max(worst, std::fabs(out[i] - out[i - 1]));
  CHECK(worst < 0.1f);  // a few dozen frames of ramp, not an instant cut
}

CHOP_TEST(smooth_fades_use_a_smoothstep_and_legacy_stays_linear) {
  const auto lin = run({hit(0, 960, 0, false, 100)});
  const auto smooth = run({hit(0, 960, 0, true, 100)});
  CHECK_NEAR(lin[25], 0.25, 1e-4);
  CHECK_NEAR(smooth[25], 0.15625, 1e-4);
  CHECK_NEAR(lin[50], 0.5, 1e-4);
  CHECK_NEAR(smooth[50], 0.5, 1e-4);
  CHECK_NEAR(smooth[200], 1.0, 1e-5);
}

CHOP_TEST(events_without_hints_render_exactly_as_before) {
  std::uint64_t h = 0xCBF29CE484222325ull;
  const auto out = run({hit(0, 480, 0, false), hit(480, 480, 0, false), hit(960, 480, 0, false, 40)});
  for (float v : out) {
    std::uint32_t b;
    static_assert(sizeof b == sizeof v);
    __builtin_memcpy(&b, &v, sizeof b);
    h = (h ^ b) * 0x100000001B3ull;
  }
  CHECK_EQ(h, 9673844696356860708ull);  // recorded with the renderer before the v2 hints existed
}
