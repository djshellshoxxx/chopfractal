#pragma once
// Acceptance thresholds of the plan-level break-quality metrics (research section 12, metrics 1 to 14, 24, 25).
// One row per profile so a profile change updates the tests mechanically. Strict profiles use the funk / jungle
// column of the research table, soft profiles (breakcore, IDM) its parenthesised column.
//
// Derivation: the research values are the floor; where the measured human data in docs/research/BREAK_ANALYSIS.md
// is looser (backbeat snare on step 5 / 13 in funk .82 / .72, hip-hop .83 / .70; kick on 1 .87 / .80) the engine
// intentionally exceeds it, so the floor is the research number, not the human mean. densityL1Max is measured: the
// observed per-step probability falls short of the raw table by the share the collision / adjacency / ghost rules
// discard (largest where the band is widest: jungle .12, rolling .14, breakcore .16 at the time of writing), so the
// research bound 0.15 is widened for DnB and breakcore. Hip-hop fill-or-crash is 0.45 because its profile fill
// multiplier is 0.5 (research 6.5). Both guard against regressions rather than define taste.
#include <chopfractal/break_engine/break_engine.hpp>

namespace bq {
struct Row {
  chopfractal::breakeng::Profile profile;
  double bothBackbeatMin;   // 1: snare on steps 5 and 13 in the same bar
  double snare5Min;         // 1: snare on step 5
  double noSnareMax;        // 1: bars without any snare
  double kick1Min;          // 2
  double noKickMax;         // 2
  double kickOnBackbeatMax; // 4: kick on 5 or 13
  double densityL1Max;      // 5: mean absolute per-step probability error vs the engine's own table
  double backbeatGhostGapMin; // 6: median backbeat velocity minus median ghost velocity
  double velSdMin, velSdMax;  // 6: standard deviation of backbeat snare velocity
  double fillBar4Min;       // 8: P(bar 4 has more events in steps 13..16 than bar 1)
  double fillOrCrashMin;    // 8: bar 4 has a fill gesture or a crash
  double bar4DiffersMin;    // 9
  double bar1EqualsBar2Min; // 9 (0 = not checked, soft profiles)
  double hamming12Max;      // 9
};
using chopfractal::breakeng::Profile;
// clang-format off
inline const Row kRows[] = {
    //  profile              both  s5    nosn  k1    nokick kOnBB  L1    gap  sdLo sdHi fill  f|c   b4dif b1=b2 ham
    {Profile::AmenFunk,      0.70, 0.95, 0.02, 0.85, 0.03,  0.03, 0.10, 50, 1.0, 10.0, 0.50, 0.70, 0.80, 0.50, 3},
    {Profile::Jungle,        0.70, 0.95, 0.02, 0.85, 0.03,  0.03, 0.15, 50, 1.0, 10.0, 0.50, 0.70, 0.80, 0.50, 3},
    {Profile::DnbRolling,    0.70, 0.95, 0.02, 0.85, 0.03,  0.03, 0.17, 50, 1.0, 10.0, 0.50, 0.70, 0.80, 0.50, 3},
    {Profile::Breakcore,     0.45, 0.70, 0.10, 0.60, 0.05,  0.08, 0.20, 40, 1.0, 10.0, 0.40, 0.70, 0.80, 0.00, 99},
    {Profile::IdmGlitch,     0.30, 0.55, 0.10, 0.40, 0.08,  0.08, 0.12, 40, 1.0, 10.0, 0.40, 0.60, 0.80, 0.00, 99},
    {Profile::HipHop,        0.70, 0.95, 0.02, 0.85, 0.03,  0.03, 0.10, 50, 1.0, 10.0, 0.40, 0.45, 0.70, 0.50, 3},
};
// clang-format on
inline const Row& row(chopfractal::breakeng::Profile p) {
  for (const Row& r : kRows) if (r.profile == p) return r;
  return kRows[0];
}
constexpr int kSeeds = 200;
}  // namespace bq
