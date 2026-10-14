#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/time_stretch/stretch.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::stretch;

namespace {

// `stretch` is both a namespace and the function; spell the function out once.
template <class... A>
auto doStretch(A&&... a) {
  return ::chopfractal::stretch::stretch(std::forward<A>(a)...);
}

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double hz, double seconds, int sr, double amp = 0.8) {
  std::vector<float> x(static_cast<std::size_t>(seconds * sr));
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / sr));
  return x;
}

Input mono(const std::vector<float>& x, int sr, const float** holder) {
  holder[0] = x.data();
  Input in;
  in.channels = holder;
  in.numChannels = 1;
  in.frames = static_cast<std::int64_t>(x.size());
  in.sampleRate = sr;
  return in;
}

Plan planFor(std::int64_t in, double ratio, std::vector<std::int64_t> anchors = {}) {
  auto p = makePlan(in, std::llround(static_cast<double>(in) * ratio), anchors);
  CHECK(p.ok());
  return p.ok() ? p.value() : Plan{};
}

// Frequency from linearly interpolated upward zero crossings over the middle 80%.
double frequency(const std::vector<float>& y, int sr) {
  const std::size_t lo = y.size() / 10, hi = y.size() - y.size() / 10;
  int crossings = 0;
  double first = -1.0, last = -1.0;
  for (std::size_t i = lo; i + 1 < hi; ++i)
    if (y[i] < 0.f && y[i + 1] >= 0.f) {
      const double t = static_cast<double>(i) + static_cast<double>(-y[i]) / static_cast<double>(y[i + 1] - y[i]);
      if (first < 0.0) first = t;
      last = t;
      ++crossings;
    }
  return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
}

}  // namespace

CHOP_TEST(map_position_has_no_drift_and_ratio_limits) {
  for (auto [in, out] : {std::pair<std::int64_t, std::int64_t>{11520000, 9216000}, {96000, 76800}, {1000003, 2000006}, {48000, 24000}}) {
    CHECK_EQ(mapPosition(0, in, out), 0);
    CHECK_EQ(mapPosition(in, in, out), out);
    std::int64_t prev = -1;
    for (int k = 0; k <= 1000; ++k) {
      const std::int64_t p = in * k / 1000;
      const std::int64_t m = mapPosition(p, in, out);
      CHECK(std::fabs(static_cast<double>(m) - static_cast<double>(p) * static_cast<double>(out) / static_cast<double>(in)) <= 0.5 + 1e-9);
      CHECK(m >= prev);
      prev = m;
    }
  }
  CHECK(!checkRatio(0.4999).ok() && !checkRatio(2.0001).ok());
  CHECK(checkRatio(0.5).ok() && checkRatio(2.0).ok());
  CHECK_NEAR(lengthRatio(96.0, 120.0), 0.8, 1e-12);
  auto bad = checkRatio(4.8);
  CHECK(bad.error().message == "Out of range: the tempo change must stay between 0.5x and 2x.");
}

CHOP_TEST(plan_adds_ends_maps_anchors_and_drops_collisions) {
  auto p = makePlan(1000, 500, {250, 250, 0, 1000, 251, 999});
  CHECK(p.ok());
  CHECK(p.value().inBounds.front() == 0 && p.value().inBounds.back() == 1000);
  CHECK(p.value().outBounds.front() == 0 && p.value().outBounds.back() == 500);
  for (std::size_t i = 1; i < p.value().inBounds.size(); ++i) {
    CHECK(p.value().inBounds[i] > p.value().inBounds[i - 1]);
    CHECK(p.value().outBounds[i] > p.value().outBounds[i - 1]);
  }
  CHECK(!makePlan(0, 10, {}).ok());
}

CHOP_TEST(output_length_is_exact_for_every_ratio_quality_and_rate) {
  for (int sr : {44100, 48000, 96000})
    for (double ratio : {0.5, 0.75, 1.0, 1.25, 2.0})
      for (Quality q : {Quality::Draft, Quality::Standard, Quality::High}) {
        if (q == Quality::High && sr == 96000) continue;  // keeps the suite fast; covered at 48 kHz
        const auto x = sine(300.0, 1.0, sr);
        const float* h[1];
        const Input in = mono(x, sr, h);
        const Plan plan = planFor(in.frames, ratio, {in.frames / 3});
        auto y = doStretch(in, plan, q);
        CHECK(y.ok());
        if (y.ok()) CHECK_EQ(static_cast<std::int64_t>(y.value().size()), plan.outFrames);
      }
}

CHOP_TEST(pitch_is_preserved_within_the_quality_bounds) {
  struct Case { Quality q; double cents; };
  for (const Case c : {Case{Quality::Draft, 20.0}, Case{Quality::Standard, 10.0}, Case{Quality::High, 5.0}})
    for (double hz : {440.0, 220.0})
      for (double ratio : {0.5, 0.8, 1.25, 2.0}) {
        const int sr = 48000;
        const auto x = sine(hz, 3.0, sr);
        const float* h[1];
        const Input in = mono(x, sr, h);
        auto y = doStretch(in, planFor(in.frames, ratio), c.q);
        CHECK(y.ok());
        if (!y.ok()) continue;
        const double f = frequency(y.value(), sr);
        const double err = std::fabs(1200.0 * std::log2(f / hz));
        if (err > c.cents) std::fprintf(stderr, "pitch error %.2f cents (q=%d hz=%.0f ratio=%.2f)\n", err, static_cast<int>(c.q), hz, ratio);
        CHECK(err <= c.cents);
      }
}

CHOP_TEST(anchored_events_land_exactly_and_keep_their_attack) {
  const int sr = 48000;
  std::vector<float> x(static_cast<std::size_t>(sr * 5), 0.f);
  std::vector<std::int64_t> clicks, anchors;
  for (int i = 0; i < 10; ++i) {
    const std::int64_t pos = static_cast<std::int64_t>(i) * sr / 2;
    clicks.push_back(pos);
    x[static_cast<std::size_t>(pos)] = 1.f;
    // a little decaying tone after each click so the regions contain signal
    for (int k = 1; k < 2000; ++k) x[static_cast<std::size_t>(pos + k)] = 0.3f * static_cast<float>(std::exp(-k / 500.0) * std::sin(2.0 * kPi * 300.0 * k / sr));
    if (i > 0) anchors.push_back(pos);
  }
  for (double ratio : {0.8, 1.25})
    for (Quality q : {Quality::Draft, Quality::Standard, Quality::High}) {
      const float* h[1];
      const Input in = mono(x, sr, h);
      const Plan plan = planFor(in.frames, ratio, anchors);
      auto y = doStretch(in, plan, q);
      CHECK(y.ok());
      if (!y.ok()) continue;
      for (std::int64_t pos : clicks) {
        const std::int64_t expected = mapPosition(pos, in.frames, plan.outFrames);
        std::int64_t peakAt = expected;
        float peak = 0.f;
        for (std::int64_t i = std::max<std::int64_t>(0, expected - 50); i < expected + 50; ++i)
          if (std::fabs(y.value()[static_cast<std::size_t>(i)]) > peak) {
            peak = std::fabs(y.value()[static_cast<std::size_t>(i)]);
            peakAt = i;
          }
        CHECK_EQ(peakAt, expected);
        CHECK(peak >= 0.99f);
      }
    }
}

CHOP_TEST(region_joins_are_seamless_and_energy_is_preserved) {
  const int sr = 48000;
  const auto x = sine(220.0, 2.0, sr);
  const float* h[1];
  const Input in = mono(x, sr, h);
  const Plan plan = planFor(in.frames, 1.25, {24000, 48000, 72000});
  auto y = doStretch(in, plan, Quality::Standard);
  CHECK(y.ok());
  if (!y.ok()) return;
  float own = 0.f;
  for (std::size_t i = 1; i < x.size(); ++i) own = std::max(own, std::fabs(x[i] - x[i - 1]));
  for (std::size_t b = 1; b + 1 < plan.outBounds.size(); ++b) {
    const std::size_t at = static_cast<std::size_t>(plan.outBounds[b]);
    CHECK(std::fabs(y.value()[at] - y.value()[at - 1]) <= 1.5f * own);
  }
  // white noise keeps its level
  Rng rng(5);
  std::vector<float> n(static_cast<std::size_t>(sr * 2));
  for (float& v : n) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * 0.5f;
  double inRms = 0.0;
  for (float v : n) inRms += static_cast<double>(v) * v;
  inRms = std::sqrt(inRms / static_cast<double>(n.size()));
  for (double ratio : {0.6, 1.0, 1.6})
    for (Quality q : {Quality::Draft, Quality::Standard}) {
      const float* hh[1];
      const Input nin = mono(n, sr, hh);
      auto out = doStretch(nin, planFor(nin.frames, ratio), q);
      CHECK(out.ok());
      if (!out.ok()) continue;
      double r = 0.0;
      for (float v : out.value()) r += static_cast<double>(v) * v;
      r = std::sqrt(r / static_cast<double>(out.value().size()));
      CHECK(std::fabs(20.0 * std::log10(r / inRms)) <= 1.0);
    }
}

CHOP_TEST(silence_dc_and_nonfinite_input) {
  const int sr = 48000;
  std::vector<float> zeros(static_cast<std::size_t>(sr), 0.f), dc(static_cast<std::size_t>(sr), 0.5f), bad(static_cast<std::size_t>(sr), 0.25f);
  bad[1000] = std::numeric_limits<float>::quiet_NaN();
  bad[5000] = std::numeric_limits<float>::infinity();
  for (const auto* v : {&zeros, &dc, &bad}) {
    const float* h[1];
    const Input in = mono(*v, sr, h);
    auto y = doStretch(in, planFor(in.frames, 1.3), Quality::Standard);
    CHECK(y.ok());
    if (!y.ok()) continue;
    for (float s : y.value()) CHECK(std::isfinite(s));
    if (v == &zeros) CHECK(std::all_of(y.value().begin(), y.value().end(), [](float s) { return s == 0.f; }));
    if (v == &dc)
      for (float s : y.value()) CHECK_NEAR(s, 0.5, 1e-5);
  }
}

CHOP_TEST(stereo_matches_dual_mono_and_keeps_the_phase_relation) {
  const int sr = 48000;
  const auto x = sine(330.0, 1.5, sr);
  const float* mh[1];
  const Input m = mono(x, sr, mh);
  const Plan plan = planFor(m.frames, 1.4);
  auto ym = doStretch(m, plan, Quality::Standard);
  const float* sh[2] = {x.data(), x.data()};
  Input s = m;
  s.channels = sh;
  s.numChannels = 2;
  auto ys = doStretch(s, plan, Quality::Standard);
  CHECK(ym.ok() && ys.ok());
  if (!ym.ok() || !ys.ok()) return;
  const std::size_t n = static_cast<std::size_t>(plan.outFrames);
  for (std::size_t i = 0; i < n; ++i) {
    CHECK_NEAR(ys.value()[i], ym.value()[i], 1e-5);
    CHECK_NEAR(ys.value()[n + i], ym.value()[i], 1e-5);
  }
  // different channels: a common alignment preserves the phase between them
  std::vector<float> shifted(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) shifted[i] = static_cast<float>(0.8 * std::sin(2.0 * kPi * 330.0 * static_cast<double>(i) / sr + 0.9));
  const float* ph[2] = {x.data(), shifted.data()};
  Input p = m;
  p.channels = ph;
  p.numChannels = 2;
  auto yp = doStretch(p, plan, Quality::Standard);
  CHECK(yp.ok());
  if (!yp.ok()) return;
  double xy = 0.0, xx = 0.0, yy = 0.0, ref = 0.0;
  for (std::size_t i = n / 10; i < n - n / 10; ++i) {
    const double a = yp.value()[i], b = yp.value()[n + i];
    xy += a * b;
    xx += a * a;
    yy += b * b;
  }
  double ix = 0.0, ixx = 0.0, iyy = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    ix += static_cast<double>(x[i]) * shifted[i];
    ixx += static_cast<double>(x[i]) * x[i];
    iyy += static_cast<double>(shifted[i]) * shifted[i];
  }
  ref = ix / std::sqrt(ixx * iyy);
  CHECK_NEAR(xy / std::sqrt(xx * yy), ref, 0.05);
}

CHOP_TEST(hit_mode_keeps_the_attack_and_fades_the_cut) {
  const int sr = 48000;
  std::vector<float> x(static_cast<std::size_t>(2000), 0.f);  // far shorter than three windows
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = 1.f;
  const float* h[1];
  const Input in = mono(x, sr, h);
  auto shorter = doStretch(in, planFor(in.frames, 0.6), Quality::Standard);
  CHECK(shorter.ok());
  if (shorter.ok()) {
    const auto& y = shorter.value();
    CHECK_EQ(y[0], 1.f);
    const std::size_t fade = static_cast<std::size_t>(std::llround(0.005 * sr));
    for (std::size_t i = 0; i + fade < y.size(); ++i) CHECK_EQ(y[i], 1.f);
    for (std::size_t i = y.size() - fade + 1; i < y.size(); ++i) CHECK(y[i] <= y[i - 1] + 1e-6f);
    CHECK(y.back() < 0.01f);
  }
  auto longer = doStretch(in, planFor(in.frames, 1.5), Quality::Standard);
  CHECK(longer.ok());
  if (longer.ok()) {
    CHECK_EQ(longer.value()[1999], 1.f);
    CHECK_EQ(longer.value()[2000], 0.f);
    CHECK_EQ(longer.value().back(), 0.f);
  }
}

CHOP_TEST(cancel_returns_an_error_quickly_and_runs_are_deterministic) {
  const int sr = 48000;
  const auto x = sine(200.0, 20.0, sr);
  const float* h[1];
  const Input in = mono(x, sr, h);
  const Plan plan = planFor(in.frames, 1.2);
  const auto t0 = std::chrono::steady_clock::now();
  auto cancelled = doStretch(in, plan, Quality::High, [](double f) { return f < 0.1; });
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(!cancelled.ok() && cancelled.error().code == ErrorCode::Cancelled);
  CHECK(ms < 20000.0);
  const auto s = sine(250.0, 2.0, sr);
  const float* h2[1];
  const Input small = mono(s, sr, h2);
  const Plan p2 = planFor(small.frames, 0.9, {30000});
  auto a = doStretch(small, p2, Quality::Standard);
  std::vector<float> other;
  std::thread worker([&] {
    auto b = doStretch(small, p2, Quality::Standard);
    if (b.ok()) other = b.value();
  });
  worker.join();
  auto c = doStretch(small, p2, Quality::Standard);
  CHECK(a.ok() && c.ok());
  CHECK(a.value() == c.value() && a.value() == other);
}

CHOP_TEST(invalid_requests_are_refused) {
  const int sr = 48000;
  const auto x = sine(200.0, 0.5, sr);
  const float* h[1];
  const Input in = mono(x, sr, h);
  Plan plan = planFor(in.frames, 1.1);
  plan.outFrames = in.frames * 3;  // outside 0.5..2
  plan.outBounds.back() = plan.outFrames;
  CHECK(!doStretch(in, plan, Quality::Draft).ok());
  Plan mismatch = planFor(in.frames, 1.1);
  mismatch.inFrames = in.frames + 5;
  CHECK(!doStretch(in, mismatch, Quality::Draft).ok());
  Input none = in;
  none.numChannels = 3;
  CHECK(!doStretch(none, planFor(in.frames, 1.1), Quality::Draft).ok());
}

#ifdef NDEBUG
CHOP_TEST(time_budget_smoke_for_standard_quality) {
  const int sr = 48000;
  const auto x = sine(220.0, 10.0, sr);
  const float* h[2] = {x.data(), x.data()};
  Input in;
  in.channels = h;
  in.numChannels = 2;
  in.frames = static_cast<std::int64_t>(x.size());
  in.sampleRate = sr;
  const auto t0 = std::chrono::steady_clock::now();
  auto y = doStretch(in, planFor(in.frames, 0.8, {100000, 200000, 300000}), Quality::Standard);
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK(y.ok());
  CHECK(s < 1.5);
}
#endif
