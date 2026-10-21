// Unit tests: one or more per rule of research sections 4 and 6 as implemented by the planner.
#include <chopfractal/break_engine/break_engine.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::breakeng;

namespace {
PlanSettings base(Profile p = Profile::Jungle, std::uint64_t seed = 1) {
  PlanSettings s;
  s.options.profile = p;
  s.seed = seed;
  return s;
}
bool snareAt(const BarPlan& b, int step) {
  for (const Hit& h : b.hits)
    if (h.role == DrumRole::Snare && h.gesture == Gesture::None && h.step == step) return true;
  return false;
}
int count(const BarPlan& b, DrumRole r) {
  int n = 0;
  for (const Hit& h : b.hits) n += h.role == r;
  return n;
}
bool hasGesture(const BarPlan& b, Gesture g) { return std::find(b.gestures.begin(), b.gestures.end(), g) != b.gestures.end(); }
}  // namespace

// ---- helpers and tables ----
CHOP_TEST(rules_phrase_slots) {
  CHECK_EQ(phraseSlot(0, 1), 0);
  CHECK_EQ(phraseSlot(0, 2), 0);
  CHECK_EQ(phraseSlot(1, 2), 3);
  for (int b = 0; b < 8; ++b) CHECK_EQ(phraseSlot(b, 8), b % 4);
  CHECK_EQ(phraseSlot(3, 4), 3);
}

CHOP_TEST(rules_velocity_level_roundtrip) {
  for (int v = 0; v <= 127; ++v) CHECK_EQ(levelToVelocity(velocityToLevel(v)), v);
  CHECK_EQ(levelToVelocity(2.0), 127);
  CHECK_EQ(levelToVelocity(-1.0), 0);
  CHECK_NEAR(velocityToLevel(127), 1.0, 1e-12);
}

CHOP_TEST(rules_ms_to_ticks) {
  CHECK_EQ(msToTicks(0.0, 120.0), 0);
  CHECK_EQ(msToTicks(500.0, 120.0), kTicksPerQuarter);  // one beat at 120 bpm
  CHECK_EQ(msToTicks(-500.0, 120.0), -kTicksPerQuarter);
  CHECK_EQ(msToTicks(10.0, 0.0), msToTicks(10.0, 120.0));  // 0 = default tempo
  CHECK(msToTicks(10.0, 60.0) < msToTicks(10.0, 240.0));
}

CHOP_TEST(rules_profile_table) {
  CHECK_EQ(kProfileCount, 6);
  std::set<std::string> names;
  for (int i = 0; i < kProfileCount; ++i) {
    const ProfileInfo& p = profileInfo(static_cast<Profile>(i));
    names.insert(p.name);
    CHECK(p.densityLo <= p.densityHi);
    CHECK(p.swingLo <= p.swingHi);
    CHECK(p.anchorFloor >= 0.5 && p.anchorFloor <= 1.0);
    CHECK(p.strictness >= 0.0 && p.strictness <= 1.0);
    CHECK(std::string(profileName(static_cast<Profile>(i))) == p.name);
  }
  CHECK_EQ(names.size(), 6u);
  // Research 6.5: soft anchors only for breakcore and IDM; glitch profiles exempt from the event cap.
  CHECK(!profileInfo(Profile::Jungle).softAnchors && profileInfo(Profile::Breakcore).softAnchors && profileInfo(Profile::IdmGlitch).softAnchors);
  CHECK(profileInfo(Profile::Breakcore).anchorFloor >= 0.7 && profileInfo(Profile::IdmGlitch).anchorFloor >= 0.5);
}

CHOP_TEST(rules_options_validation) {
  Options o;
  CHECK(valid(o));
  o.groove = 1.1;
  CHECK(!valid(o));
  o = Options{};
  o.humanize = -0.1;
  CHECK(!valid(o));
  o = Options{};
  o.tempoHint = 5.0;
  CHECK(!valid(o));
  o.tempoHint = 174.0;
  CHECK(valid(o));
  o = Options{};
  o.profile = static_cast<Profile>(99);
  CHECK(!valid(o));
  o = Options{};
  o.phraseStrictness = 1.5;
  CHECK(!valid(o));
}

// ---- anchors (6.1 steps 1, 5, 13; 6.5 protection) ----
CHOP_TEST(rules_anchor_probability_modes) {
  Options o;
  o.profile = Profile::Jungle;
  CHECK(anchorProbability(o, false, false) >= 0.95);
  CHECK(anchorProbability(o, true, false) >= 0.95);
  CHECK(anchorProbability(o, false, true) >= 0.95);
  o.anchors = AnchorMode::Free;
  CHECK(anchorProbability(o, false, false) <= 0.55);
  o.anchors = AnchorMode::Strict;
  o.profile = Profile::IdmGlitch;
  CHECK(anchorProbability(o, false, false) >= 0.95);  // strict wins over the soft profile
  o.anchors = AnchorMode::Profile;
  const double idm0 = anchorProbability(o, false, false);
  CHECK(idm0 >= 0.5 && idm0 < 0.95);
  o.chaos = 1.0;  // chaos lowers soft anchors towards the floor
  CHECK(anchorProbability(o, false, false) <= idm0);
  CHECK(anchorProbability(o, false, false) >= profileInfo(Profile::IdmGlitch).anchorFloor - 1e-12);
}

CHOP_TEST(rules_strict_anchors_always_present_in_strict_profiles) {
  int bars = 0, snare4 = 0;
  for (Profile p : {Profile::AmenFunk, Profile::Jungle, Profile::DnbRolling, Profile::HipHop})
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
      PlanSettings s = base(p, seed);
      s.options.chaos = 1.0;  // chaos never removes anchors of a strict profile
      s.options.glitch = 1.0;
      s.allow.retrigger = true;
      for (const BarPlan& b : planPattern(s)) {
        // The backbeat is present with p = 0.97 / 0.95 (research 6.1); a bar always has some kick and some snare.
        ++bars;
        snare4 += snareAt(b, 4);
        bool kick = false, snare = false;
        for (const Hit& h : b.hits) {
          kick |= h.role == DrumRole::Kick;
          snare |= h.role == DrumRole::Snare;
        }
        CHECK(kick);
        CHECK(snare);
      }
    }
  CHECK(snare4 >= bars * 94 / 100);
}

CHOP_TEST(rules_strict_mode_forces_anchors_even_in_soft_profiles) {
  int bars = 0, s4 = 0, s12 = 0, k1 = 0;
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    PlanSettings s = base(Profile::Breakcore, seed);
    s.options.anchors = AnchorMode::Strict;
    for (const BarPlan& b : planPattern(s)) {
      ++bars;
      s4 += snareAt(b, 4);
      s12 += snareAt(b, 12);
      for (const Hit& h : b.hits) CHECK(!(h.anchor && h.step == 14));  // strict never displaces the backbeat
      for (const Hit& h : b.hits) k1 += h.role == DrumRole::Kick && h.step == 0;
    }
  }
  CHECK(s4 >= bars * 94 / 100 && s12 >= bars * 92 / 100 && k1 >= bars * 92 / 100);
}

CHOP_TEST(rules_free_mode_can_drop_anchors) {
  int missing = 0;
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    PlanSettings s = base(Profile::Jungle, seed);
    s.options.anchors = AnchorMode::Free;
    for (const BarPlan& b : planPattern(s)) missing += !snareAt(b, 4);
  }
  CHECK(missing > 50);
}

CHOP_TEST(rules_displaced_backbeat_only_late_and_never_strict) {
  int displaced = 0, early = 0;
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    PlanSettings s = base(Profile::AmenFunk, seed);
    const auto plans = planPattern(s);
    for (const BarPlan& b : plans) {
      const bool d = snareAt(b, 14) && !snareAt(b, 12);
      displaced += d;
      if (d && b.slot < 2) ++early;
    }
    s.options.anchors = AnchorMode::Strict;
    for (const BarPlan& b : planPattern(s)) CHECK(!(snareAt(b, 14) && !snareAt(b, 12)));
  }
  CHECK(displaced > 0);
  CHECK_EQ(early, 0);
}

CHOP_TEST(rules_no_kick_on_backbeat_steps_ever) {
  for (int pi = 0; pi < kProfileCount; ++pi)
    for (std::uint64_t seed = 0; seed < 100; ++seed)
      for (const BarPlan& b : planPattern(base(static_cast<Profile>(pi), seed)))
        for (const Hit& h : b.hits) CHECK(!(h.role == DrumRole::Kick && h.gesture == Gesture::None && (h.step == 4 || h.step == 12)));
}

CHOP_TEST(rules_anchor_cells) {
  CHECK(isAnchorCell(DrumRole::Kick, 0, false));
  CHECK(!isAnchorCell(DrumRole::Kick, 8, false));
  CHECK(isAnchorCell(DrumRole::Snare, 4, false));
  CHECK(isAnchorCell(DrumRole::Snare, 12, false));
  CHECK(!isAnchorCell(DrumRole::Snare, 14, false));
  CHECK(isAnchorCell(DrumRole::Snare, 14, true));
  CHECK(!isAnchorCell(DrumRole::Ghost, 4, false));
}

// ---- probability table (6.1) and phrase modifiers (6.2) ----
CHOP_TEST(rules_cell_probabilities) {
  const PlanSettings s = base();
  CHECK(cellProbability(s, 0, DrumRole::Kick, 0) >= 0.9);
  CHECK(cellProbability(s, 0, DrumRole::Snare, 4) >= 0.9);
  CHECK_NEAR(cellProbability(s, 0, DrumRole::Kick, 4), 0.0, 1e-12);
  CHECK_NEAR(cellProbability(s, 0, DrumRole::Kick, 12), 0.0, 1e-12);
  CHECK_NEAR(cellProbability(s, 0, DrumRole::Snare, 0), 0.0, 1e-12);
  for (int st : {0, 4, 8, 12}) CHECK_NEAR(cellProbability(s, 0, DrumRole::Ghost, st), 0.0, 1e-12);  // no ghosts on strong steps
  CHECK(cellProbability(s, 0, DrumRole::Ghost, 5) > cellProbability(s, 0, DrumRole::Ghost, 1));
  CHECK_NEAR(cellProbability(s, 0, DrumRole::Hat, 2), 0.0, 1e-12);  // roles without a table
  CHECK_NEAR(cellProbability(s, 0, DrumRole::Kick, 16), 0.0, 1e-12);
  for (int st = 0; st < 16; ++st)
    for (int slot = 0; slot < 4; ++slot)
      for (DrumRole r : {DrumRole::Kick, DrumRole::Snare, DrumRole::Ghost}) {
        const double p = cellProbability(s, slot, r, st);
        CHECK(p >= 0.0 && p <= 1.0);
      }
}

CHOP_TEST(rules_phrase_modifiers) {
  const PlanSettings s = base();
  // Kicks other than the downbeat rise through the phrase (0.6, 0.8, 1.2, 1.4).
  const double k0 = cellProbability(s, 0, DrumRole::Kick, 10), k3 = cellProbability(s, 3, DrumRole::Kick, 10);
  CHECK(k3 > k0 * 2.0);
  CHECK(cellProbability(s, 3, DrumRole::Ghost, 5) > cellProbability(s, 0, DrumRole::Ghost, 5));
  // Fills never in bar 1, most likely in bar 4.
  CHECK_NEAR(fillProbability(s, 0), 0.0, 1e-12);
  CHECK(fillProbability(s, 3) > fillProbability(s, 1));
  CHECK(fillProbability(s, 1) > fillProbability(s, 2));
  PlanSettings none = s;
  none.options.fill = 0.0;
  CHECK_NEAR(fillProbability(none, 3), 0.0, 1e-12);
}

CHOP_TEST(rules_density_maps_into_band) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    PlanSettings lo = base(static_cast<Profile>(pi)), hi = lo;
    lo.density = 0.0;
    hi.density = 1.0;
    double mlo = 0, mhi = 0;
    for (int st = 0; st < 16; ++st)
      for (DrumRole r : {DrumRole::Kick, DrumRole::Snare, DrumRole::Ghost}) {
        mlo += cellProbability(lo, 1, r, st);
        mhi += cellProbability(hi, 1, r, st);
      }
    CHECK(mhi > mlo);
  }
}

CHOP_TEST(rules_swing_range_and_groove_scaling) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    PlanSettings s = base(static_cast<Profile>(pi));
    const ProfileInfo& info = profileInfo(s.options.profile);
    s.swing = 0.0;
    const double lo = effectiveSwing(s);
    s.swing = 1.0;
    const double hi = effectiveSwing(s);
    CHECK(lo >= info.swingLo - 1e-9 && hi <= info.swingHi * 1.0 + 1e-9 && hi >= lo);
    s.options.groove = 0.0;
    CHECK_NEAR(effectiveSwing(s), 0.0, 1e-12);
    s.options.groove = 1.0;
    CHECK(effectiveSwing(s) >= hi);
    CHECK(effectiveSwing(s) <= 1.0);
  }
}

CHOP_TEST(rules_phrase_strictness) {
  PlanSettings s = base(Profile::Jungle);
  const double def = effectivePhraseStrictness(s);
  CHECK(def > 0.5);
  s.variationPhrase = 1.0;
  CHECK(effectivePhraseStrictness(s) < def);
  s.variationPhrase = 0.5;
  s.options.phraseStrictness = 0.1;
  CHECK_NEAR(effectivePhraseStrictness(s), 0.1, 1e-12);
  CHECK(effectivePhraseStrictness(base(Profile::Breakcore)) < def);
}

// ---- collision and density rules (6.4) ----
CHOP_TEST(rules_can_add_one_hit_per_family_and_step) {
  const PlanSettings s = base();
  std::vector<Cell> present{{DrumRole::Hat, 2, false}};
  CHECK(!canAdd(s, present, {DrumRole::Hat, 2, false}, false));
  CHECK(!canAdd(s, present, {DrumRole::OpenHat, 2, false}, false));
  CHECK(canAdd(s, present, {DrumRole::Kick, 2, false}, false));
  CHECK(canAdd(s, present, {DrumRole::Hat, 3, false}, false));
}

CHOP_TEST(rules_can_add_kick_snare_collision) {
  PlanSettings s = base();
  std::vector<Cell> present{{DrumRole::Kick, 8, false}};
  CHECK(!canAdd(s, present, {DrumRole::Snare, 8, false}, false));
  CHECK(!canAdd(s, present, {DrumRole::Ghost, 8, false}, false));
  CHECK(!canAdd(s, present, {DrumRole::Snare, 8, false}, true));  // even an anchor never collides
  present = {{DrumRole::Snare, 9, false}};
  CHECK(!canAdd(s, present, {DrumRole::Kick, 9, false}, false));
  s.options.kickSnareLayer = true;
  CHECK(canAdd(s, present, {DrumRole::Kick, 9, false}, false));
}

CHOP_TEST(rules_can_add_low_adjacency) {
  const PlanSettings s = base(Profile::Jungle);
  // Two low hits already within three steps: a third is refused (research 6.4 rule 2).
  std::vector<Cell> present{{DrumRole::Kick, 2, false}, {DrumRole::Snare, 4, false}};
  CHECK(!canAdd(s, present, {DrumRole::Kick, 3, false}, false));
  CHECK(canAdd(s, present, {DrumRole::Kick, 3, false}, true));   // anchors are exempt
  CHECK(canAdd(s, present, {DrumRole::Kick, 9, false}, false));
  const PlanSettings glitch = base(Profile::Breakcore);
  CHECK(canAdd(glitch, present, {DrumRole::Kick, 3, false}, false));  // glitch profiles relax it
}

CHOP_TEST(rules_can_add_ghost_caps) {
  const PlanSettings s = base(Profile::Jungle);
  std::vector<Cell> present{{DrumRole::Ghost, 1, false}, {DrumRole::Ghost, 3, false}};
  CHECK(!canAdd(s, present, {DrumRole::Ghost, 2, false}, false));  // 3 per beat and 3 consecutive
  present = {{DrumRole::Ghost, 5, false}, {DrumRole::Ghost, 6, false}};
  CHECK(!canAdd(s, present, {DrumRole::Ghost, 7, false}, false));  // three consecutive snare-family steps
  present = {{DrumRole::Ghost, 5, false}, {DrumRole::Ghost, 7, false}};
  CHECK(!canAdd(s, present, {DrumRole::Ghost, 6, false}, false));
  present = {{DrumRole::Ghost, 1, false}, {DrumRole::Ghost, 3, false}, {DrumRole::Ghost, 5, false}, {DrumRole::Ghost, 7, false}, {DrumRole::Ghost, 9, false}, {DrumRole::Ghost, 11, false}};
  CHECK(!canAdd(s, present, {DrumRole::Ghost, 13, false}, false));  // 6 per bar
  present = {{DrumRole::Ghost, 1, false}};
  CHECK(canAdd(s, present, {DrumRole::Ghost, 7, false}, false));
}

CHOP_TEST(rules_can_add_open_hat) {
  const PlanSettings s = base(Profile::Jungle);
  std::vector<Cell> present{{DrumRole::OpenHat, 6, false}};
  CHECK(!canAdd(s, present, {DrumRole::OpenHat, 14, false}, false));  // one per bar
  const PlanSettings glitch = base(Profile::IdmGlitch);
  CHECK(canAdd(glitch, present, {DrumRole::OpenHat, 14, false}, false));
  CHECK(!canAdd(glitch, present, {DrumRole::OpenHat, 7, false}, false));  // never two within two steps
}

CHOP_TEST(rules_can_add_event_cap) {
  const PlanSettings s = base(Profile::AmenFunk);
  std::vector<Cell> present;
  for (int i = 0; i < 12; ++i) present.push_back({i % 2 ? DrumRole::Perc : DrumRole::Cymbal, i, false});
  CHECK(!canAdd(s, present, {DrumRole::Perc, 14, false}, false));
  CHECK(canAdd(s, present, {DrumRole::Hat, 14, false}, false));   // hats do not count
  CHECK(canAdd(s, present, {DrumRole::Perc, 14, true}, true));    // anchors and gesture hits are exempt
}

CHOP_TEST(rules_plans_respect_can_add_everywhere) {
  // Every produced bar satisfies the one-hit-per-family and no-collision invariants (checked on the nominal grid).
  for (int pi = 0; pi < kProfileCount; ++pi)
    for (std::uint64_t seed = 0; seed < 60; ++seed) {
      PlanSettings s = base(static_cast<Profile>(pi), seed);
      s.allow.retrigger = s.allow.reverse = s.allow.glide = true;
      s.options.glitch = 1.0;
      for (const BarPlan& b : planPattern(s)) {
        std::set<std::pair<int, int>> seen;
        for (const Hit& h : b.hits) {
          if (h.gesture != Gesture::None) continue;
          const int fam = h.role == DrumRole::Kick ? 0 : (h.role == DrumRole::Snare || h.role == DrumRole::Ghost) ? 1 : (h.role == DrumRole::Hat || h.role == DrumRole::OpenHat) ? 2 : 3;
          CHECK(seen.insert({h.step, fam}).second);
        }
        CHECK(count(b, DrumRole::OpenHat) <= 1 || profileInfo(s.options.profile).glitchProfile);
      }
    }
}

// ---- velocity hierarchy (4.1) ----
CHOP_TEST(rules_velocity_hierarchy_in_ranges) {
  for (std::uint64_t seed = 0; seed < 60; ++seed)
    for (const BarPlan& b : planPattern(base(Profile::Jungle, seed)))
      for (const Hit& h : b.hits) {
        CHECK(h.velocity >= 1 && h.velocity <= 127);
        if (h.gesture != Gesture::None) continue;
        if (h.role == DrumRole::Ghost) CHECK(h.velocity >= 20 && h.velocity <= 80);
        if (h.role == DrumRole::Snare && h.anchor) CHECK(h.velocity >= 100);
        if (h.role == DrumRole::Kick && h.step == 0) CHECK(h.velocity >= 105);
        if (h.role == DrumRole::Hat) CHECK(h.velocity < 111);
      }
}

CHOP_TEST(rules_backbeat_lands_harder_than_ghosts_per_bar) {
  for (std::uint64_t seed = 0; seed < 60; ++seed)
    for (const BarPlan& b : planPattern(base(Profile::Jungle, seed))) {
      int minBackbeat = 128, maxGhost = 0;
      for (const Hit& h : b.hits) {
        if (h.gesture != Gesture::None) continue;
        if (h.role == DrumRole::Snare && h.anchor) minBackbeat = std::min(minBackbeat, h.velocity);
        if (h.role == DrumRole::Ghost) maxGhost = std::max(maxGhost, h.velocity);
      }
      if (minBackbeat < 128 && maxGhost > 0) CHECK(minBackbeat > maxGhost);
    }
}

// ---- roles / availability ----
CHOP_TEST(rules_availability_flags) {
  PlanSettings s = base(Profile::Jungle);
  s.avail.ghost = false;
  s.avail.hat = false;
  s.avail.openHat = false;
  s.avail.cymbal = false;
  s.avail.perc = false;
  for (std::uint64_t seed = 0; seed < 60; ++seed) {
    s.seed = seed;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits) CHECK(h.role == DrumRole::Kick || h.role == DrumRole::Snare);
  }
  // With everything available all roles appear over enough seeds.
  std::set<int> roles;
  s = base(Profile::DnbRolling);
  s.options.fill = 1.0;
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    s.seed = seed;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits) roles.insert(static_cast<int>(h.role));
  }
  CHECK(roles.count(static_cast<int>(DrumRole::Kick)) && roles.count(static_cast<int>(DrumRole::Snare)) && roles.count(static_cast<int>(DrumRole::Ghost)));
  CHECK(roles.count(static_cast<int>(DrumRole::Hat)) && roles.count(static_cast<int>(DrumRole::OpenHat)) && roles.count(static_cast<int>(DrumRole::Cymbal)));
}

CHOP_TEST(rules_rolling_backbeat_puts_ghosts_after_backbeats) {
  int rolling = 0, standard = 0;
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    PlanSettings s = base(Profile::HipHop, seed);
    s.options.backbeat = Backbeat::Rolling;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits) rolling += h.role == DrumRole::Ghost && (h.step == 5 || h.step == 13);
    s.options.backbeat = Backbeat::Standard;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits) standard += h.role == DrumRole::Ghost && (h.step == 5 || h.step == 13);
  }
  CHECK(rolling * 10 > standard * 14);
}

CHOP_TEST(rules_hat_pattern_modes) {
  // DnB rolling plays 16ths nearly always, funk mostly 8ths (profile table hat16).
  auto hatSteps = [](Profile p) {
    double odd = 0, bars = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed)
      for (const BarPlan& b : planPattern(base(p, seed))) {
        ++bars;
        for (const Hit& h : b.hits) odd += (h.role == DrumRole::Hat && h.step % 2 == 1);
      }
    return odd / bars;
  };
  CHECK(hatSteps(Profile::DnbRolling) > hatSteps(Profile::Jungle));
  CHECK(hatSteps(Profile::DnbRolling) > 3.0);
}

// ---- fills and gestures (6.3) ----
CHOP_TEST(rules_fill_gestures_only_at_phrase_end_bias) {
  int bar4 = 0, bar1 = 0;
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    PlanSettings s = base(Profile::Jungle, seed);
    s.options.fill = 1.0;
    const auto plans = planPattern(s);
    for (Gesture g : {Gesture::Roll16, Gesture::Roll32, Gesture::DoubleSnare, Gesture::TomRun, Gesture::GateDrop}) {
      bar4 += hasGesture(plans[3], g);
      bar1 += hasGesture(plans[0], g);
    }
  }
  CHECK_EQ(bar1, 0);  // fill chance in the first bar is zero
  CHECK(bar4 > 50);
}

CHOP_TEST(rules_gate_drop_only_in_bar_four_and_only_hats_remain) {
  int seen = 0;
  for (std::uint64_t seed = 0; seed < 600; ++seed) {
    PlanSettings s = base(Profile::Jungle, seed);
    s.options.fill = 1.0;
    const auto plans = planPattern(s);
    for (std::size_t i = 0; i < 3; ++i) CHECK(!hasGesture(plans[i], Gesture::GateDrop));
    if (hasGesture(plans[3], Gesture::GateDrop)) {
      ++seen;
      for (const Hit& h : plans[3].hits)
        if (h.step >= 14 && !h.anchor && h.gesture == Gesture::None && h.role != DrumRole::Cymbal) CHECK(false);
    }
  }
  CHECK(seen > 0);
}

CHOP_TEST(rules_glitch_gestures_gated_by_allow_flags) {
  int stutter = 0, rev = 0, tape = 0;
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    PlanSettings s = base(Profile::Breakcore, seed);
    s.options.glitch = 1.0;
    for (const BarPlan& b : planPattern(s)) {
      for (Gesture g : {Gesture::Stutter8, Gesture::Stutter16, Gesture::Stutter32, Gesture::SpeedRamp, Gesture::ReverseSwell, Gesture::TapeStop}) CHECK(!hasGesture(b, g));
    }
    s.allow.retrigger = true;
    s.allow.reverse = true;
    s.allow.glide = true;
    for (const BarPlan& b : planPattern(s)) {
      stutter += hasGesture(b, Gesture::Stutter16) || hasGesture(b, Gesture::Stutter8) || hasGesture(b, Gesture::Stutter32) || hasGesture(b, Gesture::SpeedRamp);
      rev += hasGesture(b, Gesture::ReverseSwell);
      tape += hasGesture(b, Gesture::TapeStop);
    }
  }
  CHECK(stutter > 20 && rev > 5 && tape > 2);
}

CHOP_TEST(rules_glitch_scales_with_option_and_profile) {
  auto gestures = [](Profile p, double glitch, double chaos) {
    int n = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
      PlanSettings s = base(p, seed);
      s.options.glitch = glitch;
      s.options.chaos = chaos;
      s.allow.retrigger = s.allow.reverse = s.allow.glide = true;
      for (const BarPlan& b : planPattern(s))
        for (Gesture g : b.gestures) n += g >= Gesture::Stutter8 && g <= Gesture::TapeStop;
    }
    return n;
  };
  CHECK_EQ(gestures(Profile::Jungle, 0.0, 0.0), 0);
  CHECK(gestures(Profile::Jungle, 1.0, 0.0) > gestures(Profile::Jungle, 0.5, 0.0));
  CHECK(gestures(Profile::Breakcore, 0.5, 0.0) > gestures(Profile::AmenFunk, 0.5, 0.0));
  CHECK(gestures(Profile::Jungle, 0.5, 1.0) > gestures(Profile::Jungle, 0.5, 0.0));  // chaos raises gestures
}

CHOP_TEST(rules_stutter_repeats_source_role_with_decay) {
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    PlanSettings s = base(Profile::Breakcore, seed);
    s.options.glitch = 1.0;
    s.allow.retrigger = true;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits)
        if (h.gesture == Gesture::Stutter8 || h.gesture == Gesture::Stutter16) {
          CHECK(h.role == DrumRole::Kick || h.role == DrumRole::Snare);
          CHECK(h.gestureCount >= 2 && h.gestureCount <= 4);
          CHECK(h.gestureIndex >= 1 && h.gestureIndex < h.gestureCount);
        }
  }
}

CHOP_TEST(rules_reverse_swell_ends_on_the_bar_line) {
  int n = 0;
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    PlanSettings s = base(Profile::IdmGlitch, seed);
    s.options.glitch = 1.0;
    s.allow.reverse = true;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits)
        if (h.gesture == Gesture::ReverseSwell) {
          ++n;
          CHECK(h.tx.reverse);
          CHECK_EQ(h.endAt, kBarTicks44);
          CHECK(h.start + h.maxWindow == kBarTicks44);
        }
  }
  CHECK(n > 0);
}

CHOP_TEST(rules_tape_stop_glides_down_at_phrase_end) {
  int n = 0;
  for (std::uint64_t seed = 0; seed < 600; ++seed) {
    PlanSettings s = base(Profile::IdmGlitch, seed);
    s.options.glitch = 1.0;
    s.allow.glide = true;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits)
        if (h.gesture == Gesture::TapeStop) {
          ++n;
          CHECK(h.fx.glideSemitones <= -12.f);
          CHECK_EQ(h.start, 13 * kStepTicks);
          CHECK_EQ(h.duration, kBarTicks44 - 13 * kStepTicks);
        }
  }
  CHECK(n > 0);
}

CHOP_TEST(rules_crash_only_on_downbeat_or_phrase_end) {
  int seen = 0;
  for (int pi = 0; pi < kProfileCount; ++pi)
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
      int perPhrase = 0;
      for (const BarPlan& b : planPattern(base(static_cast<Profile>(pi), seed)))
        for (const Hit& h : b.hits)
          if (h.gesture == Gesture::Crash) {
            ++seen;
            ++perPhrase;
            CHECK(h.step == 0 || h.step == 7 || h.step == 8);
            CHECK(h.step == 0 ? b.slot == 0 : b.slot == 3);
            CHECK(h.role == DrumRole::Cymbal);
          }
      if (!profileInfo(static_cast<Profile>(pi)).glitchProfile) CHECK(perPhrase <= 1);
    }
  CHECK(seen > 0);
}

// ---- timing (4.2, 7.4) ----
CHOP_TEST(rules_groove_zero_and_humanize_zero_are_on_grid) {
  for (std::uint64_t seed = 0; seed < 20; ++seed) {
    PlanSettings s = base(Profile::HipHop, seed);
    s.options.groove = 0.0;
    s.options.humanize = 0.0;
    s.swing = 0.0;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits)
        if (h.gesture == Gesture::None) CHECK_EQ(h.start, h.step * kStepTicks);
  }
}

CHOP_TEST(rules_humanize_adds_bounded_jitter) {
  PlanSettings s = base(Profile::Jungle, 3);
  s.options.humanize = 1.0;
  int jittered = 0;
  for (const BarPlan& b : planPattern(s))
    for (const Hit& h : b.hits) {
      if (h.gesture != Gesture::None) continue;
      const Ticks off = h.start - h.step * kStepTicks - ((h.step % 2) && !h.anchor ? static_cast<Ticks>(80.0 * effectiveSwing(s) + 0.5) : 0);
      jittered += off != 0;
      CHECK(std::llabs(static_cast<long long>(off)) <= msToTicks(maxTimingMs(s, h.role, h.anchor), 120.0));
    }
  CHECK(jittered > 0);
}

CHOP_TEST(rules_tempo_hint_scales_millisecond_offsets) {
  PlanSettings a = base(Profile::HipHop, 5), b = a;
  a.options.tempoHint = 60.0;
  b.options.tempoHint = 240.0;
  const auto pa = planPattern(a), pb = planPattern(b);
  long long sa = 0, sb = 0;
  for (std::size_t i = 0; i < pa.size(); ++i) {
    for (const Hit& h : pa[i].hits) sa += std::llabs(static_cast<long long>(h.start - h.step * kStepTicks));
    for (const Hit& h : pb[i].hits) sb += std::llabs(static_cast<long long>(h.start - h.step * kStepTicks));
  }
  CHECK(sb > sa);
}

CHOP_TEST(rules_hip_hop_snare_is_laid_back) {
  double sum = 0;
  int n = 0;
  for (std::uint64_t seed = 0; seed < 100; ++seed)
    for (const BarPlan& b : planPattern(base(Profile::HipHop, seed)))
      for (const Hit& h : b.hits)
        if (h.anchor && h.role == DrumRole::Snare) { sum += static_cast<double>(h.start - h.step * kStepTicks); ++n; }
  CHECK(sum / n > 5.0);  // late, in ticks (about 10 ms at 120 bpm is 19 ticks)
}

// ---- phrase behaviour (6.2) ----
CHOP_TEST(rules_phrase_strictness_repeats_bars) {
  auto sameness = [](double strict) {
    double d = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
      PlanSettings s = base(Profile::Jungle, seed);
      s.options.phraseStrictness = strict;
      const auto p = planPattern(s);
      auto steps = [](const BarPlan& b) {
        std::set<std::pair<int, int>> r;
        for (const Hit& h : b.hits)
          if (h.gesture == Gesture::None && (h.role == DrumRole::Kick || h.role == DrumRole::Snare || h.role == DrumRole::Ghost)) r.insert({h.step, static_cast<int>(h.role)});
        return r;
      };
      const auto a = steps(p[0]), c = steps(p[1]);
      int diff = 0;
      for (const auto& x : a) diff += !c.count(x);
      for (const auto& x : c) diff += !a.count(x);
      d += diff;
    }
    return d;
  };
  CHECK(sameness(1.0) < sameness(0.0));
}

CHOP_TEST(rules_bar_count_variants) {
  for (int bars : {1, 2, 4, 8}) {
    PlanSettings s = base();
    s.bars = bars;
    const auto p = planPattern(s);
    CHECK_EQ(p.size(), static_cast<std::size_t>(bars));
    for (std::size_t i = 0; i < p.size(); ++i) {
      CHECK_EQ(p[i].bar, static_cast<int>(i));
      CHECK_EQ(p[i].slot, phraseSlot(static_cast<int>(i), bars));
      CHECK(snareAt(p[i], 4));
    }
  }
}

// ---- determinism / sorting ----
CHOP_TEST(rules_seed_changes_plan_and_hits_are_sorted) {
  int different = 0;
  for (std::uint64_t seed = 1; seed < 30; ++seed) {
    const auto a = planPattern(base(Profile::Jungle, seed)), b = planPattern(base(Profile::Jungle, seed + 100));
    different += a[1].hits.size() != b[1].hits.size() || a[1].hits[3].velocity != b[1].hits[3].velocity || a[1].hits[3].start != b[1].hits[3].start;
    for (const BarPlan& bar : a)
      for (std::size_t i = 1; i < bar.hits.size(); ++i) CHECK(bar.hits[i - 1].start <= bar.hits[i].start);
  }
  CHECK(different > 20);
}

CHOP_TEST(rules_hits_stay_inside_the_bar) {
  for (int pi = 0; pi < kProfileCount; ++pi)
    for (std::uint64_t seed = 0; seed < 60; ++seed) {
      PlanSettings s = base(static_cast<Profile>(pi), seed);
      s.allow.reverse = s.allow.retrigger = s.allow.glide = s.allow.pitch = s.allow.filter = s.allow.crunch = true;
      s.options.glitch = 1.0;
      s.options.fill = 1.0;
      s.options.humanize = 1.0;
      for (const BarPlan& b : planPattern(s))
        for (const Hit& h : b.hits) {
          CHECK(h.start >= 0 && h.start < kBarTicks44);
          CHECK(h.step >= 0 && h.step < kStepsPerBar);
          CHECK(h.velocity >= 1 && h.velocity <= 127);
        }
    }
}

CHOP_TEST(rules_variation_respects_allow_flags_and_pitch_cap) {
  for (std::uint64_t seed = 0; seed < 60; ++seed) {
    PlanSettings s = base(Profile::Jungle, seed);
    s.variationEvent = 1.0;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits) {
        CHECK(h.tx.pitchSemitones == 0.f);
        CHECK(h.fx.filter == FilterType::Off);
        CHECK(h.fx.glideSemitones == 0.f && h.fx.crush == 0.f);
      }
    s.allow.pitch = s.allow.filter = s.allow.glide = s.allow.crunch = true;
    s.pitchRange = 3;
    int any = 0;
    for (const BarPlan& b : planPattern(s))
      for (const Hit& h : b.hits) {
        if (h.anchor) { CHECK(h.tx.pitchSemitones == 0.f); }
        CHECK(std::fabs(h.tx.pitchSemitones) <= 3.0f + 1e-6f || h.gesture != Gesture::None);
        any += h.tx.pitchSemitones != 0.f || h.fx.filter != FilterType::Off;
      }
    (void)any;
  }
}
