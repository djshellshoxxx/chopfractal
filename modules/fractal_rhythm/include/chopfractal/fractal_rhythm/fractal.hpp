#pragma once
// Fractal Rhythm: a motif applied at every scale. The bar is divided by the motif; every hit is itself
// divided by the same motif, down to `depth` scales. Output is ordinary nested events: every cell above
// the deepest scale carries a NestedPattern child covering exactly that cell's window, so only the
// deepest cells sound (a Cantor-like rhythm).
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_contracts/policy.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::fractal {

constexpr int kMinMotif = 2;
constexpr int kMaxMotif = 8;
constexpr int kMaxDepth = 3;
constexpr Ticks kMinCellTicks = 15;

struct Settings {
  std::string motif = "x.xx";  // x = hit, a = accented hit, . = rest
  int depth = 2;               // 1..3 scales
  std::uint64_t seed = 1;
  double mutation = 0.0;       // [0, 1] chance each deeper cell flips (hit <-> rest) and picks an alternate chop
  bool mirror = false;         // reverse the motif on alternate scales
  double accentDecay = 1.0;    // (0, 1] level multiplier per scale below the first
};

struct Context {
  const ChopSnapshot* chops = nullptr;
  int bar = 0;
  int barCount = 1;
  Ticks barTicks = 3840;
  int beatsPerBar = 4;
  const ICandidatePolicy* policy = nullptr;
  std::size_t eventBudget = limits::kMaxEventsPerPattern;  // maximum sounding hits this bar may add
};

// Leaf level = product over scales of (a: 1.0, x: 0.8) times accentDecay^(depth-1), clamped to [0.05, 1].
Status validate(const Context& ctx, const Settings& s);
// Events are bar-relative with derived IDs; the caller installs them (pattern::setBarEvents).
// LimitExceeded carries the deepest permitted depth in `hint` when the event budget is the problem.
Result<std::vector<Event>> generateBar(const Context& ctx, const Settings& s);
// Number of sounding hits generateBar would produce without mutation (active cells ^ depth).
std::size_t soundingHits(const Settings& s);

}  // namespace chopfractal::fractal
