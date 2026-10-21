// Slice-boundary quality (research section 7): fades, transient-aware start, click guard, level match, voice choke.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/audio_renderer/slice_quality.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::shared_ptr<SourceData> makeStereo(std::int64_t frames, int rate = 48000) {
  auto s = std::make_shared<SourceData>();
  s->channels = 2;
  s->sampleRate = rate;
  s->frames = frames;
  s->samples.assign(static_cast<std::size_t>(2 * frames), 0.f);
  return s;
}
void put(SourceData& s, std::int64_t i, float v) {
  s.samples[static_cast<std::size_t>(i)] = v;
  s.samples[static_cast<std::size_t>(s.frames + i)] = v;
}

FlatEvent ev(std::uint64_t id, std::int64_t a, std::int64_t b, Ticks start, Ticks dur) {
  FlatEvent e;
  e.id = EventId{id};
  e.chop = ChopId{id};
  e.region = {a, b};
  e.start = start;
  e.duration = dur;
  return e;
}

std::shared_ptr<const Playback> pb(SourcePtr src, const FlatEventList& l, Ticks length = 3840) {
  auto r = makePlayback(std::move(src), l, length);
  CHECK(r.ok());
  return r.ok() ? r.value() : nullptr;
}

std::vector<std::vector<float>> rnd(const std::shared_ptr<const Playback>& p, bool polish, int block = 512, double tail = 0.0, int cycles = 1) {
  OfflineSettings s;
  s.blockSize = block;
  s.cycles = cycles;
  s.tailSeconds = tail;
  s.params.slicePolish = polish;
  auto r = renderOffline(p, s);
  CHECK(r.ok());
  return r.ok() ? r.value() : std::vector<std::vector<float>>(2);
}

// Frames of one 120 BPM tick: 48000 * 60 / 120 / 960 = 25 frames per 1/16... (1 tick = 0.5208 frames).
constexpr double kFramesPerTick = 48000.0 * 60.0 / (120.0 * 960.0);
std::int64_t atTick(Ticks t) { return static_cast<std::int64_t>(std::ceil(static_cast<double>(t) * kFramesPerTick - 1e-6)); }

float maxDelta(const std::vector<float>& v, std::size_t from = 1, std::size_t to = SIZE_MAX) {
  float m = 0.f;
  to = std::min(to, v.size());
  for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i) m = std::max(m, std::fabs(v[i] - v[i - 1]));
  return m;
}

}  // namespace

CHOP_TEST(analysis_backs_up_to_a_quiet_zero_crossing_and_never_moves_later) {
  auto src = makeStereo(48000);
  // Quiet pre-roll with a sign change, then a loud attack beginning at frame 1000.
  for (std::int64_t i = 900; i < 1000; ++i) put(*src, i, (i % 20 < 10) ? 0.003f : -0.003f);
  for (std::int64_t i = 1000; i < 3000; ++i) put(*src, i, 0.8f * static_cast<float>(std::sin(2 * kPi * 120.0 * static_cast<double>(i - 1000) / 48000.0 + 0.5)));
  // Marker placed 20 frames (0.4 ms) after the attack began: the attack is already under way.
  const SliceAnalysis late = analyzeSlice(*src, {1020, 3000});
  CHECK(late.moved);
  CHECK(late.start < 1020);
  CHECK(late.start >= 1020 - 96);  // within 2 ms at 48 kHz
  CHECK(std::fabs(src->samples[static_cast<std::size_t>(late.start)]) <= 0.1 * 0.8f);
  // A marker in the quiet pre-roll is already before the attack: left alone.
  const SliceAnalysis early = analyzeSlice(*src, {980, 3000});
  CHECK(!early.moved);
  CHECK_EQ(early.start, 980);
  // Silence and invalid regions return the requested start.
  CHECK_EQ(analyzeSlice(*src, {20000, 21000}).start, 20000);
  CHECK_EQ(analyzeSlice(*src, {5, 5}).start, 5);
  for (std::int64_t s = 0; s < 3000; s += 37) CHECK(analyzeSlice(*src, {s, s + 500}).start <= s);
}

CHOP_TEST(analysis_classifies_kick_snare_hat_and_tone) {
  auto src = makeStereo(96000);
  Rng rng(3);
  float lp = 0.f;
  for (int i = 0; i < 9600; ++i) {  // kick: decaying 55 Hz sine
    put(*src, i, 0.9f * static_cast<float>(std::sin(2 * kPi * 55.0 * i / 48000.0) * std::exp(-i / 4000.0)));
  }
  for (int i = 0; i < 9600; ++i) {  // snare: mid noise burst
    const float n = static_cast<float>(rng.uniform01() * 2 - 1);
    lp += 0.45f * (n - lp);
    put(*src, 12000 + i, 0.8f * lp * static_cast<float>(std::exp(-i / 2500.0)));
  }
  float prev = 0.f;
  for (int i = 0; i < 4800; ++i) {  // closed hat: high-passed noise
    const float n = static_cast<float>(rng.uniform01() * 2 - 1);
    put(*src, 24000 + i, 0.6f * (n - prev) * static_cast<float>(std::exp(-i / 700.0)));
    prev = n;
  }
  for (int i = 0; i < 24000; ++i) {  // open cymbal: long high-passed noise
    const float n = static_cast<float>(rng.uniform01() * 2 - 1);
    put(*src, 36000 + i, 0.5f * (n - prev) * static_cast<float>(std::exp(-i / 9000.0)));
    prev = n;
  }
  for (int i = 0; i < 20000; ++i) put(*src, 62000 + i, 0.4f * static_cast<float>(std::sin(2 * kPi * 900.0 * i / 48000.0)));  // sustained tone
  CHECK(analyzeSlice(*src, {0, 9600}).cls == SliceClass::Kick);
  CHECK(analyzeSlice(*src, {12000, 21600}).cls == SliceClass::Snare);
  CHECK(analyzeSlice(*src, {24000, 28800}).cls == SliceClass::Hat);
  CHECK(analyzeSlice(*src, {36000, 60000}).cls == SliceClass::Cymbal);
  CHECK(analyzeSlice(*src, {62000, 82000}).cls == SliceClass::Other);
}

CHOP_TEST(polished_fades_are_raised_cosine_and_class_dependent_and_the_default_is_unchanged) {
  // A constant source is classified as a kick (all energy is low band): attack 0.8 ms = 38 frames at 48 kHz.
  auto src = makeStereo(48000);
  for (int i = 0; i < 48000; ++i) put(*src, i, 1.f);
  const auto p = pb(src, {ev(1, 0, 48000, 0, 960)});
  const auto off = rnd(p, false);
  const auto on = rnd(p, true);
  // v1: linear 32-frame attack. Polished: raised cosine over 38 frames.
  CHECK_NEAR(off[0][16], 0.5, 0.02);
  CHECK_NEAR(off[0][8], 0.25, 0.02);
  CHECK_NEAR(on[0][19], 0.5, 0.02);
  CHECK_NEAR(on[0][9], 0.5 - 0.5 * std::cos(kPi * 9.0 / 38.0), 0.02);
  CHECK(on[0][9] < 0.2f && off[0][8] >= 0.24f);
  CHECK_NEAR(on[0][60], 1.0, 1e-4);
  // Release: 3 ms = 144 frames before the gate end (the gate is 960 ticks = 25000 frames at 120 BPM / 48 kHz).
  const std::int64_t gateEnd = atTick(960);
  CHECK_NEAR(on[0][static_cast<std::size_t>(gateEnd - 72)], 0.5, 0.03);
  CHECK_NEAR(on[0][static_cast<std::size_t>(gateEnd - 200)], 1.0, 1e-4);
}

CHOP_TEST(reversed_slices_get_a_long_attack_and_the_short_release_only_when_they_reach_the_former_transient) {
  auto src = makeStereo(48000);
  for (int i = 0; i < 48000; ++i) put(*src, i, 1.f);
  // The gate (960 ticks = 25000 frames) is longer than the 4800-frame region: the reversed slice runs out first.
  FlatEvent e = ev(1, 0, 4800, 0, 960);
  e.tx.reverse = true;
  const auto on = rnd(pb(src, {e}), true);
  CHECK_NEAR(on[0][96], 0.5, 0.03);  // 4 ms attack = 192 frames, midpoint 96
  CHECK(on[0][2400] > 0.99f);
  CHECK_NEAR(on[0][4800 - 8], 0.5, 0.05);  // 0.3 ms = 14 frames, midpoint 7 frames before the end of the region
  CHECK(on[0][4800 - 40] > 0.99f);
  // A gate that cuts the swell short keeps the normal 3 ms release (no click).
  FlatEvent cut = ev(1, 0, 40000, 0, 240);
  cut.tx.reverse = true;
  const auto o2 = rnd(pb(src, {cut}), true);
  const std::int64_t gateEnd = atTick(240);
  CHECK_NEAR(o2[0][static_cast<std::size_t>(gateEnd - 72)], 0.5, 0.05);
}

CHOP_TEST(a_chops_own_fade_settings_still_win_under_polish) {
  auto src = makeStereo(48000);
  for (int i = 0; i < 48000; ++i) put(*src, i, 1.f);
  FlatEvent e = ev(1, 0, 48000, 0, 960);
  e.fadeInFrames = 480;  // 10 ms at 48 kHz
  const auto on = rnd(pb(src, {e}), true);
  CHECK_NEAR(on[0][240], 0.5, 0.03);
}

CHOP_TEST(slice_edges_do_not_click_across_many_seeds) {
  // Smooth bass-heavy source with a DC offset, so almost every cut lands off a zero crossing. A click would show up
  // as an inter-sample jump far larger than the source's own slope.
  auto src = makeStereo(96000);
  for (int i = 0; i < 96000; ++i) {
    const double t = i / 48000.0;
    put(*src, i, static_cast<float>(0.2 + 0.25 * std::sin(2 * kPi * 80 * t) + 0.2 * std::sin(2 * kPi * 230 * t) + 0.1 * std::sin(2 * kPi * 700 * t)));
  }
  float natural = 0.f;
  for (int i = 1; i < 96000; ++i) natural = std::max(natural, std::fabs(src->samples[static_cast<std::size_t>(i)] - src->samples[static_cast<std::size_t>(i - 1)]));
  float worstOn = 0.f, worstOff = 0.f;
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    Rng rng(seed);
    FlatEventList l;
    for (int slot = 0; slot < 16; ++slot) {  // one hit per 1/16 slot, shorter than the slot, so voices do not overlap
      const std::int64_t a = static_cast<std::int64_t>(rng.uniform(80000));
      const bool reverse = rng.uniform(4) == 0;
      // A reversed slice that runs out of audio ends at its former transient with a deliberate 0.3 ms cut (research 7.1);
      // here the gate always cuts the swell first, which is the case that must not click.
      const std::int64_t len = (reverse ? 7000 : 2000) + static_cast<std::int64_t>(rng.uniform(5000));
      FlatEvent e = ev(static_cast<std::uint64_t>(slot + 1), a, a + len, slot * 240, 60 + static_cast<Ticks>(rng.uniform(170)));
      e.tx.reverse = reverse;
      e.tx.pitchSemitones = static_cast<float>(static_cast<int>(rng.uniform(5)) - 2);
      l.push_back(e);
    }
    const auto p = pb(src, l);
    const auto on = rnd(p, true);
    const auto off = rnd(p, false);
    worstOn = std::max(worstOn, std::max(maxDelta(on[0]), maxDelta(on[1])));
    worstOff = std::max(worstOff, std::max(maxDelta(off[0]), maxDelta(off[1])));
  }
  // The source slope times the pitch factor (<= 2^(2/12)) plus the envelope slope (raised cosine, <= pi/2 / attack).
  const float bound = natural * 1.13f + 0.8f * 1.5708f / 38.f;
  CHECK(worstOn <= bound);
  CHECK(worstOn <= 0.12f);
  CHECK(worstOff <= 0.2f);  // the v1 path is also free of hard clicks here; this guards the test itself
}

CHOP_TEST(level_match_pulls_slices_of_one_class_together_and_only_when_polished) {
  auto src = makeStereo(96000);
  for (int i = 0; i < 9600; ++i) {  // three kicks of very different loudness
    const float env = static_cast<float>(std::exp(-i / 4000.0));
    const float x = static_cast<float>(std::sin(2 * kPi * 55.0 * i / 48000.0)) * env;
    put(*src, i, 0.9f * x);
    put(*src, 20000 + i, 0.3f * x);
    put(*src, 40000 + i, 0.08f * x);
  }
  const auto p = pb(src, {ev(1, 0, 9600, 0, 480), ev(2, 20000, 29600, 960, 480), ev(3, 40000, 49600, 1920, 480)});
  auto peakIn = [](const std::vector<float>& v, std::int64_t from, std::int64_t to) {
    float m = 0.f;
    for (std::int64_t i = from; i < to && i < static_cast<std::int64_t>(v.size()); ++i) m = std::max(m, std::fabs(v[static_cast<std::size_t>(i)]));
    return m;
  };
  const auto off = rnd(p, false);
  const auto on = rnd(p, true);
  auto dbSpread = [&](const std::vector<std::vector<float>>& o) {
    const float a = peakIn(o[0], 0, atTick(900)), b = peakIn(o[0], atTick(960), atTick(1860)), c = peakIn(o[0], atTick(1920), atTick(2820));
    const float hi = std::max(a, std::max(b, c)), lo = std::min(a, std::min(b, c));
    return 20.0 * std::log10(hi / lo);
  };
  CHECK(dbSpread(off) > 15.0);
  CHECK(dbSpread(on) < 9.0);  // the deadband pulls each outlier to the class tolerance edge, so the spread shrinks to about 4 dB per side
  CHECK(dbSpread(on) < dbSpread(off) - 6.0);
}

CHOP_TEST(a_new_kick_fades_the_previous_kick_and_a_snare_ducks_a_ringing_kick) {
  // Kick = constant 0.8 (low-band), snare = decaying mid noise.
  auto src = makeStereo(96000);
  Rng rng(9);
  float lp = 0.f;
  for (int i = 0; i < 48000; ++i) put(*src, i, 0.8f);
  for (int i = 0; i < 9600; ++i) {
    const float n = static_cast<float>(rng.uniform01() * 2 - 1);
    lp += 0.45f * (n - lp);
    put(*src, 60000 + i, 0.9f * lp * static_cast<float>(std::exp(-i / 2500.0)));
  }
  // Two kicks overlapping: second starts at tick 480 (250 ms) while the first (gate 960) still rings.
  {
    const auto p = pb(src, {ev(1, 0, 24000, 0, 960), ev(2, 24000, 48000, 480, 480)});
    const auto off = rnd(p, false);
    const auto on = rnd(p, true);
    const std::size_t at = static_cast<std::size_t>(atTick(480));
    CHECK_NEAR(off[0][at + 2000], 1.6, 0.05);   // v1: both kicks sum
    CHECK_NEAR(on[0][at + 2000], 0.8, 0.05);    // choked: only the new one (the first faded over 3 ms)
    CHECK_NEAR(on[0][at - 10], 0.8, 0.05);      // the first is untouched before the new kick
  }
  // A snare on top of a ringing kick: the kick is ducked by 6 dB, not cut.
  {
    const auto p = pb(src, {ev(1, 0, 48000, 0, 960), ev(2, 60000, 69600, 480, 240)});
    const auto on = rnd(p, true);
    const auto off = rnd(p, false);
    const std::size_t at = static_cast<std::size_t>(atTick(480));
    // Look at the DC (low-pass) of the output: mean over 2400 frames after the duck settled, minus the snare mean (about 0).
    auto mean = [&](const std::vector<float>& v, std::size_t from, std::size_t n) {
      double s = 0;
      for (std::size_t i = 0; i < n; ++i) s += v[from + i];
      return s / static_cast<double>(n);
    };
    CHECK_NEAR(mean(off[0], at + 600, 2400), 0.8, 0.05);
    CHECK_NEAR(mean(on[0], at + 600, 2400), 0.4, 0.05);
  }
}

CHOP_TEST(rolls_get_per_repeat_variation_only_under_polish_and_it_is_deterministic) {
  auto src = makeStereo(48000);
  for (int i = 0; i < 48000; ++i) put(*src, i, 0.5f);
  FlatEvent e = ev(7, 0, 48000, 0, 960);
  e.tx.retrigger = 6;
  const auto p = pb(src, {e});
  auto sub = [&](const std::vector<std::vector<float>>& o, int k) { return o[0][static_cast<std::size_t>(atTick(960 * k / 6) + 400)]; };
  const auto off = rnd(p, false);
  const auto on = rnd(p, true);
  const auto on2 = rnd(p, true, 77);
  CHECK(on[0] == on2[0]);  // block-size independent and deterministic
  // v1: exact geometric 0.8 decay between sub-hits.
  for (int k = 1; k < 6; ++k) CHECK_NEAR(sub(off, k) / sub(off, k - 1), 0.8, 1e-4);
  int varied = 0;
  for (int k = 1; k < 6; ++k)
    if (std::fabs(sub(on, k) / sub(on, k - 1) - 0.8) > 1e-3) ++varied;
  CHECK(varied >= 4);
  for (int k = 1; k < 6; ++k) CHECK_NEAR(sub(on, k) / sub(on, k - 1), 0.8, 0.06);  // sigma 3 vel only
}

CHOP_TEST(polished_output_is_identical_for_every_block_size_and_finite) {
  auto src = makeStereo(96000);
  Rng rng(5);
  for (int i = 0; i < 96000; ++i) put(*src, i, static_cast<float>((rng.uniform01() * 2 - 1) * std::exp(-(i % 12000) / 3000.0)));
  FlatEventList l;
  for (int k = 0; k < 24; ++k) {
    FlatEvent e = ev(static_cast<std::uint64_t>(k + 1), (k % 8) * 12000, (k % 8) * 12000 + 6000, k * 150, 100 + (k % 5) * 90);
    e.tx.reverse = k % 7 == 0;
    e.tx.retrigger = static_cast<std::uint8_t>(1 + (k % 4));
    l.push_back(e);
  }
  const auto p = pb(src, l);
  const auto ref = rnd(p, true, 512, 0.3, 2);
  for (int block : {1, 7, 64, 333, 2048}) {
    const auto o = rnd(p, true, block, 0.3, 2);
    CHECK(o[0] == ref[0] && o[1] == ref[1]);
  }
  for (float x : ref[0]) CHECK(std::isfinite(x));
}
