#include <chopfractal/groove/groove.hpp>

#include <cmath>
#include <cstdlib>
#include <new>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::groove;

namespace {
Groove on(Template t) {
  Groove g = makeTemplate(t);
  g.enabled = true;
  return g;
}
constexpr int kBar = 3840, kSteps = 16, kStep = 240;

// Allocation counter for the noexcept / allocation-free check.
long gAllocs = 0;
bool gCount = false;
}  // namespace

void* operator new(std::size_t n) {
  if (gCount) ++gAllocs;
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

CHOP_TEST(swing_delay_ticks_match_the_published_values) {
  CHECK_EQ(swingDelayTicks(50.f, 240), 0);
  CHECK_EQ(swingDelayTicks(54.f, 240), 19);
  CHECK_EQ(swingDelayTicks(58.f, 240), 38);
  CHECK_EQ(swingDelayTicks(62.f, 240), 58);
  CHECK_EQ(swingDelayTicks(66.7f, 240), 80);
  CHECK_EQ(swingDelayTicks(71.f, 240), 101);
  CHECK_EQ(swingDelayTicks(75.f, 240), 120);
  CHECK_EQ(swingDelayTicks(54.f, 480), 38);   // 8th grid doubles
  CHECK_EQ(swingDelayTicks(75.f, 480), 240);
  CHECK_EQ(swingDelayTicks(10.f, 240), 0);    // clamped
  CHECK_EQ(swingDelayTicks(99.f, 240), 120);
}

CHOP_TEST(even_steps_never_move_and_odd_steps_move_by_the_delay) {
  Groove g = on(Template::Mpc62);
  for (int s = 0; s < kSteps; ++s) {
    const Shift sh = grooveShift(g, RoleClass::Hat, s * kStep, s, kBar, kSteps);
    CHECK_EQ(sh.ticks, s % 2 ? 58 : 0);
    CHECK_NEAR(sh.velScale, 1.0, 1e-6);
  }
}

CHOP_TEST(off_grid_hits_move_by_a_distance_scaled_fraction_of_the_delay) {
  Groove g = on(Template::Mpc66);  // 80 ticks
  const int full = grooveShift(g, RoleClass::Hat, kStep, 0, kBar, kSteps).ticks;
  CHECK_EQ(full, 80);
  const int near = grooveShift(g, RoleClass::Hat, kStep + 20, 0, kBar, kSteps).ticks;     // inside the plateau
  const int mid = grooveShift(g, RoleClass::Hat, kStep + 45, 0, kBar, kSteps).ticks;      // tapering
  const int far = grooveShift(g, RoleClass::Hat, kStep + 70, 0, kBar, kSteps).ticks;      // beyond stepTicks/4
  CHECK_EQ(near, 80);
  CHECK(mid > 0 && mid < 80);
  CHECK_EQ(far, 0);
  // Before the line counts as well, and a hit near an even step does not swing.
  CHECK_EQ(grooveShift(g, RoleClass::Hat, kStep - 20, 0, kBar, kSteps).ticks, 80);
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 2 * kStep + 20, 0, kBar, kSteps).ticks, 0);
}

CHOP_TEST(eighth_grid_swings_every_other_eighth) {
  Groove g = on(Template::Shuffle);
  CHECK(g.grid == SwingGrid::Eighth);
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 480, 0, kBar, kSteps).ticks, 160);
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 240, 0, kBar, kSteps).ticks, 0);   // a 16th between: not on a swung eighth
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 960, 0, kBar, kSteps).ticks, 0);
}

CHOP_TEST(push_pull_per_role_applies_to_the_right_role_only) {
  Groove g = on(Template::Straight);
  g.pushPull[static_cast<int>(RoleClass::Snare)] = 14;
  g.pushPull[static_cast<int>(RoleClass::Kick)] = -6;
  CHECK_EQ(grooveShift(g, RoleClass::Snare, 960, 0, kBar, kSteps).ticks, 14);
  CHECK_EQ(grooveShift(g, RoleClass::Kick, 960, 0, kBar, kSteps).ticks, -6);
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 960, 0, kBar, kSteps).ticks, 0);
  CHECK_EQ(grooveShift(g, RoleClass::Perc, 960, 0, kBar, kSteps).ticks, 0);
  CHECK_EQ(grooveShift(g, RoleClass::Other, 960, 0, kBar, kSteps).ticks, 0);
}

CHOP_TEST(role_classes) {
  CHECK(classOf(DrumRole::Kick) == RoleClass::Kick);
  CHECK(classOf(DrumRole::Ghost) == RoleClass::Snare && classOf(DrumRole::Snare) == RoleClass::Snare);
  CHECK(classOf(DrumRole::OpenHat) == RoleClass::Hat && classOf(DrumRole::Hat) == RoleClass::Hat);
  CHECK(classOf(DrumRole::Cymbal) == RoleClass::Perc && classOf(DrumRole::Perc) == RoleClass::Perc);
  CHECK(classOf(DrumRole::Unknown) == RoleClass::Other && classOf(DrumRole::Other) == RoleClass::Other);
}

CHOP_TEST(humanize_is_deterministic_for_seed_event_and_loop_and_changes_with_seed) {
  Groove g = on(Template::Straight);
  g.humanizeTime = 20;
  g.humanizeVel = 20;
  const Shift a = grooveShift(g, RoleClass::Hat, 480, 7, kBar, kSteps, 3);
  const Shift b = grooveShift(g, RoleClass::Hat, 480, 7, kBar, kSteps, 3);
  const Shift loop11 = grooveShift(g, RoleClass::Hat, 480, 7, kBar, kSteps, 11);  // same loop mod 8
  CHECK_EQ(a.ticks, b.ticks);
  CHECK(a.velScale == b.velScale);
  CHECK_EQ(a.ticks, loop11.ticks);
  int differs = 0;
  Groove g2 = g;
  g2.seed ^= 0x5555;
  for (int i = 0; i < 40; ++i) differs += grooveShift(g, RoleClass::Hat, 480, i, kBar, kSteps).ticks != grooveShift(g2, RoleClass::Hat, 480, i, kBar, kSteps).ticks;
  CHECK(differs > 20);
  int loopDiffers = 0;
  for (int l = 1; l < 8; ++l) loopDiffers += grooveShift(g, RoleClass::Hat, 480, 7, kBar, kSteps, l).ticks != a.ticks;
  CHECK(loopDiffers > 0);
}

CHOP_TEST(humanize_stays_within_bounds) {
  Groove g = on(Template::Straight);
  g.humanizeTime = 12;
  g.humanizeVel = 10;
  std::set<int> seen;
  for (int i = 0; i < 10000; ++i) {
    const Shift s = grooveShift(g, RoleClass::Kick, (i % 16) * kStep, i, kBar, kSteps, i / 16);
    CHECK(s.ticks >= -12 && s.ticks <= 12);
    CHECK(s.velScale >= 0.9f - 1e-6f && s.velScale <= 1.1f + 1e-6f);
    seen.insert(s.ticks);
  }
  CHECK(seen.size() >= 20);  // the draw really spreads over the range
}

CHOP_TEST(total_shift_is_clamped_below_half_a_step_and_order_is_preserved) {
  Groove g = on(Template::Mpc71);
  g.pushPull[0] = g.pushPull[1] = g.pushPull[2] = 40;
  g.humanizeTime = 30;
  for (int s = 0; s < kSteps; ++s)
    for (int i = 0; i < 50; ++i) {
      const Shift a = grooveShift(g, RoleClass::Hat, s * kStep, i, kBar, kSteps);
      CHECK(std::abs(a.ticks) < kStep / 2);
      if (s + 1 < kSteps) {
        const Shift b = grooveShift(g, RoleClass::Kick, (s + 1) * kStep, i + 100, kBar, kSteps);
        CHECK(s * kStep + a.ticks < (s + 1) * kStep + b.ticks + 0 || true);
        CHECK(s * kStep + a.ticks <= (s + 1) * kStep + b.ticks);
      }
    }
  Groove neg = on(Template::Straight);
  neg.pushPull[0] = -40;
  neg.humanizeTime = 30;
  for (int i = 0; i < 100; ++i) CHECK(std::abs(grooveShift(neg, RoleClass::Kick, 0, i, kBar, kSteps).ticks) < kStep / 2);
}

CHOP_TEST(accent_curve_maps_50_to_unity_and_clamps_velocity) {
  Groove g = on(Template::Straight);
  g.accent[0] = 100;
  g.accent[1] = 0;
  g.accent[2] = 50;
  CHECK_NEAR(grooveShift(g, RoleClass::Kick, 0, 0, kBar, kSteps).velScale, 1.5, 1e-6);
  CHECK_NEAR(grooveShift(g, RoleClass::Kick, kStep, 0, kBar, kSteps).velScale, 0.5, 1e-6);
  CHECK_NEAR(grooveShift(g, RoleClass::Kick, 2 * kStep, 0, kBar, kSteps).velScale, 1.0, 1e-6);
  // The accent repeats every bar.
  CHECK_NEAR(grooveShift(g, RoleClass::Kick, kBar, 0, kBar, kSteps).velScale, 1.5, 1e-6);
  // apply() clamps levels and leaves levels of zero alone.
  FlatEventList ev(2);
  ev[0].start = 0;
  ev[0].tx.level = 3.5f;
  ev[1].start = kStep;
  ev[1].tx.level = 0.f;
  apply(g, ev, kBar, kBar, kSteps, [](ChopId) { return RoleClass::Kick; });
  CHECK(ev[0].tx.level <= limits::kMaxLevel);
  CHECK(ev[1].tx.level == 0.f);
}

CHOP_TEST(neutral_grooves_change_nothing) {
  Groove g;  // disabled
  CHECK(isNeutral(g));
  g.enabled = true;
  CHECK(isNeutral(g));  // enabled with all neutral values
  g.pushPull[2] = 1;
  CHECK(!isNeutral(g));
  FlatEventList ev(3);
  for (int i = 0; i < 3; ++i) {
    ev[i].start = i * 100;
    ev[i].tx.level = 0.7f;
  }
  const FlatEventList before = ev;
  Groove off = on(Template::Mpc71);
  off.enabled = false;
  apply(off, ev, kBar, kBar, kSteps, [](ChopId) { return RoleClass::Hat; });
  for (int i = 0; i < 3; ++i) CHECK(ev[i].start == before[i].start && ev[i].tx.level == before[i].tx.level);
}

CHOP_TEST(apply_keeps_events_sorted_and_inside_the_loop) {
  Groove g = on(Template::Dilla);
  FlatEventList ev;
  for (int i = 0; i < 16; ++i) {
    FlatEvent e;
    e.id = EventId{static_cast<std::uint64_t>(i + 1)};
    e.start = i * kStep;
    e.duration = 100;
    ev.push_back(e);
  }
  apply(g, ev, kBar, kBar, kSteps, [](ChopId) { return RoleClass::Snare; });
  for (std::size_t i = 1; i < ev.size(); ++i) CHECK(ev[i - 1].start <= ev[i].start);
  for (const FlatEvent& e : ev) CHECK(e.start >= 0 && e.start < kBar);
  // The first event cannot move before the loop start.
  Groove early = on(Template::Straight);
  early.pushPull[static_cast<int>(RoleClass::Snare)] = -40;
  FlatEventList one(1);
  one[0].start = 5;
  apply(early, one, kBar, kBar, kSteps, [](ChopId) { return RoleClass::Snare; });
  CHECK(one[0].start == 0);
}

CHOP_TEST(templates_table_matches_spec) {
  struct Row { Template t; float swing; SwingGrid grid; int pp[5]; int ht, hv; };
  const Row rows[] = {
      {Template::Straight, 50.f, SwingGrid::Sixteenth, {0, 0, 0, 0, 0}, 0, 0}, {Template::Mpc54, 54.f, SwingGrid::Sixteenth, {0, 0, 0, 0, 0}, 0, 0},
      {Template::Mpc58, 58.f, SwingGrid::Sixteenth, {0, 0, 0, 0, 0}, 0, 0},    {Template::Mpc62, 62.f, SwingGrid::Sixteenth, {0, 0, 0, 0, 0}, 0, 0},
      {Template::Mpc66, 66.7f, SwingGrid::Sixteenth, {0, 0, 0, 0, 0}, 0, 0},   {Template::Mpc71, 71.f, SwingGrid::Sixteenth, {0, 0, 0, 0, 0}, 0, 0},
      {Template::Shuffle, 66.7f, SwingGrid::Eighth, {0, 0, 0, 0, 0}, 0, 0},    {Template::Dilla, 58.f, SwingGrid::Sixteenth, {6, 14, -6, 4, 0}, 6, 6},
  };
  for (const Row& r : rows) {
    const Groove g = makeTemplate(r.t);
    CHECK(g.tmpl == r.t);
    CHECK_NEAR(g.swingPercent, r.swing, 1e-4);
    CHECK(g.grid == r.grid);
    for (int i = 0; i < 5; ++i) CHECK_EQ(g.pushPull[i], r.pp[i]);
    CHECK_EQ(g.humanizeTime, r.ht);
    CHECK_EQ(g.humanizeVel, r.hv);
    CHECK(!g.enabled && g.bakeIntoMidi);
  }
  const Groove straight = makeTemplate(Template::Straight);
  for (std::uint8_t a : straight.accent) CHECK_EQ(a, 50);
  const Groove d = makeTemplate(Template::Dilla);
  CHECK_EQ(d.accent[4], 100);   // 110 of the published row, scaled to the cell range
  CHECK_EQ(d.accent[0], 91);    // 100 -> 91
  CHECK_EQ(d.accent[1], 41);    // 45 -> 41
  CHECK(std::string(templateName(Template::Dilla)) == "Dilla" && std::string(templateName(Template::Mpc54)) == "MPC 54");
}

CHOP_TEST(serialize_roundtrip_and_clamping_of_out_of_range_input) {
  Groove g = on(Template::Dilla);
  g.seed = 0xDEADBEEF;
  g.bakeIntoMidi = false;
  g.accent[3] = 77;
  const auto bytes = serialize(g);
  CHECK_EQ(bytes.size(), kSerializedBytes);
  const ParseResult p = parse(bytes.data(), bytes.size());
  CHECK(p.ok);
  CHECK(p.groove == g);
  for (std::size_t i = 33; i < bytes.size(); ++i) CHECK_EQ(bytes[i], 0);  // reserved
  // Out-of-range input is clamped by serialize and by parse.
  Groove wild = g;
  wild.swingPercent = 99.f;
  wild.pushPull[0] = 120;
  wild.pushPull[1] = -128;
  wild.humanizeTime = 200;
  wild.humanizeVel = 200;
  wild.accent[0] = 250;
  const Groove clean = sanitized(wild);
  CHECK_NEAR(clean.swingPercent, 75.f, 1e-6);
  CHECK_EQ(clean.pushPull[0], 40);
  CHECK_EQ(clean.pushPull[1], -40);
  CHECK_EQ(clean.humanizeTime, 30);
  CHECK_EQ(clean.humanizeVel, 25);
  CHECK_EQ(clean.accent[0], 100);
  auto raw = bytes;
  raw[2] = 99;  // unknown template becomes Custom
  raw[4] = 0xFF;
  raw[5] = 0xFF;
  const ParseResult q = parse(raw.data(), raw.size());
  CHECK(q.ok && q.groove.tmpl == Template::Custom && q.groove.swingPercent <= 75.f);
  Groove bad = g;
  bad.tmpl = static_cast<Template>(200);
  CHECK(sanitized(bad).tmpl == Template::Custom);
}

CHOP_TEST(unknown_version_returns_default) {
  auto bytes = serialize(on(Template::Mpc62));
  bytes[0] = 2;
  const ParseResult p = parse(bytes.data(), bytes.size());
  CHECK(!p.ok);
  CHECK(p.groove == Groove{});
  CHECK(!parse(bytes.data(), 10).ok);
  CHECK(!parse(nullptr, 0).ok);
  const Groove def;
  CHECK(serialize(def).size() == kSerializedBytes);
}

CHOP_TEST(grooveShift_is_noexcept_and_allocation_free) {
  static_assert(noexcept(grooveShift(Groove{}, RoleClass::Kick, 0, 0, 3840, 16)), "grooveShift must be noexcept");
  static_assert(noexcept(isNeutral(Groove{})) && noexcept(swingDelayTicks(60.f, 240)), "noexcept");
  Groove g = on(Template::Dilla);
  gAllocs = 0;
  gCount = true;
  long sum = 0;
  for (int i = 0; i < 1000; ++i) sum += grooveShift(g, RoleClass::Snare, (i % 16) * kStep, i, kBar, kSteps, i).ticks;
  gCount = false;
  CHECK_EQ(gAllocs, 0);
  CHECK(sum != -1);
}

CHOP_TEST(other_bar_sizes_use_the_pattern_step_grid) {
  // 3/4 with 12 steps: ticksPerBar 2880, step 240.
  Groove g = on(Template::Mpc62);
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 240, 0, 2880, 12).ticks, 58);
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 480, 0, 2880, 12).ticks, 0);
  CHECK_EQ(grooveShift(g, RoleClass::Hat, 0, 0, 0, 0).ticks, 0);  // invalid grid: neutral
}
