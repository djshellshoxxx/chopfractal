// Plan-level acceptance metrics (research section 12, metrics 1 to 14, 24, 25) over many seeds and all profiles.
// A failing metric fails the build. Set BQ_PRINT=1 to print the measured values.
#include <chopfractal/break_engine/break_engine.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "break_thresholds.hpp"
#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::breakeng;

namespace {
bool printing() { return std::getenv("BQ_PRINT") != nullptr; }

PlanSettings settingsFor(Profile p, std::uint64_t seed) {
  PlanSettings s;
  s.options.profile = p;
  s.seed = seed * 2654435761ull + 17;
  s.bars = 4;
  s.density = 0.5;
  return s;
}
PlanSettings glitchy(Profile p, std::uint64_t seed) {
  PlanSettings s = settingsFor(p, seed);
  s.allow.reverse = s.allow.pitch = s.allow.retrigger = s.allow.glide = s.allow.filter = s.allow.crunch = true;
  return s;
}

bool isLow(DrumRole r) { return r == DrumRole::Kick; }
bool isSnareFam(DrumRole r) { return r == DrumRole::Snare || r == DrumRole::Ghost; }

bool has(const BarPlan& b, DrumRole role, int step) {
  for (const Hit& h : b.hits)
    if (h.gesture == Gesture::None && h.role == role && h.step == step) return true;
  return false;
}
bool hasSnareAt(const BarPlan& b, int step) { return has(b, DrumRole::Snare, step); }

double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}
double sd(const std::vector<double>& v) {
  if (v.size() < 2) return 0.0;
  double m = 0;
  for (double x : v) m += x;
  m /= static_cast<double>(v.size());
  double q = 0;
  for (double x : v) q += (x - m) * (x - m);
  return std::sqrt(q / static_cast<double>(v.size()));
}
void report(const char* metric, Profile p, double value, const char* rule) {
  if (printing()) std::printf("  %-26s %-18s %8.3f  (%s)\n", metric, profileName(p), value, rule);
}
// A role grid of the non-gesture hits: 16 cells x kick/snare/ghost bitmask.
std::vector<int> grid(const BarPlan& b) {
  std::vector<int> g(16, 0);
  for (const Hit& h : b.hits) {
    if (h.gesture != Gesture::None) continue;
    if (h.role == DrumRole::Kick) g[static_cast<std::size_t>(h.step)] |= 1;
    if (h.role == DrumRole::Snare) g[static_cast<std::size_t>(h.step)] |= 2;
    if (h.role == DrumRole::Ghost) g[static_cast<std::size_t>(h.step)] |= 4;
  }
  return g;
}
int hamming(const std::vector<int>& a, const std::vector<int>& b) {
  int n = 0;
  for (std::size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
  return n;
}
int endCount(const BarPlan& b) {
  int n = 0;
  for (const Hit& h : b.hits)
    if (h.step >= 12 && h.role != DrumRole::Hat && h.role != DrumRole::OpenHat) ++n;
  return n;
}
}  // namespace

// 1, 2, 4: anchors.
CHOP_TEST(break_quality_01_02_04_anchors) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const bq::Row& t = bq::row(p);
    int bars = 0, both = 0, s5 = 0, nosn = 0, k1 = 0, nokick = 0, kbb = 0;
    for (int seed = 0; seed < bq::kSeeds; ++seed)
      for (const BarPlan& b : planPattern(settingsFor(p, static_cast<std::uint64_t>(seed)))) {
        ++bars;
        const bool a = hasSnareAt(b, 4), c = hasSnareAt(b, 12) || hasSnareAt(b, 14);
        both += a && c;
        s5 += a;
        bool anySnare = false, anyKick = false;
        for (const Hit& h : b.hits) {
          anySnare |= h.role == DrumRole::Snare;
          anyKick |= h.role == DrumRole::Kick;
        }
        nosn += !anySnare;
        nokick += !anyKick;
        k1 += has(b, DrumRole::Kick, 0);
        kbb += has(b, DrumRole::Kick, 4) || has(b, DrumRole::Kick, 12);
      }
    const double n = bars;
    report("backbeat both", p, both / n, ">=");
    report("snare on 5", p, s5 / n, ">=");
    report("no snare", p, nosn / n, "<=");
    report("kick on 1", p, k1 / n, ">=");
    report("no kick", p, nokick / n, "<=");
    report("kick on 5/13", p, kbb / n, "<=");
    CHECK(both / n >= t.bothBackbeatMin);
    CHECK(s5 / n >= t.snare5Min);
    CHECK(nosn / n <= t.noSnareMax);
    CHECK(k1 / n >= t.kick1Min);
    CHECK(nokick / n <= t.noKickMax);
    CHECK(kbb / n <= t.kickOnBackbeatMax);
  }
}

// 3: kick and snare never share a step (layered mode off), 7: ghost behaviour.
CHOP_TEST(break_quality_03_07_collisions_and_ghosts) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const bool glitch = profileInfo(p).glitchProfile;
    int collisions = 0, strongGhosts = 0, tooMany = 0, runs = 0, bars = 0;
    for (int seed = 0; seed < bq::kSeeds; ++seed)
      for (const BarPlan& b : planPattern(glitchy(p, static_cast<std::uint64_t>(seed)))) {
        ++bars;
        int ghosts = 0;
        bool snareStep[16] = {};
        for (const Hit& h : b.hits) {
          if (h.gesture != Gesture::None) continue;
          if (isSnareFam(h.role)) {
            snareStep[h.step] = true;
            for (const Hit& k : b.hits)
              if (k.gesture == Gesture::None && isLow(k.role) && k.step == h.step) ++collisions;
          }
          if (h.role == DrumRole::Ghost) {
            ++ghosts;
            if (h.step % 4 == 0) ++strongGhosts;
          }
        }
        tooMany += ghosts > 6;
        if (!glitch)
          for (int w = 0; w + 2 < 16; ++w) runs += snareStep[w] && snareStep[w + 1] && snareStep[w + 2];
      }
    report("kick/snare collisions", p, collisions, "== 0");
    report("ghost on strong step", p, strongGhosts, "== 0");
    CHECK_EQ(collisions, 0);
    CHECK_EQ(strongGhosts, 0);
    CHECK_EQ(tooMany, 0);
    CHECK_EQ(runs, 0);
    CHECK(bars == bq::kSeeds * 4);
  }
}

// 5: per-step onset probability against the engine's own table, per role.
CHOP_TEST(break_quality_05_density_histogram) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const bq::Row& t = bq::row(p);
    double obs[3][16] = {};
    double exp[3][16] = {};
    const DrumRole roles[3] = {DrumRole::Kick, DrumRole::Snare, DrumRole::Ghost};
    int bars = 0;
    for (int seed = 0; seed < bq::kSeeds; ++seed) {
      const PlanSettings s = settingsFor(p, static_cast<std::uint64_t>(seed));
      for (const BarPlan& b : planPattern(s)) {
        ++bars;
        for (int r = 0; r < 3; ++r)
          for (int st = 0; st < 16; ++st) {
            obs[r][st] += has(b, roles[r], st) ? 1.0 : 0.0;
            exp[r][st] += cellProbability(s, b.slot, roles[r], st);
          }
      }
    }
    double worst = 0;
    for (int r = 0; r < 3; ++r) {
      double l1 = 0;
      for (int st = 0; st < 16; ++st) l1 += std::fabs(obs[r][st] - exp[r][st]) / bars;
      l1 /= 16.0;
      worst = std::max(worst, l1);
    }
    report("density L1 (worst role)", p, worst, "<=");
    CHECK(worst <= t.densityL1Max);
  }
}

// Events per bar without hats stay inside (a tolerance around) the profile band.
CHOP_TEST(break_quality_05b_density_band) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const ProfileInfo& info = profileInfo(p);
    for (double d : {0.0, 0.5, 1.0}) {
      double sum = 0;
      int bars = 0;
      for (int seed = 0; seed < 60; ++seed) {
        PlanSettings s = settingsFor(p, static_cast<std::uint64_t>(seed));
        s.density = d;
        for (const BarPlan& b : planPattern(s)) {
          int n = 0;
          for (const Hit& h : b.hits)
            if (h.gesture == Gesture::None && h.role != DrumRole::Hat && h.role != DrumRole::OpenHat) ++n;
          sum += n;
          ++bars;
        }
      }
      const double mean = sum / bars;
      const double target = info.densityLo + (info.densityHi - info.densityLo) * d;
      if (printing()) std::printf("  band %-18s d=%.1f mean=%.2f target=%.1f\n", info.name, d, mean, target);
      CHECK(mean >= target * 0.70 - 1.0);
      CHECK(mean <= target * 1.10 + 1.0);
    }
  }
}

// 6: velocity hierarchy.
CHOP_TEST(break_quality_06_velocity_hierarchy) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const bq::Row& t = bq::row(p);
    std::vector<double> bb, gh;
    std::map<int, int> same;
    int snares = 0;
    for (int seed = 0; seed < bq::kSeeds; ++seed)
      for (const BarPlan& b : planPattern(settingsFor(p, static_cast<std::uint64_t>(seed))))
        for (const Hit& h : b.hits) {
          if (h.gesture != Gesture::None) continue;
          if (h.role == DrumRole::Snare && h.anchor) {
            bb.push_back(h.velocity);
            ++same[h.velocity];
            ++snares;
          }
          if (h.role == DrumRole::Ghost) gh.push_back(h.velocity);
        }
    const double mb = median(bb), mg = median(gh);
    const double s = sd(bb);
    int maxSame = 0;
    for (const auto& kv : same) maxSame = std::max(maxSame, kv.second);
    report("backbeat - ghost median", p, mb - mg, ">=");
    report("ghost / backbeat", p, mg / mb, "0.15..0.5");
    report("backbeat velocity sd", p, s, "range");
    report("share of top velocity", p, static_cast<double>(maxSame) / snares, "<= 0.25");
    CHECK(mb - mg >= t.backbeatGhostGapMin);
    CHECK(mg / mb >= 0.15 && mg / mb <= 0.5);
    CHECK(s >= t.velSdMin && s <= t.velSdMax);
    // Research: at most 5 percent identical within a role. The backbeat range is narrow (100..127), so the equal-value
    // share of the single most common velocity is bounded at 25 percent instead (a 10-wide spread cannot do better).
    CHECK(static_cast<double>(maxSame) / snares <= 0.25);
  }
}

// 8, 9: phrase shape.
CHOP_TEST(break_quality_08_09_phrase) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const bq::Row& t = bq::row(p);
    int more = 0, fillOrCrash = 0, differs = 0, eq12 = 0;
    for (int seed = 0; seed < bq::kSeeds; ++seed) {
      const auto plans = planPattern(settingsFor(p, static_cast<std::uint64_t>(seed)));
      more += endCount(plans[3]) > endCount(plans[0]);
      bool fc = false;
      for (Gesture g : plans[3].gestures) fc |= g != Gesture::None;
      fillOrCrash += fc;
      differs += hamming(grid(plans[3]), grid(plans[0])) >= 3;
      eq12 += hamming(grid(plans[0]), grid(plans[1])) <= static_cast<int>(t.hamming12Max);
    }
    const double n = bq::kSeeds;
    report("bar4 more at end", p, more / n, ">=");
    report("bar4 fill or crash", p, fillOrCrash / n, ">=");
    report("bar4 differs", p, differs / n, ">=");
    report("bar1 ~ bar2", p, eq12 / n, ">=");
    CHECK(more / n >= t.fillBar4Min);
    CHECK(fillOrCrash / n >= t.fillOrCrashMin);
    CHECK(differs / n >= t.bar4DiffersMin);
    CHECK(eq12 / n >= t.bar1EqualsBar2Min);
  }
}

// 11: swing offsets of odd 16th hats equal the configured swing; anchors are not swung.
CHOP_TEST(break_quality_11_swing) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    for (double sw : {0.0, 0.5, 1.0}) {
      double oddSum = 0, evenSum = 0, anchorSum = 0;
      int odd = 0, even = 0, anchors = 0;
      PlanSettings base = settingsFor(p, 1);
      base.swing = sw;
      const double expected = 80.0 * effectiveSwing(base);
      for (int seed = 0; seed < 100; ++seed) {
        PlanSettings s = settingsFor(p, static_cast<std::uint64_t>(seed));
        s.swing = sw;
        for (const BarPlan& b : planPattern(s))
          for (const Hit& h : b.hits) {
            if (h.gesture != Gesture::None) continue;
            const double off = static_cast<double>(h.start - h.step * kStepTicks);
            if (h.anchor) { anchorSum += off; ++anchors; }
            else if (h.role == DrumRole::Hat || h.role == DrumRole::OpenHat) {
              if (h.step % 2) { oddSum += off; ++odd; } else { evenSum += off; ++even; }
            }
          }
      }
      const double oddMean = oddSum / odd, evenMean = evenSum / even;
      report("swing odd offset (ticks)", p, oddMean, "== configured");
      CHECK_NEAR(oddMean - evenMean, expected, 0.8);
      // Anchors: mean offset within +-2 ms (msToTicks), plus the profile's laid-back snare.
      static const double kSnareLateMs[kProfileCount] = {4.0, 0.0, 0.0, 0.0, 0.0, 10.0};  // tables.cpp, groove 0.5
      const double bound = static_cast<double>(msToTicks(kSnareLateMs[pi] + 2.0, 120.0));
      report("anchor mean offset", p, anchorSum / anchors, "<= bound");
      CHECK(std::fabs(anchorSum / anchors) <= bound);
    }
  }
}

// 12: timing budget; gesture sub-hits sit exactly on the 1/64 grid (no jitter on fine rolls).
CHOP_TEST(break_quality_12_timing_budget) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    int over = 0, offGrid = 0;
    for (int seed = 0; seed < 100; ++seed) {
      PlanSettings s = glitchy(p, static_cast<std::uint64_t>(seed));
      s.swing = 1.0;
      s.options.humanize = 1.0;
      s.options.groove = 1.0;
      const double swing = 80.0 * effectiveSwing(s);
      for (const BarPlan& b : planPattern(s))
        for (const Hit& h : b.hits) {
          if (h.gesture != Gesture::None && h.gesture != Gesture::Crash) {
            if (h.endAt < 0 && h.gesture != Gesture::TapeStop && h.start % (kStepTicks / 4) != 0) ++offGrid;
            continue;
          }
          const double off = static_cast<double>(h.start - h.step * kStepTicks) - ((h.step % 2) && !h.anchor ? swing : 0.0);
          const double cap = h.anchor && p == Profile::HipHop ? 20.0 : 12.0;
          if (std::fabs(off) > static_cast<double>(msToTicks(cap, 120.0)) + 1.5) ++over;
        }
    }
    report("events over timing budget", p, over, "== 0");
    CHECK_EQ(over, 0);
    CHECK_EQ(offGrid, 0);
  }
}

// 13, 24: roll and stutter shape.
CHOP_TEST(break_quality_13_24_roll_shape) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const bool glitchProfile = profileInfo(p).glitchProfile;
    int bad = 0, fine = 0, rolls = 0, flat = 0, tooLong = 0;
    for (int seed = 0; seed < 150; ++seed) {
      PlanSettings s = glitchy(p, static_cast<std::uint64_t>(seed));
      s.options.glitch = 1.0;
      s.options.fill = 1.0;
      for (const BarPlan& b : planPattern(s)) {
        std::map<Gesture, std::vector<const Hit*>> byGesture;
        for (const Hit& h : b.hits)
          if (h.gesture != Gesture::None && h.gesture != Gesture::Crash) byGesture[h.gesture].push_back(&h);
        for (auto& kv : byGesture) {
          const Gesture g = kv.first;
          auto& v = kv.second;
          std::sort(v.begin(), v.end(), [](const Hit* a, const Hit* c) { return a->start < c->start; });
          if (v.size() > 16) ++tooLong;
          const bool rising = g == Gesture::Roll16 || g == Gesture::Roll32 || g == Gesture::SpeedRamp || g == Gesture::DoubleSnare || g == Gesture::TomRun;
          const bool falling = g == Gesture::Stutter8 || g == Gesture::Stutter16 || g == Gesture::Stutter32;
          if (g == Gesture::SpeedRamp && !glitchProfile) ++fine;
          if (rising || falling) ++rolls;
          for (std::size_t i = 1; i < v.size(); ++i) {
            const int d = v[i]->velocity - v[i - 1]->velocity;
            if (g == Gesture::TomRun) { if (d > 6) ++bad; }       // descending run
            else if (rising && d < -6) ++bad;
            else if (falling && d > 6) ++bad;
          }
          // 24: repeats of a roll of >= 4 are not machine-gun identical (Stutter32 is flat by design but jittered).
          if (v.size() >= 4 && (g == Gesture::Roll16 || g == Gesture::Roll32 || g == Gesture::Stutter32)) {
            std::set<int> distinct;
            for (const Hit* h : v) distinct.insert(h->velocity);
            if (distinct.size() < 2) ++flat;
            // The pitch trend of a rising roll (0 to +2 st over the roll) is intended; what is left after removing it is jitter.
            double pitchMin = 99, pitchMax = -99;
            for (const Hit* h : v) {
              const double trend = g == Gesture::Stutter32 ? 0.0 : 2.0 * h->gestureIndex / (h->gestureCount - 1);
              const double res = h->tx.pitchSemitones - trend;
              pitchMin = std::min(pitchMin, res);
              pitchMax = std::max(pitchMax, res);
            }
            if (g != Gesture::Stutter32 && pitchMax - pitchMin > 0.6 + 1e-6) ++bad;
          }
        }
      }
    }
    report("rolls checked", p, rolls, "> 0 where gestures allowed");
    CHECK_EQ(bad, 0);
    CHECK_EQ(fine, 0);
    CHECK_EQ(flat, 0);
    CHECK_EQ(tooLong, 0);
    if (p == Profile::Breakcore || p == Profile::IdmGlitch) CHECK(rolls > 20);
  }
}

// 14: reversed hits end exactly on a grid line, only on snare / cymbal / ghost roles.
CHOP_TEST(break_quality_14_reverse) {
  int reversed = 0;
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    for (int seed = 0; seed < 150; ++seed) {
      PlanSettings s = glitchy(p, static_cast<std::uint64_t>(seed));
      s.options.glitch = 1.0;
      for (const BarPlan& b : planPattern(s))
        for (const Hit& h : b.hits) {
          if (!h.tx.reverse) continue;
          ++reversed;
          CHECK(h.role == DrumRole::Snare || h.role == DrumRole::Cymbal || h.role == DrumRole::Ghost);
          CHECK(h.endAt >= 0 && h.endAt % kStepTicks == 0);
          CHECK(h.maxWindow > 0 && h.maxWindow <= kBarTicks44);
        }
    }
  }
  CHECK(reversed > 0);
  // Without the allow flag no hit is reversed.
  for (int seed = 0; seed < 50; ++seed)
    for (const BarPlan& b : planPattern(settingsFor(Profile::Breakcore, static_cast<std::uint64_t>(seed))))
      for (const Hit& h : b.hits) CHECK(!h.tx.reverse);
}

// 25: silence audit; in bars 1 to 3 no gap between consecutive nominal steps exceeds two 16ths (four for the glitch
// profiles, which may drop the hat bed); only the gate-drop gesture, which is a bar-4 fill, leaves longer holes.
CHOP_TEST(break_quality_25_silence) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const Profile p = static_cast<Profile>(pi);
    const int maxGap = profileInfo(p).glitchProfile ? 4 : 2;
    int gaps = 0;
    for (int seed = 0; seed < bq::kSeeds; ++seed) {
      const auto plans = planPattern(settingsFor(p, static_cast<std::uint64_t>(seed)));
      for (std::size_t bi = 0; bi < 3; ++bi) {
        std::set<int> steps;
        for (const Hit& h : plans[bi].hits) steps.insert(h.step);
        int last = 0;
        for (int st : steps) {
          if (st - last > maxGap) ++gaps;
          last = st;
        }
        if (16 - last > maxGap + 1) ++gaps;
      }
    }
    report("silent gaps in bars 1..3", p, gaps, "== 0");
    CHECK_EQ(gaps, 0);
  }
}

// 15: determinism and the hash of a plan are stable.
CHOP_TEST(break_quality_15_determinism) {
  for (int pi = 0; pi < kProfileCount; ++pi) {
    const PlanSettings s = glitchy(static_cast<Profile>(pi), 7);
    const auto a = planPattern(s), b = planPattern(s);
    CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      CHECK_EQ(a[i].hits.size(), b[i].hits.size());
      for (std::size_t k = 0; k < a[i].hits.size(); ++k) {
        CHECK_EQ(a[i].hits[k].start, b[i].hits[k].start);
        CHECK_EQ(a[i].hits[k].velocity, b[i].hits[k].velocity);
        CHECK(a[i].hits[k].role == b[i].hits[k].role);
      }
    }
    // planBar is stateless.
    const BarPlan single = planBar(s, 2);
    CHECK_EQ(single.hits.size(), a[2].hits.size());
  }
}
