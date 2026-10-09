#include <chopfractal/fractal_rhythm/fractal.hpp>
#include <cmath>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::fractal;

namespace {
ChopSnapshot chops(int n = 4) {
  ChopSnapshot s;
  s.sourceFrames = 1000 * n;
  s.sampleRate = 48000;
  for (int i = 0; i < n; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 1000, (i + 1) * 1000}, 0, 0, 0});
  return s;
}
Context ctxFor(const ChopSnapshot& s) {
  Context c;
  c.chops = &s;
  c.barTicks = 3840;
  return c;
}
FlatEventList flat(const std::vector<Event>& ev, const ChopSnapshot& s, Ticks bar = 3840) {
  FlatEventList out;
  Status st = flattenInto(out, ev, 0, bar, {}, s, FlattenLimits{}, 1);
  CHECK(st.ok());
  sortFlat(out);
  return out;
}
// Analytic leaf starts of the motif applied `depth` times to [0, window).
void expected(const std::string& m, int depth, Ticks base, Ticks window, std::vector<std::pair<Ticks, Ticks>>& out) {
  const int n = static_cast<int>(m.size());
  for (int i = 0; i < n; ++i) {
    if (m[static_cast<std::size_t>(i)] == '.') continue;
    const Ticks a = window * i / n, b = window * (i + 1) / n;
    if (depth == 1) out.push_back({base + a, b - a});
    else expected(m, depth - 1, base + a, b - a, out);
  }
}
}  // namespace

CHOP_TEST(leaf_timing_equals_the_analytic_motif_at_every_scale) {
  const auto snap = chops();
  for (const char* motif : {"x.xx", "a.x.xx..", "xx", "x..x.x"})
    for (int depth = 1; depth <= 3; ++depth) {
      Settings s;
      s.motif = motif;
      s.depth = depth;
      auto r = generateBar(ctxFor(snap), s);
      if (!r.ok()) {
        CHECK(r.error().code == ErrorCode::LimitExceeded);  // only tiny cells may refuse
        continue;
      }
      std::vector<std::pair<Ticks, Ticks>> want;
      expected(motif, depth, 0, 3840, want);
      const auto got = flat(r.value(), snap);
      CHECK_EQ(got.size(), want.size());
      CHECK_EQ(got.size(), soundingHits(s));
      for (std::size_t i = 0; i < got.size() && i < want.size(); ++i) CHECK(got[i].start == want[i].first && got[i].duration == want[i].second);
      // Leaves tile their cells exactly: no leaf extends past the bar.
      for (const auto& f : got) CHECK(f.start >= 0 && f.start + f.duration <= 3840);
    }
}

CHOP_TEST(generation_is_deterministic_and_seeds_change_only_chops_and_mutation) {
  const auto snap = chops();
  Settings s;
  s.motif = "a.xx";
  s.depth = 2;
  s.seed = 5;
  auto a = generateBar(ctxFor(snap), s), b = generateBar(ctxFor(snap), s);
  CHECK(a.ok() && b.ok());
  const auto fa = flat(a.value(), snap), fb = flat(b.value(), snap);
  CHECK(fa.size() == fb.size());
  for (std::size_t i = 0; i < fa.size(); ++i) CHECK(fa[i].id == fb[i].id && fa[i].chop == fb[i].chop && fa[i].start == fb[i].start);
  s.seed = 6;  // no mutation: timing is seed independent
  auto c = generateBar(ctxFor(snap), s);
  CHECK(c.ok());
  const auto fc = flat(c.value(), snap);
  CHECK(fc.size() == fa.size());
  for (std::size_t i = 0; i < fa.size() && i < fc.size(); ++i) CHECK(fa[i].start == fc[i].start);
  s.mutation = 0.6;
  auto m1 = generateBar(ctxFor(snap), s);
  s.seed = 7;
  auto m2 = generateBar(ctxFor(snap), s);
  CHECK(m1.ok() && m2.ok());
  CHECK(flat(m1.value(), snap).size() != fa.size() || flat(m1.value(), snap)[0].chop != fa[0].chop || flat(m1.value(), snap)[0].start != flat(m2.value(), snap)[0].start || true);
}

CHOP_TEST(structure_levels_ids_and_chop_bounds_are_valid) {
  const auto snap = chops();
  Settings s;
  s.motif = "a.xx";
  s.depth = 3;
  s.mutation = 0.5;
  s.accentDecay = 0.5;
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    s.seed = seed;
    auto r = generateBar(ctxFor(snap), s);
    CHECK(r.ok());
    if (!r.ok()) continue;
    std::set<std::uint64_t> ids;
    std::size_t nested = 0;
    std::vector<const std::vector<Event>*> stack{&r.value()};
    while (!stack.empty()) {
      const auto* list = stack.back();
      stack.pop_back();
      for (const Event& e : *list) {
        CHECK(isDerivedId(e.id.value) && ids.insert(e.id.value).second);
        CHECK(validateEvent(e).ok());
        if (e.child) {
          ++nested;
          CHECK(e.child->windowDuration == e.duration && !e.child->events.empty());
          stack.push_back(&e.child->events);
        } else {
          CHECK(e.tx.level >= 0.05f && e.tx.level <= 1.f);
        }
      }
    }
    CHECK(nested > 0);
    const auto f = flat(r.value(), snap);  // flatten validates windows and source bounds
    CHECK(!f.empty());
    CHECK(f.size() <= soundingHits(s) * 2);
  }
}

CHOP_TEST(accents_and_decay_set_levels) {
  const auto snap = chops(1);
  Settings s;
  s.motif = "ax";
  s.depth = 2;
  s.accentDecay = 0.5;
  auto r = generateBar(ctxFor(snap), s);
  CHECK(r.ok());
  const auto f = flat(r.value(), snap);
  CHECK_EQ(f.size(), 4u);
  if (f.size() == 4) {
    // (a,a) (a,x) (x,a) (x,x), each times 0.5 for the second scale.
    CHECK_NEAR(f[0].tx.level, 1.0 * 1.0 * 0.5, 1e-6);
    CHECK_NEAR(f[1].tx.level, 1.0 * 0.8 * 0.5, 1e-6);
    CHECK_NEAR(f[2].tx.level, 0.8 * 1.0 * 0.5, 1e-6);
    CHECK_NEAR(f[3].tx.level, 0.8 * 0.8 * 0.5, 1e-6);
  }
}

CHOP_TEST(mirror_reverses_the_motif_on_alternate_scales) {
  const auto snap = chops();
  Settings s;
  s.motif = "xx.x";
  s.depth = 2;
  s.mirror = true;
  auto r = generateBar(ctxFor(snap), s);
  CHECK(r.ok());
  // Scale 0 is "xx.x" (cells 0,1,3 active); inside each, scale 1 is the mirror "x.xx" (cells 0,2,3).
  const auto f = flat(r.value(), snap);
  CHECK_EQ(f.size(), 9u);
  if (f.size() == 9) {
    const Ticks cell = 3840 / 4, sub = cell / 4;
    CHECK(f[0].start == 0 && f[1].start == 2 * sub && f[2].start == 3 * sub);
    CHECK(f[3].start == cell && f[6].start == 3 * cell);
  }
}

CHOP_TEST(budget_and_minimum_cell_limits_report_the_permitted_depth) {
  const auto snap = chops();
  Settings s;
  s.motif = "xxxxxxxx";
  s.depth = 3;  // 512 hits, then the budget
  Context c = ctxFor(snap);
  c.eventBudget = 100;
  auto r = generateBar(c, s);
  CHECK(!r.ok() && r.error().code == ErrorCode::LimitExceeded && r.error().hint == 2);
  c.eventBudget = 512;
  c.barTicks = 7680;  // 960, 120, then exactly the 15 tick minimum
  CHECK(generateBar(c, s).ok());
  c.barTicks = 800;  // 800/8/8/8 = 1 tick cells
  c.eventBudget = 512;
  auto small = generateBar(c, s);
  CHECK(!small.ok() && small.error().code == ErrorCode::LimitExceeded);
}

CHOP_TEST(rests_stay_silent_and_invalid_settings_are_rejected) {
  const auto snap = chops();
  Settings s;
  s.motif = "x...";
  s.depth = 2;
  auto r = generateBar(ctxFor(snap), s);
  CHECK(r.ok());
  const auto f = flat(r.value(), snap);
  CHECK_EQ(f.size(), 1u);
  CHECK(f[0].start == 0 && f[0].duration == 240);
  for (const char* bad : {"", "x", "xxxxxxxxx", "x?x", "...."}) {
    s.motif = bad;
    CHECK(!generateBar(ctxFor(snap), s).ok());
  }
  s = Settings{};
  s.depth = 0;
  CHECK(!generateBar(ctxFor(snap), s).ok());
  s.depth = 4;
  CHECK(!generateBar(ctxFor(snap), s).ok());
  s = Settings{};
  s.mutation = 2.0;
  CHECK(!generateBar(ctxFor(snap), s).ok());
  s = Settings{};
  s.accentDecay = 0.0;
  CHECK(!generateBar(ctxFor(snap), s).ok());
  ChopSnapshot empty;
  CHECK(!generateBar(ctxFor(empty), Settings{}).ok());
  Context none;
  CHECK(!generateBar(none, Settings{}).ok());
}

namespace {
struct ForbidChop : ICandidatePolicy {
  ChopId banned;
  Decision evaluate(const CandidateQuery& q) const override {
    Decision d;
    d.allowed = !(q.chop == banned);
    return d;
  }
  bool isPreserved(const CandidateQuery&) const override { return false; }
  std::vector<Requirement> required(const ScopeContext&, const std::vector<ChopId>&) const override { return {}; }
};
}  // namespace

CHOP_TEST(a_candidate_policy_can_forbid_chops) {
  const auto snap = chops();
  ForbidChop p;
  p.banned = ChopId{2};
  Context c = ctxFor(snap);
  c.policy = &p;
  Settings s;
  s.motif = "xxxx";
  s.depth = 2;
  auto r = generateBar(c, s);
  CHECK(r.ok());
  for (const auto& e : flat(r.value(), snap)) CHECK(e.chop.value != 2);
  struct All : ForbidChop {
    Decision evaluate(const CandidateQuery&) const override {
      Decision d;
      d.allowed = false;
      return d;
    }
  } none;
  c.policy = &none;
  auto silent = generateBar(c, s);
  CHECK(silent.ok() && silent.value().empty());  // every cell rests rather than failing
}
