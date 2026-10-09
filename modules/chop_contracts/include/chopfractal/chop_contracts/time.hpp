#pragma once
// Canonical musical time. Positions are integer ticks, independent of sample rate and tempo.
#include <cstdint>

namespace chopfractal {

using Ticks = std::int64_t;
constexpr Ticks kTicksPerQuarter = 960;
constexpr Ticks kTicksPerWhole = kTicksPerQuarter * 4;

struct TimeSignature {
  int numerator = 4;
  int denominator = 4;
  constexpr bool valid() const {
    const bool denomOk = denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 ||
                         denominator == 16 || denominator == 32;
    return denomOk && numerator >= 1 && numerator <= 32;
  }
  friend constexpr bool operator==(TimeSignature a, TimeSignature b) {
    return a.numerator == b.numerator && a.denominator == b.denominator;
  }
};

constexpr Ticks ticksPerBeat(TimeSignature ts) { return kTicksPerWhole / ts.denominator; }
constexpr Ticks ticksPerBar(TimeSignature ts) { return ticksPerBeat(ts) * ts.numerator; }

// Grid cell: whole-note through 1/32, straight or triplet. All results are exact integers.
struct Grid {
  int division = 16;  // 1, 2, 4, 8, 16, 32 => note value 1/division
  bool triplet = false;
  constexpr bool valid() const {
    return division == 1 || division == 2 || division == 4 || division == 8 || division == 16 || division == 32;
  }
  friend constexpr bool operator==(Grid a, Grid b) { return a.division == b.division && a.triplet == b.triplet; }
};

constexpr Ticks gridTicks(Grid g) {
  const Ticks straight = kTicksPerWhole / g.division;
  return g.triplet ? straight * 2 / 3 : straight;
}

}  // namespace chopfractal
