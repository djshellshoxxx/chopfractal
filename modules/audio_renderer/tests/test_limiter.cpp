// Safety limiter (F16) and the renderer's output stage: ceiling, true peak, transparency, latency, release, determinism.
#include <chopfractal/audio_renderer/limiter.hpp>
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kLead = 3000;  // silence before the signal: the limiter is enabled, warmed up and fully faded in by then

struct Stereo {
  std::vector<float> l, r;
};

double db(double lin) { return 20.0 * std::log10(std::max(1e-12, lin)); }

Stereo run(const Stereo& in, double ceilDb, int block = 512, double sr = 48000.0) {
  Limiter lim;
  lim.prepare(sr, block);
  Stereo out = in;
  const LimiterParams p{true, static_cast<float>(ceilDb)};
  for (std::size_t pos = 0; pos < in.l.size(); pos += static_cast<std::size_t>(block)) {
    const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), in.l.size() - pos));
    lim.process(p, out.l.data() + pos, out.r.data() + pos, n);
  }
  return out;
}

Stereo withLead(const std::vector<float>& l, const std::vector<float>& r) {
  Stereo s;
  s.l.assign(kLead, 0.f);
  s.r.assign(kLead, 0.f);
  s.l.insert(s.l.end(), l.begin(), l.end());
  s.r.insert(s.r.end(), r.begin(), r.end());
  s.l.insert(s.l.end(), 4000, 0.f);  // a tail so the delayed end of the signal is inside the buffer
  s.r.insert(s.r.end(), 4000, 0.f);
  return s;
}

// Independent true-peak reference: 8x oversampling by a 65-tap Blackman-windowed sinc (not the limiter's interpolator).
double referenceTruePeak(const std::vector<float>& x, std::size_t from = 0) {
  double peak = 0;
  const int half = 32;
  for (std::size_t n = std::max<std::size_t>(from, half); n + half < x.size(); ++n) {
    peak = std::max(peak, static_cast<double>(std::fabs(x[n])));
    for (int q = 1; q < 8; ++q) {
      double y = 0;
      for (int k = -half; k <= half; ++k) {
        const double u = k - q / 8.0;
        const double sinc = std::sin(kPi * u) / (kPi * u);
        const double w = 0.42 + 0.5 * std::cos(kPi * u / (half + 1)) + 0.08 * std::cos(2 * kPi * u / (half + 1));
        y += x[static_cast<std::size_t>(static_cast<std::int64_t>(n) + k)] * sinc * w;
      }
      peak = std::max(peak, std::fabs(y));
    }
  }
  return peak;
}

std::vector<float> sine(double hz, double amp, std::size_t n, double phase = 0.0, double sr = 48000.0) {
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<float>(amp * std::sin(2 * kPi * hz * static_cast<double>(i) / sr + phase));
  return v;
}

}  // namespace

CHOP_TEST(output_never_exceeds_the_ceiling_and_the_true_peak_stays_within_a_tenth_of_a_db) {
  Rng rng(12);
  const std::size_t n = 48000;
  struct Sig {
    const char* name;
    std::vector<float> l, r;
  };
  std::vector<Sig> sigs;
  sigs.push_back({"sine +6 dBFS", sine(997, 2.0, n), sine(997, 2.0, n)});
  {
    std::vector<float> a(n), b(n);
    for (std::size_t i = 0; i < n; ++i) {
      a[i] = static_cast<float>(rng.uniform01() * 2 - 1) * 4.0f;  // +12 dB
      b[i] = static_cast<float>(rng.uniform01() * 2 - 1) * 4.0f;
    }
    sigs.push_back({"noise +12 dB", a, b});
  }
  {
    std::vector<float> a(n, 0.f);
    for (std::size_t i = 0; i < n; i += 977) a[i] = 3.0f;
    sigs.push_back({"impulse train", a, a});
  }
  {
    std::vector<float> a(n);
    for (std::size_t i = 0; i < n; ++i) a[i] = (i / 6 % 2) ? 1.5f : -1.5f;  // square wave at fs/12
    sigs.push_back({"square", a, a});
  }
  {
    std::vector<float> a(n, 0.f);
    for (std::size_t i = n / 2; i < n; ++i) a[i] = 1.8f;
    sigs.push_back({"DC step", a, a});
  }
  {
    // fs/4 sine at 45 degrees: every sample is 0.707 of the peak, the inter-sample peaks are 3 dB higher.
    sigs.push_back({"fs/4 at 45 deg", sine(12000, 1.0, n, kPi / 4), sine(12000, 1.0, n, kPi / 4)});
  }
  for (double ceil : {-6.0, -3.0, -1.0, 0.0}) {
    for (const Sig& s : sigs) {
      const Stereo in = withLead(s.l, s.r);
      const Stereo out = run(in, ceil);
      const double lin = std::pow(10.0, ceil / 20.0);
      const std::size_t settled = kLead + 400;
      float samplePeak = 0.f;
      for (std::size_t i = settled; i < out.l.size(); ++i) samplePeak = std::max(samplePeak, std::max(std::fabs(out.l[i]), std::fabs(out.r[i])));
      CHECK(samplePeak <= lin * 1.000001);
      const double tp = std::max(referenceTruePeak(out.l, settled), referenceTruePeak(out.r, settled));
      if (db(tp) > ceil + 0.1) std::fprintf(stderr, "true peak %.3f dB over ceiling %.1f for %s\n", db(tp), ceil, s.name);
      CHECK(db(tp) <= ceil + 0.1);
    }
  }
}

CHOP_TEST(a_limiter_without_the_detector_would_fail_the_true_peak_check) {
  // Mutation check recorded as a test: limiting on the sample peak alone leaves the fs/4 inter-sample peaks 3 dB over.
  const std::size_t n = 48000;
  std::vector<float> x = sine(12000, 1.0, n, kPi / 4);
  const double samplePeak = *std::max_element(x.begin(), x.end());
  CHECK_NEAR(samplePeak, 0.7071, 0.01);
  CHECK_NEAR(referenceTruePeak(x), 1.0, 0.01);
  const Stereo out = run(withLead(x, x), -1.0);
  float sp = 0.f;
  for (std::size_t i = kLead + 400; i < out.l.size(); ++i) sp = std::max(sp, std::fabs(out.l[i]));
  CHECK(sp < 0.7f);  // the limiter sees the inter-sample peak and pulls the samples well below the ceiling
}

CHOP_TEST(signals_below_the_knee_pass_unchanged_but_delayed_by_the_latency) {
  const std::vector<float> a = sine(440, 0.25, 24000), b = sine(660, 0.2, 24000, 1.0);  // -12 dBFS, ceiling -1
  const Stereo in = withLead(a, b);
  const Stereo out = run(in, -1.0);
  Limiter probe;
  probe.prepare(48000.0, 512);
  const std::size_t L = static_cast<std::size_t>(probe.latencyFrames());
  for (std::size_t i = kLead + 100; i < in.l.size() - 100; ++i) {
    CHECK_NEAR(out.l[i], in.l[i - L], 1e-6);
    CHECK_NEAR(out.r[i], in.r[i - L], 1e-6);
  }
}

CHOP_TEST(latency_is_exactly_lookahead_plus_detector_delay_at_every_sample_rate) {
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    Limiter lim;
    lim.prepare(sr, 512);
    const int expect = std::max(8, static_cast<int>(std::lround(0.0015 * sr))) + 16;
    CHECK_EQ(lim.latencyFrames(), expect);
    CHECK_EQ(Limiter::latencyFor(sr), expect);
    Stereo in;
    in.l.assign(20000, 0.f);
    in.r.assign(20000, 0.f);
    in.l[8000] = 0.05f;
    in.r[8000] = 0.05f;
    const Stereo out = run(in, -1.0, 512, sr);
    std::size_t peakAt = 0;
    for (std::size_t i = 0; i < out.l.size(); ++i)
      if (std::fabs(out.l[i]) > std::fabs(out.l[peakAt])) peakAt = i;
    CHECK_EQ(static_cast<int>(peakAt), 8000 + expect);
  }
  CHECK_EQ(Limiter::latencyFor(48000.0), 88);
  CHECK_EQ(Limiter::latencyFor(44100.0), 82);
}

CHOP_TEST(release_recovers_towards_unity_with_a_time_constant_of_120_ms) {
  // A DC bed well under the knee with one 2 ms burst that needs about -4.7 dB; the bed's gain shows the recovery.
  std::vector<float> a(96000, 0.05f);
  for (std::size_t i = 10000; i < 10096; ++i) a[i] = 1.8f;
  const Stereo out = run(withLead(a, a), -1.0);
  Limiter probe;
  probe.prepare(48000.0, 512);
  const std::size_t L = static_cast<std::size_t>(probe.latencyFrames());
  auto gain = [&](std::size_t srcIndex) { return out.l[kLead + srcIndex + L] / 0.05; };
  // The smoothed gain has a floor (boxcar of the release stage); read the minimum after the burst and then at 1 and 3 tau.
  double gmin = 1.0;
  std::size_t at = 0;
  for (std::size_t i = 10096; i < 10096 + 600; ++i)
    if (gain(i) < gmin) {
      gmin = gain(i);
      at = i;
    }
  CHECK(gmin < 0.7 && gmin > 0.4);
  const double tau = 0.120 * 48000.0;
  CHECK_NEAR(gain(at + static_cast<std::size_t>(tau)), 1.0 - (1.0 - gmin) * std::exp(-1.0), 0.04);
  CHECK_NEAR(gain(at + static_cast<std::size_t>(3 * tau)), 1.0 - (1.0 - gmin) * std::exp(-3.0), 0.03);
  CHECK(gain(at + static_cast<std::size_t>(5 * tau)) > 0.99);
}

CHOP_TEST(stereo_link_applies_equal_gain_to_both_channels) {
  std::vector<float> loud = sine(300, 1.8, 24000), quiet = sine(300, 0.09, 24000);
  const Stereo out = run(withLead(loud, quiet), -1.0);
  for (std::size_t i = kLead + 3000; i < kLead + 20000; i += 41) {
    if (std::fabs(out.r[i]) < 1e-4f) continue;
    Limiter probe;
    probe.prepare(48000.0, 512);
    const std::size_t L = static_cast<std::size_t>(probe.latencyFrames());
    const double gl = out.l[i] / loud[i - kLead - L];
    const double gr = out.r[i] / quiet[i - kLead - L];
    if (std::fabs(loud[i - kLead - L]) > 0.05f && std::fabs(quiet[i - kLead - L]) > 0.005f) CHECK_NEAR(gl, gr, 1e-4);
  }
}

CHOP_TEST(non_finite_input_is_sanitized_and_state_never_goes_nan) {
  std::vector<float> a = sine(500, 1.2, 24000);
  a[5000] = std::numeric_limits<float>::quiet_NaN();
  a[5001] = std::numeric_limits<float>::infinity();
  a[9000] = -std::numeric_limits<float>::infinity();
  const Stereo out = run(withLead(a, a), -1.0);
  for (float x : out.l) CHECK(std::isfinite(x));
  for (float x : out.r) CHECK(std::isfinite(x));
}

CHOP_TEST(limiter_output_is_identical_for_every_block_size_including_the_enable_and_disable_ramps) {
  Rng rng(3);
  std::vector<float> a(48000), b(48000);
  for (std::size_t i = 0; i < a.size(); ++i) {
    a[i] = static_cast<float>(rng.uniform01() * 2 - 1) * 1.6f * static_cast<float>(0.5 + 0.5 * std::sin(i / 3000.0));
    b[i] = static_cast<float>(rng.uniform01() * 2 - 1) * 1.2f;
  }
  auto runToggled = [&](int block) {
    Limiter lim;
    lim.prepare(48000.0, block);
    Stereo o;
    o.l = a;
    o.r = b;
    for (std::size_t pos = 0; pos < a.size(); pos += static_cast<std::size_t>(block)) {
      const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), a.size() - pos));
      // The enable flag flips at fixed sample positions, so split blocks at those positions.
      std::size_t p0 = pos;
      while (p0 < pos + static_cast<std::size_t>(n)) {
        const std::size_t cuts[] = {6000, 30000};
        std::size_t end = pos + static_cast<std::size_t>(n);
        for (std::size_t c : cuts)
          if (p0 < c && c < end) end = c;
        const bool on = p0 >= 6000 && p0 < 30000;
        lim.process(LimiterParams{on, -1.f}, o.l.data() + p0, o.r.data() + p0, static_cast<int>(end - p0));
        p0 = end;
      }
    }
    return o;
  };
  const Stereo ref = runToggled(512);
  for (int block : {1, 7, 64, 333, 2048}) {
    const Stereo o = runToggled(block);
    CHECK(o.l == ref.l && o.r == ref.r);
  }
  // Disabled stretches are a bit-exact bypass.
  for (std::size_t i = 0; i < 6000; ++i) CHECK(ref.l[i] == a[i]);
  for (std::size_t i = 38000; i < a.size(); ++i) CHECK(ref.l[i] == a[i]);
}

CHOP_TEST(enable_and_disable_crossfade_has_no_discontinuity) {
  const std::vector<float> a = sine(100, 0.1, 48000);
  Limiter lim;
  lim.prepare(48000.0, 512);
  Stereo o;
  o.l = a;
  o.r = a;
  const double slope = 2 * kPi * 100.0 / 48000.0 * 0.1;
  for (std::size_t pos = 0; pos < a.size(); pos += 100) {
    const bool on = pos >= 5000 && pos < 30000;
    lim.process(LimiterParams{on, -1.f}, o.l.data() + pos, o.r.data() + pos, 100);
  }
  float worst = 0.f;
  for (std::size_t i = 1; i < o.l.size(); ++i) worst = std::max(worst, std::fabs(o.l[i] - o.l[i - 1]));
  CHECK(worst <= slope + 0.01);
}

CHOP_TEST(no_limiter_state_survives_a_reset_and_telemetry_reports_reduction) {
  const std::vector<float> a = sine(300, 2.0, 24000);
  Limiter lim;
  lim.prepare(48000.0, 512);
  Stereo o = withLead(a, a);
  lim.process(LimiterParams{true, -1.f}, o.l.data(), o.r.data(), static_cast<int>(o.l.size()));
  CHECK(lim.telemetry().takeGr() < -3.f);
  CHECK(lim.telemetry().takePeak() <= -0.9f);
  CHECK(lim.telemetry().takeGr() == 0.f);  // taking resets
  CHECK_EQ(lim.telemetry().backstops.load(), 0u);  // the sample-peak clamp is a backstop, not the mechanism
  lim.reset();
  CHECK(!lim.active());
}

// ---------------------------------------------------------------------------------------------------------------------
// Renderer output stage
namespace {
std::shared_ptr<SourceData> loudSource() {
  auto s = std::make_shared<SourceData>();
  s->channels = 2;
  s->sampleRate = 48000;
  s->frames = 48000;
  s->samples.resize(96000);
  for (std::int64_t i = 0; i < 48000; ++i) {
    const float v = static_cast<float>(1.6 * std::sin(2 * kPi * 220.0 * static_cast<double>(i) / 48000.0));
    s->samples[static_cast<std::size_t>(i)] = v;
    s->samples[static_cast<std::size_t>(48000 + i)] = v;
  }
  return s;
}
std::shared_ptr<const Playback> loudPlayback() {
  FlatEventList l;
  for (std::uint64_t i = 0; i < 4; ++i) {
    FlatEvent e;
    e.id = EventId{i + 1};
    e.chop = ChopId{1};
    e.region = {0, 48000};
    e.start = static_cast<Ticks>(i * 960);
    e.duration = 900;
    l.push_back(e);
  }
  auto r = makePlayback(loudSource(), l, 3840);
  CHECK(r.ok());
  return r.value();
}
std::vector<std::vector<float>> renderWith(const RenderParams& p, int block = 512, double tail = 0.2) {
  OfflineSettings s;
  s.blockSize = block;
  s.tailSeconds = tail;
  s.params = p;
  auto r = renderOffline(loudPlayback(), s);
  CHECK(r.ok());
  return r.ok() ? r.value() : std::vector<std::vector<float>>(2);
}
}  // namespace

CHOP_TEST(with_match_gain_zero_and_the_limiter_off_the_output_equals_the_plain_renderer_bit_for_bit) {
  RenderParams plain;
  RenderParams armed;
  armed.matchGainDb = 0.f;
  armed.limiterEnabled = false;
  armed.limiterCeilingDb = -3.f;  // irrelevant while off
  const auto a = renderWith(plain), b = renderWith(armed);
  CHECK(a[0] == b[0] && a[1] == b[1]);
}

CHOP_TEST(the_renderer_limits_the_whole_output_and_an_export_is_latency_compensated) {
  RenderParams on;
  on.limiterEnabled = true;
  on.limiterCeilingDb = -1.f;
  const auto lim = renderWith(on);
  const auto raw = renderWith(RenderParams{});
  CHECK_EQ(lim[0].size(), raw[0].size());  // same length
  float rawPeak = 0.f, limPeak = 0.f;
  for (float x : raw[0]) rawPeak = std::max(rawPeak, std::fabs(x));
  // Skip the enable ramp at the start; the limiter has had its warm-up by 1/10 s.
  for (std::size_t i = 6000; i < lim[0].size(); ++i) limPeak = std::max(limPeak, std::fabs(lim[0][i]));
  CHECK(rawPeak > 1.5f);
  CHECK(limPeak <= std::pow(10.0f, -1.f / 20.f) * 1.000001f);
  // Compensated: the limited export lines up with the unlimited one (the lag that maximizes the correlation is 0).
  long bestLag = 999;
  double best = -1e30;
  for (long lag = -300; lag <= 300; ++lag) {
    double acc = 0;
    for (std::size_t i = 12000; i < 36000; ++i) acc += static_cast<double>(lim[0][i]) * static_cast<double>(raw[0][static_cast<std::size_t>(static_cast<long>(i) + lag)]);
    if (acc > best) {
      best = acc;
      bestLag = lag;
    }
  }
  CHECK(std::labs(bestLag) <= 1);
}

CHOP_TEST(match_gain_ramps_linearly_over_50ms_independent_of_the_block_size_and_the_limiter_output_too) {
  auto render = [&](int block) {
    OfflineSettings s;
    s.blockSize = block;
    s.tailSeconds = 0.1;
    s.params.matchGainDb = 6.f;
    s.params.limiterEnabled = true;
    auto r = renderOffline(loudPlayback(), s);
    CHECK(r.ok());
    return r.value();
  };
  const auto ref = render(512);
  for (int block : {1, 64, 333, 2048}) {
    const auto o = render(block);
    CHECK(o[0] == ref[0] && o[1] == ref[1]);
  }
  for (float x : ref[0]) CHECK(std::isfinite(x));
  // The first block starts at the target (no ramp from 0 dB), a later change ramps over 50 ms.
  Renderer r;
  r.prepare(Config{});
  auto src = std::make_shared<SourceData>();
  src->channels = 2;
  src->sampleRate = 48000;
  src->frames = 48000;
  src->samples.assign(96000, 0.5f);
  FlatEvent e;
  e.id = EventId{1};
  e.chop = ChopId{1};
  e.region = {0, 48000};
  e.start = 0;
  e.duration = 3840;
  auto pb = makePlayback(src, {e}, 3840);
  CHECK(pb.ok());
  r.mailbox().publish(pb.value());
  std::vector<float> in(512, 0.f), oL(512), oR(512);
  const float* inp[2] = {in.data(), in.data()};
  float* outp[2] = {oL.data(), oR.data()};
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  RenderParams p;
  p.matchGainDb = 6.f;
  r.process(tb, p, inp, outp, 2, 512);
  CHECK_NEAR(oL[300], 0.5 * std::pow(10.0, 6.0 / 20.0), 1e-3);  // already at the target
  p.matchGainDb = 0.f;
  float last = oL[300];
  bool monotone = true;
  int steps = 0;
  for (int blk = 1; blk <= 8; ++blk) {
    tb.ppq = blk * 512 * 120.0 / (60.0 * 48000.0);
    r.process(tb, p, inp, outp, 2, 512);
    for (int i = 0; i < 512; ++i) {
      if (oL[static_cast<std::size_t>(i)] > last + 1e-6f) monotone = false;
      last = oL[static_cast<std::size_t>(i)];
      ++steps;
    }
  }
  CHECK(monotone);
  CHECK_NEAR(last, 0.5, 1e-3);  // 8 * 512 = 4096 frames > 2400 frames of ramp
  CHECK(steps == 4096);
}

CHOP_TEST(mono_output_is_limited_before_the_downmix) {
  Renderer r;
  r.prepare(Config{});
  r.mailbox().publish(loudPlayback());
  std::vector<float> in(512, 0.f), o(512);
  const float* inp[1] = {in.data()};
  float* outp[1] = {o.data()};
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  RenderParams p;
  p.limiterEnabled = true;
  float peak = 0.f;
  for (int blk = 0; blk < 200; ++blk) {
    tb.ppq = blk * 512 * 120.0 / (60.0 * 48000.0);
    r.process(tb, p, inp, outp, 1, 512);
    if (blk > 20)
      for (float x : o) peak = std::max(peak, std::fabs(x));
  }
  CHECK(peak <= 0.8914f);
  CHECK(peak > 0.3f);
  CHECK(r.latencyFrames() == Limiter::latencyFor(48000.0));
  p.limiterEnabled = false;
  tb.ppq += 100;
  r.process(tb, p, inp, outp, 1, 512);
  CHECK_EQ(r.latencyFrames(), 0);
}
