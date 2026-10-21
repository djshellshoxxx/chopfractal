#pragma once
// Private tables and helpers of the break engine. Not part of the public API.
#include <chopfractal/break_engine/break_engine.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

namespace chopfractal::breakeng::detail {

// One row of research table 6.1 (probabilities per bar, velocities as mean/low/high on the MIDI 1..127 scale).
struct StepRow {
  double kickP;
  int kMu, kLo, kHi;
  double snareP;
  int sMu, sLo, sHi;
  double ghostP;
  int gMu, gLo, gHi;
  double hatP;
  int hMu, hLo, hHi;
  double openP;
};
extern const StepRow kRows[kStepsPerBar];

// Per-profile numbers beyond ProfileInfo.
struct ProfileTiming {
  double kick1Mult[4];     // multiplier on the downbeat kick per phrase slot
  double displace[4];      // probability the second backbeat moves from step 13 to step 15, per slot
  double hat16;            // probability a bar plays 16th hats (otherwise 8ths)
  double crashScale;       // multiplier on crash probabilities
  double snareLateMs;      // systematic laid-back offsets at groove = 0.5
  double kick2LateMs;
  double ghostLateMs;
  double hatSigmaMs;       // hat jitter at humanize = 0.5
  double openScale;        // multiplier on the open-hat probabilities
  double ruleLoss;         // share of drawn hits that survive the collision / adjacency / ghost rules (calibrated, see tests)
};
const ProfileTiming& profileTiming(Profile p);
const ProfileInfo& profileRow(Profile p);

// Phrase slot modifiers (research table 6.2). Means are 1.0 so the density mapping stays calibrated.
extern const double kOtherKickMod[4];
extern const double kGhostMod[4];
extern const double kFillChance[4];

// Gesture table 6.3: probability per phrase end (slot 3) and per other bar.
struct GestureRow {
  Gesture g;
  double pPhrase;
  double pBar;
  double weight;  // relative weight inside the fill choice
};
extern const GestureRow kFillRows[5];    // Roll16, Roll32, DoubleSnare, TomRun, GateDrop
extern const GestureRow kGlitchRows[6];  // Stutter8, Stutter16, Stutter32, SpeedRamp, ReverseSwell, TapeStop

constexpr double kBackbeatP1 = 0.97;  // research 6.1 step 5
constexpr double kBackbeatP2 = 0.95;  // step 13
constexpr double kKickAnchorP = 0.95;
constexpr double kMaxCellP = 0.95;

// Random streams (stable tags; never renumber).
enum : std::uint64_t {
  kTagCopy = 0xB101,
  kTagKick = 0xB102,
  kTagSnare = 0xB103,
  kTagGhost = 0xB104,
  kTagHat = 0xB105,
  kTagOpen = 0xB106,
  kTagCrash = 0xB107,
  kTagFill = 0xB108,
  kTagGlitch = 0xB109,
  kTagTiming = 0xB10A,
  kTagVel = 0xB10B,
  kTagFx = 0xB10C,
  kTagHatMode = 0xB10D,
  kTagDisplace = 0xB10E,
  kTagMisc = 0xB10F,
};

inline Rng cellRng(const PlanSettings& s, std::uint64_t tag, std::uint64_t a, std::uint64_t b = 0) { return Rng(deriveKey(s.seed, tag, a, b)); }

// Sum of four uniforms rescaled to unit variance: a bounded (+-3.46) normal-ish draw with exact arithmetic only.
inline double gauss(Rng& r) {
  const double u = r.uniform01() + r.uniform01() + r.uniform01() + r.uniform01();
  return (u - 2.0) * 1.7320508075688772;
}
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

double densityScale(const PlanSettings& s);
double groovePos(const Options& o);  // groove * 2: 1.0 at the default
double humanizePos(const Options& o);
int slotOf(int bar, int bars);
bool rollingOn(const Options& o);
double strictAnchorP(const Options& o, bool second);

}  // namespace chopfractal::breakeng::detail
