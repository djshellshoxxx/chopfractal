#pragma once
// A/B morph: blends two finished patterns of the same length, meter and grid. Matched hits interpolate, hits that
// exist on one side only fade in or out by probability (resolved by flatten's fixed per-id draw, so the audible
// set only shrinks / grows as t rises). Pure function: no state, no audio.
#include <chopfractal/pattern_engine/pattern.hpp>

namespace chopfractal::pattern {

struct MorphReport {
  std::size_t matched = 0;
  std::size_t onlyA = 0;
  std::size_t onlyB = 0;
  bool childrenDropped = false;
};

// t in [0, 1]. t <= 0 returns `a` and t >= 1 returns `b` unchanged (byte-identical serialize).
// freeze = false: unmatched events stay in the result with a scaled probability (live preview).
// freeze = true : the same draws are resolved now; absent events are removed and base probabilities restored.
Result<Pattern> morph(const Pattern& a, const Pattern& b, double t, const ChopSnapshot& chops, bool freeze = false,
                      MorphReport* report = nullptr);

}  // namespace chopfractal::pattern
