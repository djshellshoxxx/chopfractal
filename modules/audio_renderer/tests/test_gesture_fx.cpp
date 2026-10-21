// Gesture effects as per-event blocks: Echo, Tremolo-Pan, Stutter, Roll. Offline checks against the specs' formulas.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::shared_ptr<SourceData> dcSource(std::int64_t frames, float v = 1.f) {
  auto s = std::make_shared<SourceData>();
  s->channels = 2;
  s->sampleRate = 48000;
  s->frames = frames;
  s->samples.assign(static_cast<std::size_t>(2 * frames), v);
  return s;
}
std::shared_ptr<SourceData> rampSource(std::int64_t frames) {
  auto s = dcSource(frames, 0.f);
  for (std::int64_t i = 0; i < frames; ++i) {
    const float v = 0.1f + 0.8f * static_cast<float>(i) / static_cast<float>(frames);  // never silent, so onsets are visible
    s->samples[static_cast<std::size_t>(i)] = v;
    s->samples[static_cast<std::size_t>(frames + i)] = v;
  }
  return s;
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

double fpt(double bpm) { return 48000.0 * 60.0 / (bpm * 960.0); }  // output frames per tick

std::shared_ptr<const Playback> pb(SourcePtr src, const FlatEventList& l, const PlaybackOptions& o = PlaybackOptions{}, Ticks length = 3840) {
  auto r = makePlayback(std::move(src), l, length, o);
  CHECK(r.ok());
  return r.ok() ? r.value() : nullptr;
}

std::vector<std::vector<float>> rnd(const std::shared_ptr<const Playback>& p, double bpm = 120.0, int block = 512, std::uint8_t mask = fxbit::kAll,
                                    bool polish = false, double tail = 0.3) {
  OfflineSettings s;
  s.blockSize = block;
  s.bpm = bpm;
  s.tailSeconds = tail;
  s.params.fxMask = mask;
  s.params.slicePolish = polish;
  auto r = renderOffline(p, s);
  CHECK(r.ok());
  return r.ok() ? r.value() : std::vector<std::vector<float>>(2);
}

// Onsets of a signal that is silent between sounds: the frame where it first exceeds `thr` after being at or below it
// for at least `gap` frames.
std::vector<std::size_t> onsets(const std::vector<float>& v, float thr, std::size_t gap = 20) {
  std::vector<std::size_t> out;
  std::size_t quiet = gap;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (std::fabs(v[i]) > thr) {
      if (quiet >= gap) out.push_back(i);
      quiet = 0;
    } else {
      ++quiet;
    }
  }
  return out;
}
float peakIn(const std::vector<float>& v, std::size_t a, std::size_t b) {
  float m = 0.f;
  for (std::size_t i = a; i < b && i < v.size(); ++i) m = std::max(m, std::fabs(v[i]));
  return m;
}

}  // namespace

// ---------------------------------------------------------------- Echo
CHOP_TEST(echo_repeats_land_on_the_grid_decay_geometrically_and_stay_inside_the_loop) {
  for (double bpm : {100.0, 120.0, 174.5}) {
    for (int rate = 0; rate < 4; ++rate) {
      FlatEvent e = ev(1, 0, 4000, 0, 120);
      e.echo = EchoFx{3, static_cast<std::uint8_t>(rate), 255};
      PlaybackOptions o;
      o.echoDecay = 50;
      o.echoTone = 0;
      const auto p = pb(dcSource(4000, 0.8f), {e}, o, 3840);
      const auto out = rnd(p, bpm, 512, fxbit::kAll, false, 0.0);
      const auto on = onsets(out[0], 1e-7f, 30);
      CHECK_EQ(on.size(), static_cast<std::size_t>(4));
      if (on.size() != 4) continue;
      for (int k = 1; k < 4; ++k) {
        const double expect = static_cast<double>(on[0]) + k * static_cast<double>(kEchoSpacingTicks[rate]) * fpt(bpm);
        CHECK_NEAR(static_cast<double>(on[static_cast<std::size_t>(k)]), expect, 1.5);
        const float pk = peakIn(out[0], on[static_cast<std::size_t>(k)], on[static_cast<std::size_t>(k)] + 400);
        CHECK_NEAR(pk, 0.8 * std::pow(0.5, k), 0.01);
      }
    }
  }
  // Repeats never wrap into the next loop: an event near the loop end loses the repeats that would land past it.
  FlatEvent late = ev(1, 0, 4000, 3600, 120);
  late.echo = EchoFx{5, 1, 255};
  const auto p = pb(dcSource(4000, 0.8f), {late});
  CHECK_EQ(p->events.size(), static_cast<std::size_t>(1));  // 3600 + 480 is past 3840
}

CHOP_TEST(echo_tone_darkens_each_repeat_monotonically) {
  Rng rng(4);
  auto src = dcSource(6000, 0.f);
  for (std::int64_t i = 0; i < 6000; ++i) {
    const float n = static_cast<float>(rng.uniform01() * 2 - 1) * 0.5f;
    src->samples[static_cast<std::size_t>(i)] = n;
    src->samples[static_cast<std::size_t>(6000 + i)] = n;
  }
  FlatEvent e = ev(1, 0, 6000, 0, 240);
  e.echo = EchoFx{4, 1, 100};  // level 100 % so only the tone changes between repeats
  PlaybackOptions o;
  o.echoTone = 50;
  const auto out = rnd(pb(src, {e}, o), 120.0, 512, fxbit::kAll, false, 0.0);
  // Brightness = energy of the first difference over energy of the signal, per repeat window.
  double prev = 1e9;
  for (int k = 0; k <= 4; ++k) {
    const std::size_t a = static_cast<std::size_t>(k * 480 * fpt(120.0)) + 200, b = a + 2000;
    double d = 0, t = 0;
    for (std::size_t i = a + 1; i < b; ++i) {
      d += std::pow(out[0][i] - out[0][i - 1], 2);
      t += std::pow(out[0][i], 2);
    }
    const double bright = d / std::max(1e-12, t);
    CHECK(bright < prev);
    prev = bright;
  }
}

CHOP_TEST(echo_and_every_gesture_effect_can_be_bypassed_without_rebuilding_the_playback_bit_exactly) {
  FlatEvent a = ev(1, 0, 4000, 0, 240);
  a.echo = EchoFx{3, 1, 255};
  FlatEvent b = ev(2, 0, 4000, 960, 480);
  b.stutter = StutterFx{};
  FlatEvent c = ev(3, 0, 4000, 1920, 480);
  c.roll = RollFx{};
  FlatEvent d = ev(4, 0, 4000, 2880, 480);
  d.tremPan = TremPanFx{2, 1, 80};
  auto plain = [](FlatEvent e) {
    e.echo.reset();
    e.stutter.reset();
    e.roll.reset();
    e.tremPan.reset();
    return e;
  };
  const auto src = dcSource(4000, 0.7f);
  const auto withFx = pb(src, {a, b, c, d});
  const auto without = pb(src, {plain(a), plain(b), plain(c), plain(d)});
  CHECK(withFx->events.size() > without->events.size());
  const auto bypassed = rnd(withFx, 120.0, 512, 0);
  const auto reference = rnd(without, 120.0, 512, 0);
  CHECK(bypassed[0] == reference[0] && bypassed[1] == reference[1]);
  const auto live = rnd(withFx, 120.0, 512, fxbit::kAll);
  CHECK(live[0] != reference[0]);
  // Each effect alone also moves the output, and only its own.
  for (std::uint8_t bit : {fxbit::kEcho, fxbit::kTremPan, fxbit::kStutter, fxbit::kRoll}) CHECK(rnd(withFx, 120.0, 512, bit)[0] != reference[0]);
}

CHOP_TEST(echo_repeats_never_steal_a_primary_voice_and_the_prepared_cap_holds) {
  // Eight long primaries fill the pool; the ninth hit has echoes that must be dropped rather than steal.
  FlatEventList l;
  for (std::uint64_t i = 1; i <= 8; ++i) l.push_back(ev(i, 0, 200000, 0, 3000));
  FlatEvent nine = ev(9, 0, 200000, 480, 2800);
  nine.echo = EchoFx{4, 0, 255};
  FlatEventList lx = l;
  lx.push_back(nine);
  FlatEventList lp = l;
  nine.echo.reset();
  lp.push_back(nine);
  const auto src = dcSource(200000, 0.1f);
  const auto x = rnd(pb(src, lx), 120.0, 512, fxbit::kAll, false, 0.0);
  const auto ref = rnd(pb(src, lp), 120.0, 512, fxbit::kAll, false, 0.0);
  CHECK(x[0] == ref[0]);
  // The cap: 512 events with retrigger 8 fill 4096; effect repeats are dropped, never an error.
  FlatEventList big;
  for (std::uint64_t i = 1; i <= 512; ++i) {
    FlatEvent e = ev(i, 0, 4000, static_cast<Ticks>(i * 7 % 3600), 120);
    e.tx.retrigger = 8;
    e.stutter = StutterFx{};
    big.push_back(e);
  }
  auto r = makePlayback(src, big, 3840);
  CHECK(r.ok());
  if (r.ok()) CHECK(r.value()->events.size() <= kMaxPreparedEvents);
}

// ---------------------------------------------------------------- Tremolo-Pan
CHOP_TEST(tremolo_gain_matches_the_formula_at_sample_points) {
  FlatEvent e = ev(1, 0, 40000, 0, 1920);
  e.tremPan = TremPanFx{0, 1, 100};  // tremolo, 1/8 period = 480 ticks = 12000 frames, depth 100 %
  const auto out = rnd(pb(dcSource(40000, 1.f), {e}), 120.0, 512, fxbit::kAll, false, 0.0);
  const double P = 480 * fpt(120.0);
  for (double frac : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5}) {
    const std::size_t n = static_cast<std::size_t>(std::llround(frac * P));
    const double expect = 1.0 - 1.0 * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(n) / P));
    CHECK_NEAR(out[0][n], expect, 2e-3);
    CHECK_NEAR(out[1][n], expect, 2e-3);
  }
  // Depth 40 %: the trough is 0.6.
  e.tremPan = TremPanFx{0, 1, 40};
  const auto o2 = rnd(pb(dcSource(40000, 1.f), {e}), 120.0, 512, fxbit::kAll, false, 0.0);
  CHECK_NEAR(o2[0][static_cast<std::size_t>(std::llround(0.5 * P))], 0.6, 2e-3);
}

CHOP_TEST(pan_is_equal_power_starts_at_the_centre_and_alternates_direction_with_the_event_index) {
  FlatEventList l;
  FlatEvent a = ev(1, 0, 20000, 0, 480);
  a.tremPan = TremPanFx{1, 1, 100};  // pan only, 1/8 period (12000 frames), full depth
  FlatEvent b = ev(2, 0, 20000, 960, 480);
  b.tremPan = a.tremPan;
  const auto out = rnd(pb(dcSource(20000, 0.5f), {a, b}), 120.0, 512, fxbit::kAll, false, 0.0);
  const double P = 480 * fpt(120.0);
  // Centre unity at the start (after the 32-frame attack), constant power across the sweep.
  CHECK_NEAR(out[0][40], 0.5, 0.03);
  CHECK_NEAR(out[1][40], 0.5, 0.03);
  for (double frac = 0.05; frac < 0.95; frac += 0.05) {
    const std::size_t n = static_cast<std::size_t>(frac * P);
    const double l2 = std::pow(out[0][n] / 0.5, 2) + std::pow(out[1][n] / 0.5, 2);
    CHECK_NEAR(l2, 2.0, 0.02);
  }
  const std::size_t quarter = static_cast<std::size_t>(0.25 * P);
  CHECK(out[1][quarter] > out[0][quarter] + 0.3f);  // first event (index 0) moves right
  const std::size_t second = static_cast<std::size_t>(960 * fpt(120.0)) + quarter;
  CHECK(out[0][second] > out[1][second] + 0.3f);    // second event (index 1) moves left
}

CHOP_TEST(tremolo_phase_starts_at_every_hit_onset) {
  FlatEventList l;
  for (std::uint64_t i = 0; i < 3; ++i) {
    FlatEvent e = ev(i + 1, 0, 30000, static_cast<Ticks>(i * 1200), 600);
    e.tremPan = TremPanFx{0, 0, 100};
    l.push_back(e);
  }
  const auto out = rnd(pb(dcSource(30000, 1.f), l), 120.0, 512, fxbit::kAll, false, 0.0);
  for (int k = 1; k < 3; ++k)
    for (int n : {100, 1500, 3000}) CHECK_NEAR(out[0][static_cast<std::size_t>(k * 1200 * fpt(120.0)) + static_cast<std::size_t>(n)], out[0][static_cast<std::size_t>(n)], 2e-3);
}

CHOP_TEST(global_fallbacks_fill_in_blocks_that_say_use_the_global_value_and_forced_off_wins) {
  FlatEvent e = ev(1, 0, 40000, 0, 1920);
  e.tremPan = TremPanFx{};  // all 255: global
  PlaybackOptions o;
  o.tremMode = 0;
  o.tremRate = 1;
  o.tremDepth = 100;
  const auto out = rnd(pb(dcSource(40000, 1.f), {e}, o), 120.0, 512, fxbit::kAll, false, 0.0);
  CHECK_NEAR(out[0][static_cast<std::size_t>(std::llround(0.5 * 480 * fpt(120.0)))], 0.0, 2e-3);
  e.tremPan = TremPanFx{254, 255, 255};  // forced off
  const auto off = rnd(pb(dcSource(40000, 1.f), {e}, o), 120.0, 512, fxbit::kAll, false, 0.0);
  CHECK_NEAR(off[0][static_cast<std::size_t>(std::llround(0.5 * 480 * fpt(120.0)))], 1.0, 1e-4);
}

// ---------------------------------------------------------------- Stutter
CHOP_TEST(stutter_repeats_the_start_of_the_hit_on_the_rate_grid_with_gate_and_decay) {
  FlatEvent e = ev(1, 0, 20000, 0, 960);
  e.stutter = StutterFx{4, 4, 50, 0, -60, 0, 100, 0};  // 1/16 (240 ticks), gate 50 %, no tail, -6 dB per repeat
  const auto out = rnd(pb(dcSource(20000, 0.8f), {e}), 120.0, 512, fxbit::kAll, false, 0.0);
  const auto on = onsets(out[0], 1e-7f, 30);
  CHECK_EQ(on.size(), static_cast<std::size_t>(4));
  if (on.size() != 4) return;
  for (int k = 0; k < 4; ++k) {
    CHECK_NEAR(static_cast<double>(on[static_cast<std::size_t>(k)]), on[0] + k * 240 * fpt(120.0), 1.5);
    CHECK_NEAR(peakIn(out[0], on[static_cast<std::size_t>(k)], on[static_cast<std::size_t>(k)] + 2000), 0.8 * std::pow(0.5, k), 0.01);
  }
  // Gate 50 %: audible for about 120 ticks of every 240, then silent.
  const std::size_t gateEnd = on[1] + static_cast<std::size_t>(120 * fpt(120.0));
  CHECK(std::fabs(out[0][gateEnd + 100]) < 1e-4f);
  CHECK(std::fabs(out[0][gateEnd - 400]) > 0.1f);
}

CHOP_TEST(stutter_tail_makes_the_release_choppy_or_washy) {
  auto level = [&](std::uint8_t tail) {
    FlatEvent e = ev(1, 0, 20000, 0, 960);
    e.stutter = StutterFx{4, 4, 100, tail, 0, 0, 100, 0};
    const auto out = rnd(pb(dcSource(20000, 1.f), {e}), 120.0, 512, fxbit::kAll, false, 0.0);
    // Mean level over the last 25 % of the first repeat (6000 frames long).
    double s = 0;
    for (std::size_t i = 4500; i < 5900; ++i) s += out[0][i];
    return s / 1400.0;
  };
  const double choppy = level(0), washy = level(100);
  CHECK(choppy > 0.95);
  CHECK(washy < 0.4);  // a full-length release has faded most of the way by then
}

CHOP_TEST(stutter_rate_ramp_steps_through_the_divisions_and_ends_on_the_grid) {
  FlatEvent e = ev(1, 0, 20000, 0, 1920);
  e.stutter = StutterFx{2, 6, 100, 0, 0, 0, 100, 0};  // 1/8 -> 1/32 across two beats: 1/8, 1/8T, 1/16, 1/16T, 1/32
  const auto p = pb(dcSource(20000, 0.5f), {e});
  std::vector<Ticks> starts;
  for (std::size_t i = 0; i < p->events.size(); ++i)
    if (p->slices[i].fxBit == fxbit::kStutter) starts.push_back(p->events[i].start);
  CHECK(starts.size() >= 8);
  for (std::size_t i = 2; i < starts.size(); ++i) CHECK(starts[i] - starts[i - 1] <= starts[i - 1] - starts[i - 2]);  // never slows down
  Ticks last = 0;
  for (std::size_t i = 0; i < p->events.size(); ++i)
    if (p->slices[i].fxBit == fxbit::kStutter) last = std::max(last, p->events[i].start + p->events[i].duration);
  CHECK(last <= 1920);
  CHECK(last >= 1920 - 120);  // the final repeat reaches the end of the hit
}

CHOP_TEST(stutter_pitch_ramp_changes_the_read_rate_and_probability_thins_the_repeats) {
  FlatEvent e = ev(1, 0, 40000, 0, 960);
  e.stutter = StutterFx{4, 4, 80, 0, 0, -120, 100, 0};  // -12 st across the hit
  const auto out = rnd(pb(rampSource(40000), {e}), 120.0, 512, fxbit::kAll, false, 0.0);
  const auto on = onsets(out[0], 1e-7f, 30);
  CHECK_EQ(on.size(), static_cast<std::size_t>(4));
  if (on.size() == 4) {
    // On the ramp (0.1 + 0.8 x/N) the value above 0.1 at a fixed time into a repeat (inside the gate) is proportional to the frames consumed.
    const float first = out[0][on[0] + 4000] - 0.1f, last = out[0][on[3] + 4000] - 0.1f;
    CHECK(last / first < 0.75f && last / first > 0.45f);  // the rate falls from 1 to 0.5 over the hit; repeat 3 is at -9 st
  }
  FlatEvent q = e;
  q.stutter->pitch = 0;
  q.stutter->probability = 0;
  const auto thin = pb(dcSource(40000, 1.f), {q});
  int stutterEvents = 0;
  for (const PreparedSlice& s : thin->slices) stutterEvents += s.fxBit == fxbit::kStutter ? 1 : 0;
  CHECK_EQ(stutterEvents, 1);  // only the first repeat is certain
  q.stutter->probability = 60;
  int counts[2] = {0, 0};
  for (int run = 0; run < 2; ++run) {
    q.id = EventId{static_cast<std::uint64_t>(5 + run)};
    for (const PreparedSlice& s : pb(dcSource(40000, 1.f), {q})->slices) counts[run] += s.fxBit == fxbit::kStutter ? 1 : 0;
  }
  CHECK(counts[0] >= 1 && counts[0] <= 4);
  const auto again = pb(dcSource(40000, 1.f), {q});
  int c2 = 0;
  for (const PreparedSlice& s : again->slices) c2 += s.fxBit == fxbit::kStutter ? 1 : 0;
  CHECK_EQ(c2, counts[1]);  // deterministic per event id
}

CHOP_TEST(stutter_pan_alternates_the_repeats_between_left_and_right) {
  FlatEvent e = ev(1, 0, 20000, 0, 960);
  e.stutter = StutterFx{4, 4, 100, 0, 0, 0, 100, 100};
  const auto out = rnd(pb(dcSource(20000, 0.8f), {e}), 120.0, 512, fxbit::kAll, false, 0.0);
  const auto on = onsets(out[0], 0.02f, 30);
  const auto on1 = onsets(out[1], 0.02f, 30);
  CHECK(on.size() >= 1 && on1.size() >= 1);
  const std::size_t r0 = static_cast<std::size_t>(240 * fpt(120.0));
  CHECK(out[0][1000] > 0.5f && out[1][1000] < 0.25f);            // repeat 0 is left
  CHECK(out[1][r0 + 1000] > 0.5f && out[0][r0 + 1000] < 0.25f);  // repeat 1 is right
}

// ---------------------------------------------------------------- Roll
CHOP_TEST(roll_accelerates_with_a_rising_velocity_ramp_and_ends_inside_the_hit) {
  FlatEvent e = ev(1, 0, 20000, 0, 1920);
  e.roll = RollFx{2, 8, 25, 100, 0, kRollSmooth, 0};  // 1/8 -> 1/64, smooth, 25 % -> 100 %
  const auto p = pb(dcSource(20000, 0.8f), {e});
  std::vector<std::pair<Ticks, float>> reps;
  for (std::size_t i = 0; i < p->events.size(); ++i)
    if (p->slices[i].fxBit == fxbit::kRoll) reps.push_back({p->events[i].start, p->events[i].tx.level});
  CHECK(reps.size() >= 12);
  for (std::size_t i = 2; i < reps.size(); ++i) CHECK(reps[i].first - reps[i - 1].first <= reps[i - 1].first - reps[i - 2].first);
  for (std::size_t i = 1; i < reps.size(); ++i) CHECK(reps[i].second > reps[i - 1].second);
  CHECK_NEAR(reps.front().second, 0.25, 1e-4);
  CHECK_NEAR(reps.back().second, 1.0, 1e-4);
  CHECK(reps.back().first < 1920);
  // The base hit is silent while the roll is enabled and audible when it is bypassed.
  const auto rolled = rnd(p, 120.0, 512, fxbit::kAll, false, 0.0);
  const auto bypass = rnd(p, 120.0, 512, 0, false, 0.0);
  CHECK(peakIn(bypass[0], 5000, 40000) > 0.7f);
  CHECK(peakIn(rolled[0], 0, 100000) <= 0.81f);  // the roll peaks at the source level 0.8 times velocity 100 %
}

CHOP_TEST(roll_freezes_a_grab_and_a_palindrome_plays_every_other_repeat_backwards) {
  FlatEvent e = ev(1, 0, 40000, 0, 960);
  e.roll = RollFx{4, 4, 100, 100, 50, kRollPalindrome, 0};  // 1/16 repeats of the grab starting half way into the chop
  const auto out = rnd(pb(rampSource(40000), {e}), 120.0, 512, fxbit::kAll, false, 0.0);
  const auto on = onsets(out[0], 0.05f, 30);
  CHECK(on.size() >= 4);
  if (on.size() < 4) return;
  // On a ramp the value is the source position / 40000: repeat 0 starts at 0.5 and rises, repeat 1 plays backwards from the
  // end of the same grab (higher) and falls.
  const float v0 = 0.1f + 0.8f * 0.5f;  // the grab starts half way into the chop
  CHECK(out[0][on[0] + 100] > v0 - 0.01f && out[0][on[0] + 100] < v0 + 0.03f);
  CHECK(out[0][on[0] + 4000] > out[0][on[0] + 100]);
  CHECK(out[0][on[1] + 4000] < out[0][on[1] + 100]);
  CHECK_NEAR(out[0][on[1] + 100], out[0][on[0] + 5000] + 0.8 * 1000.0 / 40000.0 - 0.8 * 100.0 / 40000.0 * 0, 0.02);  // starts where the forward grab ended
}

CHOP_TEST(gesture_effects_are_identical_for_every_block_size_finite_and_leave_a_clean_tail) {
  Rng rng(21);
  auto src = dcSource(24000, 0.f);
  for (std::int64_t i = 0; i < 24000; ++i) {
    const float n = static_cast<float>((rng.uniform01() * 2 - 1) * std::exp(-static_cast<double>(i % 8000) / 2500.0));
    src->samples[static_cast<std::size_t>(i)] = n;
    src->samples[static_cast<std::size_t>(24000 + i)] = n * 0.8f;
  }
  FlatEventList l;
  FlatEvent a = ev(1, 0, 8000, 0, 480);
  a.echo = EchoFx{4, 1, 60};
  a.tremPan = TremPanFx{2, 0, 70};
  FlatEvent b = ev(2, 8000, 16000, 960, 960);
  b.stutter = StutterFx{3, 7, 70, 40, -20, -50, 80, 60};
  FlatEvent c = ev(3, 16000, 24000, 2160, 1200);
  c.roll = RollFx{2, 9, 30, 100, 30, kRollPalindrome | kRollSmooth, 20};
  FlatEvent d = ev(4, 0, 24000, 3300, 400);
  d.tx.retrigger = 5;
  d.tx.reverse = true;
  l = {a, b, c, d};
  const auto p = pb(src, l);
  for (bool polish : {false, true}) {
    const auto ref = rnd(p, 133.0, 512, fxbit::kAll, polish, 0.5);
    for (int block : {1, 7, 64, 333, 2048}) {
      const auto o = rnd(p, 133.0, block, fxbit::kAll, polish, 0.5);
      CHECK(o[0] == ref[0] && o[1] == ref[1]);
    }
    for (float x : ref[0]) CHECK(std::isfinite(x));
    for (float x : ref[1]) CHECK(std::isfinite(x));
    CHECK(std::fabs(ref[0].back()) < 1e-6f && std::fabs(ref[1].back()) < 1e-6f);  // the tail rings out to silence
  }
}

CHOP_TEST(out_of_range_blocks_are_rejected_by_makePlayback_and_effects_are_null_when_unused) {
  FlatEvent e = ev(1, 0, 4000, 0, 480);
  e.stutter = StutterFx{};
  e.stutter->rate = 42;
  CHECK(!makePlayback(dcSource(4000), {e}, 3840).ok());
  FlatEvent f = ev(1, 0, 4000, 0, 480);
  const auto plain = pb(dcSource(4000), {f});
  CHECK_EQ(plain->events.size(), plain->slices.size());
  CHECK_EQ(plain->slices[0].fxBit, 0);
  CHECK_EQ(plain->slices[0].tremMode, 255);
}

CHOP_TEST(neutral_effect_settings_null_against_the_plain_event) {
  const auto src = dcSource(20000, 0.6f);
  FlatEvent plain = ev(1, 0, 20000, 0, 960);
  FlatEvent a = plain;
  a.tremPan = TremPanFx{2, 1, 0};   // depth 0
  a.echo = EchoFx{0, 255, 255};     // forced off
  const auto ref = rnd(pb(src, {plain}));
  for (bool polish : {false, true}) {
    const auto x = rnd(pb(src, {a}), 120.0, 512, fxbit::kAll, polish);
    const auto y = rnd(pb(src, {plain}), 120.0, 512, fxbit::kAll, polish);
    CHECK(x[0] == y[0] && x[1] == y[1]);
  }
  CHECK(ref[0].size() > 0);
}
