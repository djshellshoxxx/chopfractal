#include <chopfractal/level_match/level_match.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal::levelmatch;

namespace {
constexpr double kPi = 3.14159265358979323846;

std::vector<float> tone(int rate, double seconds, double amp, double hz = 997.0) {
  std::vector<float> v(static_cast<std::size_t>(rate * seconds));
  for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(amp * std::sin(2 * kPi * hz * static_cast<double>(i) / rate));
  return v;
}
Loudness stereo(const std::vector<float>& x, int rate) {
  const float* p[2] = {x.data(), x.data()};
  return measure(p, 2, static_cast<std::int64_t>(x.size()), rate);
}
}  // namespace

CHOP_TEST(k_weighting_coefficients_match_the_published_48k_values) {
  const Biquad s = kWeightShelf(48000.0), h = kWeightHighPass(48000.0);
  CHECK_NEAR(s.b0, 1.53512485958697, 1e-9);
  CHECK_NEAR(s.b1, -2.69169618940638, 1e-9);
  CHECK_NEAR(s.b2, 1.19839281085285, 1e-9);
  CHECK_NEAR(s.a1, -1.69065929318241, 1e-9);
  CHECK_NEAR(s.a2, 0.73248077421585, 1e-9);
  CHECK_NEAR(h.a1, -1.99004745483398, 1e-9);
  CHECK_NEAR(h.a2, 0.99007225036621, 1e-9);
}

CHOP_TEST(a_full_scale_997hz_sine_in_both_channels_measures_zero_lufs_and_gain_scales_exactly_in_db) {
  const Loudness full = stereo(tone(48000, 5.0, 1.0), 48000);
  const Loudness half = stereo(tone(48000, 5.0, 0.5), 48000);
  CHECK(full.valid && half.valid);
  CHECK_NEAR(full.lufs, 0.0, 0.1);
  CHECK_NEAR(half.lufs, -6.02, 0.1);
  CHECK_NEAR(full.lufs - half.lufs, 6.0206, 1e-3);
  CHECK_NEAR(full.samplePeak, 1.0, 1e-6);
  // Mono measures the same as the same signal in both channels.
  const std::vector<float> m = tone(48000, 5.0, 0.5);
  const float* p[1] = {m.data()};
  CHECK_NEAR(measure(p, 1, static_cast<std::int64_t>(m.size()), 48000).lufs, half.lufs, 1e-9);
}

CHOP_TEST(gating_ignores_silence_and_sections_more_than_10_lu_below_the_rest) {
  std::vector<float> a = tone(48000, 10.0, 0.1);
  const double alone = stereo(a, 48000).lufs;
  std::vector<float> withSilence = a;
  withSilence.resize(a.size() * 2, 0.f);
  CHECK_NEAR(stereo(withSilence, 48000).lufs, alone, 0.1);
  // A section 16 LU below the rest is excluded by the relative gate (the gate sits 10 LU under the mean of everything, 12.8 LU under the loud part).
  std::vector<float> mixed = a;
  const std::vector<float> quiet = tone(48000, 10.0, 0.1 / 6.31);
  mixed.insert(mixed.end(), quiet.begin(), quiet.end());
  CHECK_NEAR(stereo(mixed, 48000).lufs, alone, 0.1);
  // A section only 6 LU below is kept, so the result falls.
  std::vector<float> near = a;
  const std::vector<float> half = tone(48000, 10.0, 0.05);
  near.insert(near.end(), half.begin(), half.end());
  CHECK(stereo(near, 48000).lufs < alone - 0.5);
}

CHOP_TEST(short_signals_use_one_padded_block_and_below_100ms_are_invalid) {
  CHECK(!stereo(tone(48000, 0.09, 0.5), 48000).valid);
  const Loudness s = stereo(tone(48000, 0.2, 0.5), 48000);
  CHECK(s.valid);
  // 0.2 s of a tone in a 0.4 s zero-padded block is 3 dB below the continuous tone.
  CHECK_NEAR(s.lufs, stereo(tone(48000, 5.0, 0.5), 48000).lufs - 3.01, 0.15);
  CHECK(!stereo(std::vector<float>(48000, 0.f), 48000).valid);  // silence
  CHECK(!measure(nullptr, 2, 100, 48000).valid);
}

CHOP_TEST(measurement_does_not_depend_on_chunking_or_on_the_sample_rate_of_the_same_tone) {
  const std::vector<float> x = tone(48000, 6.0, 0.3);
  const Loudness whole = stereo(x, 48000);
  std::vector<float> rebuilt(x.begin(), x.begin() + 100000);
  rebuilt.insert(rebuilt.end(), x.begin() + 100000, x.end());
  CHECK_EQ(stereo(rebuilt, 48000).lufs, whole.lufs);
  for (int rate : {44100, 96000, 88200, 32000}) CHECK_NEAR(stereo(tone(rate, 6.0, 0.3), rate).lufs, whole.lufs, 0.1);
}

CHOP_TEST(non_finite_samples_are_ignored_not_propagated) {
  std::vector<float> x = tone(48000, 3.0, 0.3);
  const double clean = stereo(x, 48000).lufs;
  x[1000] = std::nanf("");
  x[2000] = INFINITY;
  const Loudness l = stereo(x, 48000);
  CHECK(l.valid && std::isfinite(l.lufs) && std::isfinite(l.samplePeak));
  CHECK_NEAR(l.lufs, clean, 0.05);
}

CHOP_TEST(compute_match_clamps_rounds_and_applies_trim) {
  Loudness src{-14.0, 0.9, true}, pat{-34.0, 0.1, true};
  MatchResult r = computeMatch(src, pat, 0.0, true, -1.0);
  CHECK(r.valid && r.clamped);
  CHECK_NEAR(r.requestedDb, 20.0, 1e-9);
  CHECK_NEAR(r.gainDb, 12.0, 1e-9);
  pat.lufs = -19.04;
  r = computeMatch(src, pat, 0.0, true, -1.0);
  CHECK_NEAR(r.gainDb, 5.0, 1e-9);  // 5.04 rounded to 0.1 dB
  CHECK(!r.clamped);
  r = computeMatch(src, pat, 2.0, true, -1.0);
  CHECK_NEAR(r.gainDb, 7.0, 1e-9);
  pat.lufs = 0.0;
  r = computeMatch(src, pat, 0.0, true, -1.0);
  CHECK_NEAR(r.gainDb, -12.0, 1e-9);
  CHECK(r.clamped);
}

CHOP_TEST(headroom_rule_reduces_the_boost_when_the_limiter_is_off_only) {
  Loudness src{-14.0, 1.0, true}, pat{-20.0, 0.9, true};
  MatchResult off = computeMatch(src, pat, 0.0, false, -1.0);
  CHECK(off.headroomLimited);
  CHECK(0.9 * std::pow(10.0, off.gainDb / 20.0) <= std::pow(10.0, -0.1 / 20.0) + 1e-12);
  CHECK(off.gainDb < 6.0 && off.gainDb > 0.5);
  MatchResult on = computeMatch(src, pat, 0.0, true, -1.0);
  CHECK(!on.headroomLimited);
  CHECK_NEAR(on.gainDb, 6.0, 1e-9);
}

CHOP_TEST(silent_or_invalid_inputs_return_gain_zero_and_not_valid) {
  const Loudness bad;
  const Loudness ok{-14.0, 0.5, true};
  CHECK(!computeMatch(bad, ok, 0.0, true, -1.0).valid);
  CHECK(!computeMatch(ok, bad, 0.0, true, -1.0).valid);
  CHECK_EQ(computeMatch(ok, bad, 0.0, true, -1.0).gainDb, 0.0);
  CHECK(!computeMatch(ok, ok, std::nan(""), true, -1.0).valid);
}
