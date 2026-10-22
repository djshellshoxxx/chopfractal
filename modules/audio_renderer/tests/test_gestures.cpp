// Live gestures (F08): Stutter, Reverse and Tape Stop applied on exact frames on top of the running pattern.
#include <chopfractal/audio_renderer/gestures.hpp>
#include <chopfractal/audio_renderer/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

constexpr int kRate = 48000;

SourcePtr source(bool ramp, std::int64_t frames = 96000) {
  auto s = std::make_shared<SourceData>();
  s->channels = 1;
  s->sampleRate = kRate;
  s->frames = frames;
  s->samples.assign(static_cast<std::size_t>(frames), 1.f);
  if (ramp)
    for (std::int64_t i = 0; i < frames; ++i) s->samples[static_cast<std::size_t>(i)] = 0.1f + 0.8f * static_cast<float>(i) / static_cast<float>(frames);
  return s;
}

GestureEvent gest(int frame, GestureKind k, bool on, int rate = 1) {
  GestureEvent g;
  g.frame = frame;
  g.kind = k;
  g.on = on;
  g.rate = static_cast<std::uint8_t>(rate);
  return g;
}

struct Rig {
  Renderer r;
  double ppq = 0.0;
  explicit Rig(SourcePtr src, std::int64_t region = 90000) {
    Config cfg;
    cfg.sampleRate = kRate;
    cfg.maxBlock = 4096;
    cfg.maxVoices = 8;
    r.prepare(cfg);
    FlatEvent e;
    e.id = EventId{1};
    e.chop = ChopId{1};
    e.region = {0, region};
    e.start = 0;
    e.duration = 3840;
    auto pb = makePlayback(src, {e}, 3840);
    CHECK(pb.ok());
    r.mailbox().publish(pb.value());
  }
  std::vector<float> block(int frames, const std::vector<GestureEvent>& g = {}, GestureParams gp = {}) {
    std::vector<float> in(static_cast<std::size_t>(frames), 0.f), l(static_cast<std::size_t>(frames)), rr(static_cast<std::size_t>(frames));
    const float* inp[2] = {in.data(), in.data()};
    float* outp[2] = {l.data(), rr.data()};
    TransportBlock tb;
    tb.playing = true;
    tb.positionValid = true;
    tb.ppq = ppq;
    tb.bpm = 120.0;
    ppq += frames * 120.0 / (60.0 * kRate);
    RenderParams rp;
    LiveInput live;
    live.gestures = g.empty() ? nullptr : g.data();
    live.numGestures = static_cast<int>(g.size());
    live.gestureParams = gp;
    r.process(tb, rp, inp, outp, 2, frames, live);
    return l;
  }
};

}  // namespace

CHOP_TEST(gesture_cc_mapping_and_rates) {
  GestureEvent g;
  CHECK(gestureFromCc(20, 127, 5, 2, g) && g.kind == GestureKind::Stutter && g.on && g.frame == 5 && g.rate == 2);
  CHECK(gestureFromCc(21, 63, 0, 1, g) && g.kind == GestureKind::Reverse && !g.on);
  CHECK(gestureFromCc(22, 64, 0, 1, g) && g.kind == GestureKind::TapeStop && g.on);
  CHECK(!gestureFromCc(23, 127, 0, 1, g));
  CHECK_EQ(stutterPeriodTicks(0), 480);
  CHECK_EQ(stutterPeriodTicks(1), 240);
  CHECK_EQ(stutterPeriodTicks(2), 120);
  CHECK_EQ(stutterPeriodTicks(-4), 480);
  CHECK_EQ(stutterPeriodTicks(99), 120);
}

CHOP_TEST(reverse_gesture_flips_playing_voices_and_back_on_release) {
  Rig rig(source(true));
  const auto a = rig.block(2000);
  CHECK(a[1500] > a[500]);  // a rising ramp
  CHECK(!rig.r.gestureActive(GestureKind::Reverse));
  const auto b = rig.block(2000, {gest(0, GestureKind::Reverse, true)});
  CHECK(rig.r.gestureActive(GestureKind::Reverse));
  CHECK(b[1500] < b[100]);  // now descending
  const auto c = rig.block(2000, {gest(0, GestureKind::Reverse, false)});
  CHECK(!rig.r.gestureActive(GestureKind::Reverse));
  CHECK(c[1500] > c[100]);
}

CHOP_TEST(reverse_gesture_applies_to_a_hit_that_starts_while_held) {
  Rig rig(source(true), 20000);
  rig.block(64, {gest(0, GestureKind::Reverse, true)});  // the pattern hit began at frame 0 of the first block
  const auto b = rig.block(2000);
  CHECK(b[1500] < b[300]);
  for (float v : b) CHECK(std::isfinite(v));
}

CHOP_TEST(tape_stop_slows_to_silence_in_the_set_time_and_spins_back_up) {
  Rig rig(source(false));
  GestureParams gp;
  gp.tapeStopMs = 400.f;
  gp.spinUpMs = 150.f;
  const auto warm = rig.block(4096);
  CHECK(warm[4000] > 0.99f);
  auto stop = rig.block(4096, {gest(0, GestureKind::TapeStop, true)}, gp);
  CHECK(rig.r.gestureActive(GestureKind::TapeStop));
  CHECK(stop[100] < warm[4000] && stop[100] > 0.9f);  // barely started
  for (int i = 0; i < 4; ++i) stop = rig.block(4096, {}, gp);  // 5 blocks = 427 ms in total
  CHECK(std::fabs(stop[4000]) < 1e-3f);
  auto up = rig.block(4096, {gest(0, GestureKind::TapeStop, false)}, gp);
  CHECK(!rig.r.gestureActive(GestureKind::TapeStop));
  CHECK(up[10] < 0.05f);
  CHECK(up[4000] > 0.4f && up[4000] < 0.7f);  // 83 ms into a 150 ms spin-up
  const auto later = rig.block(4096, {}, gp);
  CHECK(later[4000] > 0.99f);
  for (float v : up) CHECK(std::isfinite(v));
}

CHOP_TEST(tape_stop_time_is_clamped_and_follows_the_parameter) {
  Rig fast(source(false));
  Rig slow(source(false));
  GestureParams gf, gs;
  gf.tapeStopMs = 50.f;
  gs.tapeStopMs = 1000.f;
  fast.block(2048);
  slow.block(2048);
  const auto f = fast.block(4096, {gest(0, GestureKind::TapeStop, true)}, gf);
  const auto s = slow.block(4096, {gest(0, GestureKind::TapeStop, true)}, gs);
  CHECK(std::fabs(f[4000]) < 1e-3f);
  CHECK(s[4000] > 0.9f);
  GestureParams bad;
  bad.tapeStopMs = std::nanf("");
  bad.spinUpMs = -5.f;
  Rig odd(source(false));
  odd.block(2048);
  for (float v : odd.block(4096, {gest(0, GestureKind::TapeStop, true)}, bad)) CHECK(std::isfinite(v));
}

CHOP_TEST(stutter_repeats_the_frozen_hit_on_its_rate_and_stops_on_release) {
  Rig rig(source(true), 60000);
  rig.block(2000);  // the hit starts, so there is something to freeze
  std::vector<float> all;
  auto b = rig.block(4096, {gest(0, GestureKind::Stutter, true, 1)});
  CHECK(rig.r.gestureActive(GestureKind::Stutter));
  all.insert(all.end(), b.begin(), b.end());
  for (int i = 0; i < 5; ++i) {
    b = rig.block(4096);
    all.insert(all.end(), b.begin(), b.end());
  }
  // 1/16 = 240 ticks = 6000 frames: a repeat restarts the slice (the ramp restarts low) every 6000 frames.
  int restarts = 0;
  for (std::size_t i = 6000 - 200; i + 1 < all.size(); ++i)
    if (all[i] < 0.01f && all[i] <= all[i + 1] && (i == 0 || all[i - 1] > all[i] || all[i - 1] == all[i])) {
      ++restarts;
      i += 3000;
    }
  CHECK(restarts >= 3 && restarts <= 5);
  for (float v : all) CHECK(std::isfinite(v));
  // The first slice of the source is all a repeat ever plays: the ramp never gets past its first 6000 frames (value 0.18).
  float peak = 0.f;
  for (float v : all) peak = std::max(peak, std::fabs(v));
  CHECK(peak < 0.25f);
  const auto off = rig.block(4096, {gest(0, GestureKind::Stutter, false)});
  CHECK(!rig.r.gestureActive(GestureKind::Stutter));
  const auto after = rig.block(8192);
  CHECK(std::fabs(after[8000]) < 1e-3f);  // the last repeat ended and the groove waits for its next hit
  (void)off;
}

CHOP_TEST(gesture_state_is_cleared_by_prepare_and_events_past_the_cap_are_ignored) {
  Rig rig(source(false));
  rig.block(2048, {gest(0, GestureKind::Reverse, true), gest(0, GestureKind::TapeStop, true)});
  CHECK(rig.r.gestureActive(GestureKind::Reverse) && rig.r.gestureActive(GestureKind::TapeStop));
  Config cfg;
  cfg.sampleRate = kRate;
  cfg.maxBlock = 4096;
  cfg.maxVoices = 8;
  rig.r.prepare(cfg);
  CHECK(!rig.r.gestureActive(GestureKind::Reverse) && !rig.r.gestureActive(GestureKind::TapeStop) && !rig.r.gestureActive(GestureKind::Stutter));
  std::vector<GestureEvent> many;
  for (int i = 0; i < 2 * kMaxGestureEventsPerBlock; ++i) many.push_back(gest(i, GestureKind::Reverse, (i & 1) == 0));
  for (float v : rig.block(2048, many)) CHECK(std::isfinite(v));
}
