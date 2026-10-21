#pragma once
// Break-aware rhythm model ("engine v2"): role-driven bar plans for drum breaks.
//
// Pure functions over plain data. A plan says WHICH role plays WHERE in a 16-step bar, how loud, how far
// off the grid, and which fill or glitch gesture produced it. It never touches chops, audio, ids or the
// pattern model: pattern_engine maps plans onto chops of the right role and builds events.
//
// Sources of every table (see tables.cpp for the numbers): docs/research/BREAK_ENGINE_RESEARCH.md sections
// 4, 6 and 10, and the measurements in docs/research/BREAK_ANALYSIS.md (Groove MIDI Dataset, Magenta,
// CC BY 4.0, plus CC0 open loops; attribution in THIRD_PARTY_NOTICES.md).
//
// Determinism: integer arithmetic and exact double operations only (no libm transcendental functions), so
// a plan is bit-identical across GCC, Clang, Apple Clang and MSVC. Threading: pure, thread-safe, not
// real-time safe.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <cstdint>
#include <vector>

namespace chopfractal::breakeng {

constexpr int kStepsPerBar = 16;
constexpr Ticks kStepTicks = kTicksPerQuarter / 4;  // one 16th note
constexpr Ticks kBarTicks44 = kStepTicks * kStepsPerBar;
constexpr std::uint32_t kModuleVersion = 1;  // bump when a plan for identical input would change

enum class Profile : std::uint8_t { AmenFunk, Jungle, DnbRolling, Breakcore, IdmGlitch, HipHop, kCount };
constexpr int kProfileCount = static_cast<int>(Profile::kCount);
// Standard = snare on 2 and 4 with the ghost layer where real players put it; Rolling = ghosts right after
// each backbeat (steps 6 and 14) and 16th hats, the DnB "rolling" feel.
enum class Backbeat : std::uint8_t { Standard, Rolling };
// Profile = the profile's own protection (strict for funk/jungle/hip-hop, soft for breakcore/IDM);
// Strict = always protected; Free = anchors may be dropped like any other hit (floor 0.5).
enum class AnchorMode : std::uint8_t { Profile, Strict, Free };

enum class Gesture : std::uint8_t {
  None = 0,
  Roll16,        // snare roll, 1/16, velocity ramp up
  Roll32,        // snare roll, 1/32, velocity ramp up
  DoubleSnare,   // two hits on the last two 16ths
  TomRun,        // percussion run, descending
  GateDrop,      // drums out for the last 2-3 steps (no hits placed; marks the plan)
  Stutter8,      // repeats at 1/8 with decay
  Stutter16,     // repeats at 1/16 with decay
  Stutter32,     // repeats at 1/32, flat, optional falling pitch
  SpeedRamp,     // repeats stepping 1/16 -> 1/32 -> 1/64, rising
  ReverseSwell,  // reversed snare or crash that ends exactly on the bar line
  TapeStop,      // long event with a falling glide at the phrase end
  Crash,
};

struct Allow {
  bool reverse = false;
  bool pitch = false;
  bool retrigger = false;  // enables stutter gestures
  bool glide = false;      // enables tape stop
  bool filter = false;
  bool crunch = false;
};

// Which roles the source can actually play (the caller derives it from its chops).
struct Avail {
  bool hat = true;
  bool openHat = true;
  bool ghost = true;
  bool cymbal = true;
  bool perc = true;
};

// The part that is persisted with a pattern.
struct Options {
  Profile profile = Profile::Jungle;
  Backbeat backbeat = Backbeat::Standard;
  AnchorMode anchors = AnchorMode::Profile;
  bool kickSnareLayer = false;   // allow kick and snare on the same step (layered hit)
  double groove = 0.5;           // 0..1: swing and systematic laid-back offsets (0.5 = the profile as measured)
  double humanize = 0.5;         // 0..1: seeded timing and velocity jitter (0.5 = the measured spread)
  double fill = 0.5;             // 0..1: fill probability (0.5 = the profile as measured)
  double glitch = 0.5;           // 0..1: glitch gesture probability (0.5 = the profile as measured)
  double chaos = 0.0;            // 0..1: set by the Chaos macro; raises glitch, softens soft-profile anchors
  double phraseStrictness = -1;  // 0..1, negative = the profile default
  double tempoHint = 0.0;        // bpm used to turn milliseconds into ticks and to align reversed hits; 0 = 120
  friend bool operator==(const Options& a, const Options& b) {
    return a.profile == b.profile && a.backbeat == b.backbeat && a.anchors == b.anchors && a.kickSnareLayer == b.kickSnareLayer && a.groove == b.groove &&
           a.humanize == b.humanize && a.fill == b.fill && a.glitch == b.glitch && a.chaos == b.chaos && a.phraseStrictness == b.phraseStrictness &&
           a.tempoHint == b.tempoHint;
  }
};
bool valid(const Options& o);

// Everything a plan depends on.
struct PlanSettings {
  Options options;
  double density = 0.5;          // 0..1 position inside the profile's events-per-bar band
  double swing = 0.0;            // 0..1 position inside the profile's swing range
  double variationPhrase = 0.5;  // lowers phrase strictness when high
  double variationEvent = 0.5;   // chance a non-anchor hit gets a pitch / effect / alternate chop
  Allow allow;
  int pitchRange = 7;            // semitones, caps pitch variation
  double fxIntensity = 0.5;
  Avail avail;
  int bars = 4;                  // 1, 2, 4 or 8
  std::uint64_t seed = 1;
};

struct Hit {
  DrumRole role = DrumRole::Kick;  // Kick, Snare, Ghost, Hat, OpenHat, Cymbal or Perc
  int step = 0;                    // nominal 16th, 0..15
  Ticks start = 0;                 // within the bar, including swing, laid-back offset, jitter and gesture sub-steps
  Ticks duration = 0;              // 0 = the caller assigns it
  Ticks endAt = -1;                // >= 0: the (reversed) hit must END exactly here; the caller derives the start from the chop length
  Ticks maxWindow = 0;             // with endAt: longest window allowed (ticks)
  int velocity = 100;              // 1..127
  Gesture gesture = Gesture::None;
  int gestureIndex = 0;            // position inside the gesture, 0-based
  int gestureCount = 0;            // number of hits of this gesture instance
  bool anchor = false;             // backbeat snare, downbeat kick, displaced backbeat
  EventTransform tx;               // pitch and reverse; level is derived from `velocity` by the caller
  EventFx fx;
};

struct BarPlan {
  int bar = 0;
  int slot = 0;                    // phrase slot 0..3 (bar 1..4 of the phrase)
  std::vector<Hit> hits;           // sorted by (start, role)
  std::vector<Gesture> gestures;   // fill and glitch gestures placed in this bar (for reports and tests)
};

// ---- planning ----
int phraseSlot(int bar, int bars);                           // 1 bar: 0; 2 bars: 0,3; otherwise bar % 4
BarPlan planBar(const PlanSettings& s, int bar);             // stateless: depends only on (settings, bar)
std::vector<BarPlan> planPattern(const PlanSettings& s);
double velocityToLevel(int velocity);                        // vel / 127
int levelToVelocity(double level);

// ---- tables and derived expectations (used by tests and the audition report) ----
struct ProfileInfo {
  const char* name;
  int densityLo, densityHi;     // events per bar without hats at density 0 and 1
  double swingLo, swingHi;      // engine swing (0..1) at swing 0 and 1
  double ghostX, fillX, glitchX;
  bool softAnchors;
  double anchorFloor;           // lowest anchor probability the profile allows
  double strictness;            // default phrase strictness
  int pitchSemitones;           // pitch variation allowed for non-anchor hits
  bool glitchProfile;           // exempt from the per-bar event cap
};
const ProfileInfo& profileInfo(Profile p);
const char* profileName(Profile p);

// Pre-resolution probability that a role plays on a step of a phrase slot (kick, snare, ghost only), including
// profile multipliers, density scaling, anchor protection and displacement. 0 for roles without a table.
double cellProbability(const PlanSettings& s, int slot, DrumRole role, int step);
// Effective anchor probabilities for the options (snare on step 1 of the backbeat pair, second backbeat, kick on 1).
double anchorProbability(const Options& o, bool secondBackbeat, bool kick);
// Probability that a fill (any fill gesture) is attempted in the bar of a phrase slot.
double fillProbability(const PlanSettings& s, int slot);
double effectivePhraseStrictness(const PlanSettings& s);
double effectiveSwing(const PlanSettings& s);   // engine swing 0..1 actually applied to non-anchor odd steps
// Largest absolute jitter plus laid-back offset (ms) a non-gesture hit of this role can receive.
double maxTimingMs(const PlanSettings& s, DrumRole role, bool anchor);
Ticks msToTicks(double ms, double bpm);

// Role/step view used by rule checks and by mutate, independent of how the hit was produced.
struct Cell {
  DrumRole role = DrumRole::Kick;
  int step = 0;
  bool gesture = false;
};
// Would adding `add` to `present` break a rule of section 6.4 (collision, kick adjacency, ghost caps, open hat,
// per-bar cap)? Anchors and gesture hits are exempt from the density-style rules but never from collisions.
bool canAdd(const PlanSettings& s, const std::vector<Cell>& present, const Cell& add, bool anchor);
bool isAnchorCell(DrumRole role, int step, bool snareAtStep12Missing);

}  // namespace chopfractal::breakeng
