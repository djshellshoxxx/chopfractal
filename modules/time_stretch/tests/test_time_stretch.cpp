#include "chop_test.hpp"

#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/time_stretch/stretch.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

using chopfractal::ErrorCode;
using chopfractal::Rng;
using namespace chopfractal::stretch;

namespace {

constexpr double kPi = 3.14159265358979323846;

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

const Quality kAllQualities[] = {Quality::Draft, Quality::Standard, Quality::High};
const char* qname(Quality q) { return q == Quality::Draft ? "Draft" : q == Quality::Standard ? "Standard" : "High"; }

std::vector<float> sine(double freq, std::int64_t frames, int sr, double amp = 0.8) {
  std::vector<float> v(static_cast<std::size_t>(frames));
  for (std::int64_t i = 0; i < frames; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * freq * static_cast<double>(i) / sr));
  return v;
}

std::int64_t outFor(std::int64_t in, double ratio) { return std::llround(static_cast<double>(in) * ratio); }

struct Run {
  bool ok = false;
  std::vector<float> data;
  Plan plan;
};

Run run(const std::vector<std::vector<float>>& ch, int sr, double ratio, Quality q, const std::vector<std::int64_t>& anchors = {}) {
  Run r;
  const std::int64_t in = static_cast<std::int64_t>(ch[0].size());
  auto plan = makePlan(in, outFor(in, ratio), anchors);
  CHECK(plan.ok());
  if (!plan.ok()) return r;
  std::vector<const float*> ptrs;
  for (const auto& c : ch) ptrs.push_back(c.data());
  Input input{ptrs.data(), static_cast<int>(ch.size()), in, sr};
  auto out = stretch(input, plan.value(), q);
  CHECK(out.ok());
  if (!out.ok()) return r;
  r.ok = true;
  r.data = std::move(out.value());
  r.plan = plan.value();
  return r;
}

// Frequency from linearly interpolated positive-going zero crossings over the middle 80%.
double zeroCrossingFreq(const float* x, std::int64_t n, int sr) {
  const std::int64_t lo = n / 10, hi = n - n / 10;
  double first = -1, last = -1;
  int count = 0;
  for (std::int64_t i = lo; i + 1 < hi; ++i) {
    if (x[i] < 0.f && x[i + 1] >= 0.f) {
      const double t = static_cast<double>(i) + static_cast<double>(-x[i]) / (static_cast<double>(x[i + 1]) - static_cast<double>(x[i]));
      if (first < 0) first = t;
      last = t;
      ++count;
    }
  }
  if (count < 2) return 0;
  return (count - 1) * sr / (last - first);
}

// Period from the autocorrelation peak near `periods` periods, parabolic interpolation.
double autocorrFreq(const float* x, std::int64_t n, int sr, double f0, int periods) {
  const std::int64_t lo = n / 10, len = n * 6 / 10;
  const int center = static_cast<int>(std::lround(periods * sr / f0));
  auto ac = [&](int lag) {
    double s = 0;
    for (std::int64_t i = lo; i < lo + len; ++i) s += static_cast<double>(x[i]) * x[i + lag];
    return s;
  };
  int best = center;
  double bv = -1e30;
  for (int lag = center - 12; lag <= center + 12; ++lag) {
    const double v = ac(lag);
    if (v > bv) {
      bv = v;
      best = lag;
    }
  }
  const double a = ac(best - 1), b = ac(best), c = ac(best + 1);
  const double denom = a - 2 * b + c;
  const double off = denom != 0 ? 0.5 * (a - c) / denom : 0.0;
  return periods * sr / (best + off);
}

double cents(double fOut, double fIn) { return 1200.0 * std::log2(fOut / fIn); }

double rms(const float* x, std::int64_t n) {
  double s = 0;
  for (std::int64_t i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(n));
}

}  // namespace

CHOP_TEST(output_length_is_exact) {
  const double ratios[] = {0.5, 0.75, 1.0, 1.25, 2.0};
  const int rates[] = {44100, 48000, 96000};
  for (int sr : rates)
    for (double ratio : ratios)
      for (Quality q : kAllQualities) {
        const std::int64_t in = sr / 2;  // 0.5 s keeps the High preset quick
        const Run r = run({sine(330.0, in, sr)}, sr, ratio, q, {in / 3});
        CHECK(r.ok);
        if (!r.ok) continue;
        CHECK_EQ(static_cast<std::int64_t>(r.data.size()), outFor(in, ratio));
        CHECK_EQ(r.plan.outFrames, outFor(in, ratio));
        for (std::size_t i = 0; i + 1 < r.plan.outBounds.size(); ++i) CHECK(r.plan.outBounds[i + 1] - r.plan.outBounds[i] >= 1);
      }
}

CHOP_TEST(pitch_is_preserved) {
  const int sr = 48000;
  const std::int64_t frames = 4 * sr;
  const double ratios[] = {0.5, 0.8, 1.25, 2.0};
  const double bound[] = {20.0, 10.0, 5.0};
  for (double f : {440.0, 220.0}) {
    const auto sig = sine(f, frames, sr);
    for (int qi = 0; qi < 3; ++qi)
      for (double ratio : ratios) {
        const Run r = run({sig}, sr, ratio, kAllQualities[qi]);
        CHECK(r.ok);
        if (!r.ok) continue;
        const double fo = zeroCrossingFreq(r.data.data(), static_cast<std::int64_t>(r.data.size()), sr);
        const double err = std::fabs(cents(fo, f));
        if (err > bound[qi]) std::fprintf(stderr, "pitch %.0f Hz %s ratio %.2f: %.2f cents\n", f, qname(kAllQualities[qi]), ratio, err);
        CHECK(err <= bound[qi]);
      }
  }
  // Harmonic tone: f0 200 Hz with 3 partials, f0 measured by autocorrelation.
  std::vector<float> h(static_cast<std::size_t>(frames));
  for (std::int64_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / sr;
    h[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2 * kPi * 200 * t) + 0.3 * std::sin(2 * kPi * 400 * t + 0.7) + 0.2 * std::sin(2 * kPi * 600 * t + 1.9));
  }
  for (int qi = 0; qi < 3; ++qi)
    for (double ratio : ratios) {
      const Run r = run({h}, sr, ratio, kAllQualities[qi]);
      CHECK(r.ok);
      if (!r.ok) continue;
      const double fo = autocorrFreq(r.data.data(), static_cast<std::int64_t>(r.data.size()), sr, 200.0, 5);
      const double err = std::fabs(cents(fo, 200.0));
      if (err > bound[qi]) std::fprintf(stderr, "harmonic %s ratio %.2f: %.2f cents\n", qname(kAllQualities[qi]), ratio, err);
      CHECK(err <= bound[qi]);
    }
}

CHOP_TEST(duration_error_of_events_is_zero) {
  const int sr = 48000;
  const std::int64_t spacing = sr / 2;
  const std::int64_t frames = spacing * 10;
  std::vector<float> sig(static_cast<std::size_t>(frames), 0.f);
  std::vector<std::int64_t> pos;
  for (int i = 0; i < 10; ++i) {
    pos.push_back(i * spacing + (i == 0 ? 0 : 7 * i));  // not all on a round grid
    sig[static_cast<std::size_t>(pos.back())] = 1.0f;
  }
  for (double ratio : {0.8, 1.25, 2.0, 0.5})
    for (Quality q : kAllQualities) {
      const Run r = run({sig}, sr, ratio, q, pos);
      CHECK(r.ok);
      if (!r.ok) continue;
      const std::int64_t out = r.plan.outFrames;
      for (int i = 0; i < 10; ++i) {
        const std::int64_t expect = mapPosition(pos[static_cast<std::size_t>(i)], frames, out);
        const std::int64_t end = i == 9 ? out : mapPosition(pos[static_cast<std::size_t>(i + 1)], frames, out);
        std::int64_t peak = expect;
        float pv = 0.f;
        for (std::int64_t k = expect; k < end; ++k)
          if (std::fabs(r.data[static_cast<std::size_t>(k)]) > pv) {
            pv = std::fabs(r.data[static_cast<std::size_t>(k)]);
            peak = k;
          }
        CHECK_EQ(peak, expect);
        CHECK(pv >= 0.99f);
      }
    }
}

CHOP_TEST(map_position_has_no_drift) {
  const std::int64_t pairs[][2] = {{11520000, 23040000}, {11520000, 5760000}, {11520000, 9216000}, {480000, 384001}, {96000, 120001}};
  bool anyDrift = false;
  for (const auto& pr : pairs) {
    const std::int64_t in = pr[0], out = pr[1];
    CHECK_EQ(mapPosition(0, in, out), 0);
    CHECK_EQ(mapPosition(in, in, out), out);
    std::int64_t prev = -1;
    std::int64_t cumulative = 0;  // naive scheme: add the rounded scaled gap each time
    std::int64_t prevPos = 0;
    for (int i = 0; i <= 1000; ++i) {
      const std::int64_t p = in / 1000 * i + (i % 7) * 13;
      const std::int64_t pp = std::min(p, in);
      const std::int64_t m = mapPosition(pp, in, out);
      const double exact = static_cast<double>(pp) * static_cast<double>(out) / static_cast<double>(in);
      CHECK(std::fabs(static_cast<double>(m) - exact) <= 0.5 + 1e-6);
      if (pp >= prevPos) CHECK(m >= prev);
      prev = m;
      prevPos = pp;
    }
    // Documented counter-example: scaling each gap independently drifts, the rule does not.
    std::int64_t last = 0;
    double maxDrift = 0;
    for (int i = 1; i <= 1000; ++i) {
      const std::int64_t p = std::min<std::int64_t>(in, 1001 * i);
      cumulative += static_cast<std::int64_t>(std::floor(static_cast<double>(p - last) * out / in));
      last = p;
      maxDrift = std::max(maxDrift, std::fabs(static_cast<double>(cumulative) - static_cast<double>(p) * out / in));
    }
    anyDrift = anyDrift || maxDrift > 0.5;
    CHECK_EQ(mapLength(0, in, out), 0);
  }
  CHECK(anyDrift);
}

CHOP_TEST(region_joins_are_seamless) {
  const int sr = 48000;
  const std::int64_t frames = 2 * sr;
  const auto sig = sine(440.0, frames, sr);
  double own = 0;
  for (std::size_t i = 1; i < sig.size(); ++i) own = std::max(own, static_cast<double>(std::fabs(sig[i] - sig[i - 1])));
  for (Quality q : kAllQualities) {
    const Run r = run({sig}, sr, 1.25, q, {30000, 61000, 77777});
    CHECK(r.ok);
    if (!r.ok) continue;
    for (std::size_t i = 1; i + 1 < r.plan.outBounds.size(); ++i) {
      const std::size_t b = static_cast<std::size_t>(r.plan.outBounds[i]);
      CHECK(std::fabs(r.data[b] - r.data[b - 1]) <= 1.5 * own + 1e-6);
    }
  }
}

CHOP_TEST(energy_is_preserved) {
  const int sr = 48000;
  const std::int64_t frames = 2 * sr;
  Rng rng(12345);
  std::vector<float> noise(static_cast<std::size_t>(frames));
  for (float& s : noise) s = static_cast<float>(rng.uniform01() * 2.0 - 1.0);
  const double in = rms(noise.data(), frames);
  for (double ratio : {0.5, 0.75, 1.25, 2.0})
    for (Quality q : kAllQualities) {
      const Run r = run({noise}, sr, ratio, q);
      CHECK(r.ok);
      if (!r.ok) continue;
      const double db = 20.0 * std::log10(rms(r.data.data(), static_cast<std::int64_t>(r.data.size())) / in);
      // White noise is the worst case: uncorrelated overlapped frames add in power, costing up to about 1.25 dB.
      if (std::fabs(db) > 1.5) std::fprintf(stderr, "energy %s ratio %.2f: %.2f dB\n", qname(q), ratio, db);
      CHECK(std::fabs(db) <= 1.5);
    }
}

CHOP_TEST(silence_and_dc_and_nonfinite_inputs) {
  const int sr = 48000;
  const std::int64_t frames = sr;
  for (Quality q : kAllQualities) {
    const Run z = run({std::vector<float>(static_cast<std::size_t>(frames), 0.f)}, sr, 1.3, q);
    CHECK(z.ok);
    for (float s : z.data) CHECK(s == 0.f);
    const Run dc = run({std::vector<float>(static_cast<std::size_t>(frames), 0.5f)}, sr, 0.7, q);
    CHECK(dc.ok);
    for (float s : dc.data) CHECK(std::fabs(s - 0.5f) <= 1e-6f);
    auto bad = sine(300.0, frames, sr);
    bad[100] = std::nanf("");
    bad[20000] = INFINITY;
    bad[40000] = -INFINITY;
    const Run nf = run({bad}, sr, 1.5, q, {30000});
    CHECK(nf.ok);
    for (float s : nf.data) CHECK(std::isfinite(s));
  }
}

CHOP_TEST(mono_and_stereo_identical_to_dual_mono) {
  const int sr = 48000;
  const std::int64_t frames = sr;
  Rng rng(99);
  std::vector<float> m(static_cast<std::size_t>(frames));
  for (std::int64_t i = 0; i < frames; ++i)
    m[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2 * kPi * 180 * i / sr) + 0.2 * std::sin(2 * kPi * 777 * i / sr) + 0.05 * (rng.uniform01() - 0.5));
  for (Quality q : kAllQualities) {
    const Run mono = run({m}, sr, 1.25, q, {20000});
    const Run st = run({m, m}, sr, 1.25, q, {20000});
    CHECK(mono.ok && st.ok);
    if (!mono.ok || !st.ok) continue;
    const std::size_t n = mono.data.size();
    CHECK_EQ(st.data.size(), 2 * n);
    CHECK(std::equal(mono.data.begin(), mono.data.end(), st.data.begin()));
    CHECK(std::equal(mono.data.begin(), mono.data.end(), st.data.begin() + static_cast<std::ptrdiff_t>(n)));
  }
  // Panned sine pair: the cross-channel relation survives (common alignment).
  const auto l = sine(250.0, frames, sr, 0.8);
  auto rr = sine(250.0, frames, sr, 0.4);
  for (Quality q : kAllQualities) {
    const Run st = run({l, rr}, sr, 0.8, q);
    CHECK(st.ok);
    if (!st.ok) continue;
    const std::size_t n = st.data.size() / 2;
    double sxy = 0, sxx = 0, syy = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const double a = st.data[i], b = st.data[n + i];
      sxy += a * b;
      sxx += a * a;
      syy += b * b;
    }
    CHECK(sxy / std::sqrt(sxx * syy) >= 0.99);
  }
}

CHOP_TEST(hit_mode_keeps_attack_and_fades_cut) {
  const int sr = 48000;
  Rng rng(5);
  std::vector<float> x(2000);
  for (float& s : x) s = static_cast<float>(rng.uniform01() * 0.5 + 0.25);  // positive, so a fade is monotonic
  // Lout < Lin
  {
    const Run r = run({x}, sr, 0.75, Quality::Standard);  // 1500 frames, one region shorter than 3N
    CHECK(r.ok);
    const std::int64_t lout = static_cast<std::int64_t>(r.data.size());
    const std::int64_t fade = std::min<std::int64_t>(std::llround(0.005 * sr), lout / 2);
    for (std::int64_t i = 0; i < lout - fade; ++i) CHECK(r.data[static_cast<std::size_t>(i)] == x[static_cast<std::size_t>(i)]);
    // The fade gain (output / input) never increases and ends at zero.
    double prev = 2;
    for (std::int64_t i = lout - fade; i < lout; ++i) {
      const double g = static_cast<double>(r.data[static_cast<std::size_t>(i)]) / x[static_cast<std::size_t>(i)];
      CHECK(g <= prev + 1e-6);
      prev = g;
    }
    CHECK(std::fabs(r.data.back()) < 1e-6f);
  }
  // Lout > Lin
  {
    const Run r = run({x}, sr, 1.4, Quality::Standard);
    CHECK(r.ok);
    for (std::size_t i = 0; i < x.size(); ++i) CHECK(r.data[i] == x[i]);
    for (std::size_t i = x.size(); i < r.data.size(); ++i) CHECK(r.data[i] == 0.f);
  }
}

CHOP_TEST(cancel_returns_error_quickly) {
  const int sr = 48000;
  const std::int64_t frames = 20 * sr;
  Rng rng(3);
  std::vector<float> a(static_cast<std::size_t>(frames)), b(static_cast<std::size_t>(frames));
  for (std::int64_t i = 0; i < frames; ++i) {
    a[static_cast<std::size_t>(i)] = static_cast<float>(rng.uniform01() - 0.5);
    b[static_cast<std::size_t>(i)] = static_cast<float>(rng.uniform01() - 0.5);
  }
  const float* ptrs[2] = {a.data(), b.data()};
  auto plan = makePlan(frames, outFor(frames, 1.25), {});
  CHECK(plan.ok());
  Input in{ptrs, 2, frames, sr};
  std::chrono::steady_clock::time_point cancelAt;
  bool cancelled = false;
  int calls = 0;
  auto out = stretch(in, plan.value(), Quality::High, [&](double f) {
    ++calls;
    if (f >= 0.1 && !cancelled) {
      cancelled = true;
      cancelAt = std::chrono::steady_clock::now();
      return false;
    }
    return true;
  });
  const auto done = std::chrono::steady_clock::now();
  CHECK(cancelled);
  CHECK(!out.ok());
  if (!out.ok()) CHECK(out.error().code == ErrorCode::Cancelled);
  CHECK(calls >= 2);
  const double ms = std::chrono::duration<double, std::milli>(done - cancelAt).count();
  if (ms > 100.0) std::fprintf(stderr, "cancel latency %.1f ms\n", ms);
  CHECK(ms <= (kSanitized ? 1000.0 : 100.0));
  // Cancelling on the very first call also works.
  auto first = stretch(in, plan.value(), Quality::Draft, [](double) { return false; });
  CHECK(!first.ok() && first.error().code == ErrorCode::Cancelled);
}

CHOP_TEST(progress_is_monotonic_and_ends_at_one) {
  const int sr = 48000;
  const auto sig = sine(200.0, 2 * sr, sr);
  const float* p[1] = {sig.data()};
  auto plan = makePlan(2 * sr, 2 * sr * 5 / 4, {30000, 60000});
  CHECK(plan.ok());
  double last = -1;
  bool mono = true;
  auto out = stretch(Input{p, 1, 2 * sr, sr}, plan.value(), Quality::Standard, [&](double f) {
    if (f < last) mono = false;
    last = f;
    return true;
  });
  CHECK(out.ok());
  CHECK(mono);
  CHECK_NEAR(last, 1.0, 1e-9);
}

CHOP_TEST(deterministic_repeat_and_block_free) {
  const int sr = 48000;
  Rng rng(21);
  std::vector<float> a(static_cast<std::size_t>(sr)), b(static_cast<std::size_t>(sr));
  for (std::int64_t i = 0; i < sr; ++i) {
    a[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(2 * kPi * 330 * i / sr) + 0.1 * (rng.uniform01() - 0.5));
    b[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(2 * kPi * 123 * i / sr));
  }
  const Run r1 = run({a, b}, sr, 1.3, Quality::Standard, {15000, 30000});
  const Run r2 = run({a, b}, sr, 1.3, Quality::Standard, {15000, 30000});
  CHECK(r1.ok && r2.ok);
  CHECK(r1.data == r2.data);
  // Two threads, independent instances, same bytes.
  Run t1, t2;
  std::thread th1([&] { t1 = run({a, b}, sr, 1.3, Quality::Standard, {15000, 30000}); });
  std::thread th2([&] { t2 = run({b, a}, sr, 0.9, Quality::High, {5000}); });
  th1.join();
  th2.join();
  CHECK(t1.ok && t2.ok);
  CHECK(t1.data == r1.data);
  const Run t2ref = run({b, a}, sr, 0.9, Quality::High, {5000});
  CHECK(t2.data == t2ref.data);
}

CHOP_TEST(ratio_limits) {
  CHECK(!checkRatio(0.4999).ok());
  CHECK(!checkRatio(2.0001).ok());
  CHECK(checkRatio(0.5).ok());
  CHECK(checkRatio(2.0).ok());
  CHECK(!checkRatio(std::nan("")).ok());
  CHECK_EQ(checkRatio(0.4999).error().code, ErrorCode::OutOfRange);
  CHECK_NEAR(lengthRatio(96.0, 120.0), 0.8, 1e-12);
  CHECK(!makePlan(1000, 499, {}).ok());
  CHECK(!makePlan(1000, 2001, {}).ok());
  CHECK(makePlan(1000, 500, {}).ok());
  CHECK(makePlan(1000, 2000, {}).ok());
  CHECK(!makePlan(0, 10, {}).ok());
}

CHOP_TEST(make_plan_adds_ends_sorts_and_dedupes) {
  auto p = makePlan(1000, 800, {500, 0, 250, 250, 1000, -5, 2000, 750});
  CHECK(p.ok());
  const std::vector<std::int64_t> in = {0, 250, 500, 750, 1000};
  CHECK(p.value().inBounds == in);
  CHECK_EQ(p.value().outBounds.size(), in.size());
  for (std::size_t i = 0; i < in.size(); ++i) CHECK_EQ(p.value().outBounds[i], mapPosition(in[i], 1000, 800));
  // Anchors that collapse onto the same output frame are dropped, not duplicated.
  auto q = makePlan(1000, 500, {1, 2, 3});
  CHECK(q.ok());
  for (std::size_t i = 1; i < q.value().outBounds.size(); ++i) CHECK(q.value().outBounds[i] > q.value().outBounds[i - 1]);
}

CHOP_TEST(stretch_rejects_bad_arguments) {
  const std::vector<float> s(5000, 0.1f);
  const float* p[1] = {s.data()};
  auto plan = makePlan(5000, 6000, {});
  CHECK(plan.ok());
  CHECK(!stretch(Input{p, 1, 4000, 48000}, plan.value(), Quality::Draft).ok());       // length mismatch
  CHECK(!stretch(Input{p, 3, 5000, 48000}, plan.value(), Quality::Draft).ok());       // channels
  CHECK(!stretch(Input{p, 1, 5000, 100}, plan.value(), Quality::Draft).ok());         // sample rate
  CHECK(!stretch(Input{nullptr, 1, 5000, 48000}, plan.value(), Quality::Draft).ok());
  Plan bad = plan.value();
  bad.outBounds = {0, 7000};
  bad.inBounds = {0, 5000};
  CHECK(!stretch(Input{p, 1, 5000, 48000}, bad, Quality::Draft).ok());
  Plan unsorted = plan.value();
  unsorted.inBounds = {0, 3000, 2000, 5000};
  unsorted.outBounds = {0, 3600, 2400, 6000};
  CHECK(!stretch(Input{p, 1, 5000, 48000}, unsorted, Quality::Draft).ok());
}

CHOP_TEST(time_budget_smoke) {
#ifdef NDEBUG
  if (kSanitized) return;
  const int sr = 48000;
  const std::int64_t frames = 10 * sr;
  Rng rng(8);
  std::vector<float> a(static_cast<std::size_t>(frames)), b(static_cast<std::size_t>(frames));
  for (std::int64_t i = 0; i < frames; ++i) {
    a[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(2 * kPi * 220 * i / sr) + 0.2 * (rng.uniform01() - 0.5));
    b[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(2 * kPi * 330 * i / sr) + 0.2 * (rng.uniform01() - 0.5));
  }
  const auto t0 = std::chrono::steady_clock::now();
  const Run r = run({a, b}, sr, 1.25, Quality::Standard, {100000, 200000, 300000});
  const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK(r.ok);
  std::printf("Standard 10 s stereo: %.2f s\n", sec);
  CHECK(sec < 1.5);
#endif
}
