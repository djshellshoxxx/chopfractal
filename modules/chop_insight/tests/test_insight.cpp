#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/chop_insight/insight.hpp>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::insight;

namespace {
constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> kick() {
  std::vector<float> v(9600);
  for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(std::sin(2 * kPi * 60.0 * static_cast<double>(i) / kRate) * std::exp(-static_cast<double>(i) / 2000.0));
  return v;
}
std::vector<float> noiseBurst(std::size_t n, double tau, bool differenced, double tone, std::uint64_t seed) {
  Rng r(seed);
  std::vector<float> v(n);
  double prev = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double w = r.uniform01() * 2 - 1;
    const double x = differenced ? w - prev : w;
    prev = w;
    const double env = std::exp(-static_cast<double>(i) / tau);
    v[i] = static_cast<float>(env * (0.5 * x + tone * std::sin(2 * kPi * 200.0 * static_cast<double>(i) / kRate)));
  }
  return v;
}
Role roleOf(const std::vector<float>& v, double* conf = nullptr) {
  const float* ch[1] = {v.data()};
  SourceView s{ch, 1, static_cast<std::int64_t>(v.size()), kRate};
  auto f = analyzeChop(s, {0, s.frames});
  CHECK(f.ok());
  return f.ok() ? classify(f.value(), conf) : Role::Other;
}
}  // namespace

CHOP_TEST(synthetic_drum_hits_are_classified) {
  double c = 0;
  CHECK(roleOf(kick(), &c) == Role::Kick && c > 0.5);
  CHECK(roleOf(noiseBurst(2400, 500, true, 0, 1)) == Role::Hat);                // 50 ms bright burst
  CHECK(roleOf(noiseBurst(48000, 9000, true, 0, 2)) == Role::Cymbal);          // long bright wash
  CHECK(roleOf(noiseBurst(9600, 2400, false, 0.5, 3)) == Role::Snare);         // tone + noise body
}

CHOP_TEST(mixed_loops_are_classified_per_chop) {
  std::vector<float> loop;
  auto add = [&](const std::vector<float>& v) { loop.insert(loop.end(), v.begin(), v.end()); };
  add(kick());
  const std::size_t b1 = loop.size();
  add(noiseBurst(9600, 2400, false, 0.5, 4));
  const std::size_t b2 = loop.size();
  add(noiseBurst(2400, 500, true, 0, 5));
  const float* ch[1] = {loop.data()};
  SourceView s{ch, 1, static_cast<std::int64_t>(loop.size()), kRate};
  ChopSnapshot snap;
  snap.sourceFrames = s.frames;
  snap.sampleRate = kRate;
  snap.chops = {{ChopId{1}, {0, static_cast<std::int64_t>(b1)}, 0, 0, 0},
                {ChopId{2}, {static_cast<std::int64_t>(b1), static_cast<std::int64_t>(b2)}, 0, 0, 0},
                {ChopId{3}, {static_cast<std::int64_t>(b2), s.frames}, 0, 0, 0}};
  auto sug = suggestRoles(s, snap);
  CHECK_EQ(sug.size(), 3u);
  if (sug.size() == 3) {
    CHECK(sug[0].role == Role::Kick && sug[1].role == Role::Snare && sug[2].role == Role::Hat);
    CHECK(sug[0].chop.value == 1 && !sug[0].reason.empty());
  }
  CHECK(suggestRoles(s, snap)[1].confidence == sug[1].confidence);  // deterministic
}

CHOP_TEST(degenerate_input_never_crashes) {
  std::vector<float> silence(1000, 0.f), tiny(10, 0.5f);
  const float* ch[1] = {silence.data()};
  SourceView s{ch, 1, 1000, kRate};
  CHECK(!analyzeChop(s, {0, 1000}).ok());       // silent
  CHECK(!analyzeChop(s, {500, 400}).ok());      // empty range
  CHECK(!analyzeChop(s, {-1, 100}).ok());
  CHECK(!analyzeChop(s, {0, 2000}).ok());       // past the end
  CHECK(!analyzeChop(SourceView{}, {0, 10}).ok());
  const float* ct[1] = {tiny.data()};
  SourceView t{ct, 1, 10, kRate};
  auto f = analyzeChop(t, {0, 10});
  CHECK(f.ok());
  double c = 1;
  if (f.ok()) CHECK(classify(f.value(), &c) == Role::Other && c < 0.5);  // 0.2 ms: too short
  ChopSnapshot snap;
  snap.chops = {{ChopId{1}, {0, 1000}, 0, 0, 0}};
  auto sug = suggestRoles(s, snap);
  CHECK(sug.size() == 1 && sug[0].role == Role::Other && sug[0].confidence < 0.2);
}

CHOP_TEST(loop_suggestions_match_known_lengths_and_host_tempo) {
  auto l = suggestLoop(8.0);
  CHECK(!l.empty());
  if (!l.empty()) {
    CHECK(l[0].beats == 16 && l[0].bars == 4);
    CHECK_NEAR(l[0].bpm, 120.0, 1e-9);
    CHECK(l[0].note == "4 bars at 120.0 BPM");
  }
  CHECK_EQ(l.size(), 2u);  // 60 and 120; 240 and 30 BPM are outside 60..200
  auto h = suggestLoop(8.0, 62.0);
  CHECK(!h.empty() && h[0].beats == 8);          // host tempo prefers the 60 BPM reading
  double sum = 0;
  for (const auto& x : l) sum += x.confidence;
  CHECK_NEAR(sum, 1.0, 1e-9);
  CHECK(suggestLoop(0).empty() && suggestLoop(-1).empty() && suggestLoop(std::nan("")).empty() && suggestLoop(1e9).empty());
  auto one = suggestLoop(2.0);  // 4 beats at 120 BPM, 8 beats at 240 is out of range
  CHECK(one.size() == 1 && one[0].bars == 1 && one[0].note == "1 bar at 120.0 BPM");
}
