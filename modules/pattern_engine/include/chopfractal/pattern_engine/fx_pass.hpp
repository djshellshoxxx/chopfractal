#pragma once
// Gesture-effect pass (Echo, Tremolo-Pan, Stutter, Roll): decides which top-level events carry which effect block.
// It is a pure function over a finished pattern and never changes what or when the pattern plays, only the optional
// per-event effect blocks, so generation output (and every golden pattern hash) is unaffected. Deterministic: the same
// pattern, settings and seed give the same blocks. Hits the user forced (Echo level or Tremolo-Pan depth marked 128+,
// Tremolo-Pan mode 254) and user-owned or locked events keep whatever they have.
#include <chopfractal/pattern_engine/pattern.hpp>

#include <cstdint>

namespace chopfractal::pattern {

struct FxPassSettings {
  double echoAmount = 0.0;     // 0..1 share of eligible hits that get an Echo block (0 = none)
  double tremPanAmount = 0.0;  // 0..1 share of hits that get a Tremolo-Pan block
  double stutterAmount = 0.0;  // 0..1 share of long hits that get a Stutter block
  double rollAmount = 0.0;     // 0..1 share of long hits that get a Roll block (Stutter wins when both are chosen)
  // Shape of the generated Stutter and Roll blocks (the host parameters of the same names).
  int stutterRate = 4;       // division index 0..9 (1/4 .. 1/64T) the repeats start at
  int stutterGate = 80;      // 10..100 %
  int stutterDecay = -15;    // 0.1 dB per repeat, -120..60
  int stutterPitch = 0;      // total pitch ramp over the hit in 0.1 semitone, -120..120
  int rollRate = 4;          // division index the roll starts at
  int rollRateEnd = 8;       // division index it ends at (never coarser than the start)
  int rollFreeze = 0;        // 0..100 % into the chop
  std::uint64_t seed = 1;
  bool any() const { return echoAmount > 0.0 || tremPanAmount > 0.0 || stutterAmount > 0.0 || rollAmount > 0.0; }
  friend bool operator==(const FxPassSettings& a, const FxPassSettings& b) {
    return a.echoAmount == b.echoAmount && a.tremPanAmount == b.tremPanAmount && a.stutterAmount == b.stutterAmount &&
           a.rollAmount == b.rollAmount && a.stutterRate == b.stutterRate && a.stutterGate == b.stutterGate &&
           a.stutterDecay == b.stutterDecay && a.stutterPitch == b.stutterPitch && a.rollRate == b.rollRate &&
           a.rollRateEnd == b.rollRateEnd && a.rollFreeze == b.rollFreeze && a.seed == b.seed;
  }
};

// Removes the blocks an earlier pass wrote (user-forced and user-owned ones stay), then writes new ones.
// An all-zero setting therefore clears the pass's blocks. Nested events are not touched.
Pattern applyFxPass(const Pattern& pattern, const FxPassSettings& settings);

}  // namespace chopfractal::pattern
