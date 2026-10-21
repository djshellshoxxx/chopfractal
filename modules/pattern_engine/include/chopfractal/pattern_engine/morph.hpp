#pragma once
// A/B morph (docs/specs/roadmap/F10-ab-morph.md): blends two finished patterns of the same length, meter and grid.
// Pure and deterministic: matched hits interpolate their numeric fields, hits present in only one pattern fade in
// or out by probability. Never regenerates anything and never enforces locks inside the blend.
#include <chopfractal/pattern_engine/pattern.hpp>

namespace chopfractal::pattern {

// Hits closer than one grid cell (and on the same chop) are the same hit; the named constant for tuning (spec section 10).
constexpr int kMorphMatchGridCells = 1;

struct MorphReport {
  std::size_t matched = 0;
  std::size_t onlyA = 0;
  std::size_t onlyB = 0;
  bool childrenDropped = false;  // zoomed children of B-side hits were dropped because their derived ids collided
};

// t in [0, 1] (NaN is InvalidArgument). t <= 0 returns `a` unchanged and t >= 1 returns `b` unchanged (byte-identical
// serialize). freeze = false keeps unmatched hits in the result with a scaled probability (live preview, resolved when
// the pattern is flattened); freeze = true resolves the same draws now: hits that would be silent are removed and the
// kept ones get their base probability back, so flatten(freeze) plays exactly what the preview played.
// Errors: Conflict when length, meter or grid differ, LimitExceeded when matched + onlyA + onlyB exceeds the event cap.
Result<Pattern> morph(const Pattern& a, const Pattern& b, double t, const ChopSnapshot& chops, bool freeze = false, MorphReport* report = nullptr);

}  // namespace chopfractal::pattern
