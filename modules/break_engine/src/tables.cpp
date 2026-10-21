// Rule tables of the break engine. Every number has a source:
//
//  * kRows ............ docs/research/BREAK_ENGINE_RESEARCH.md table 6.1 (per-step, per-role probability and velocity;
//                       velocity hierarchy 4.1). Ghost velocity range 20..48 agrees with BREAK_ANALYSIS.md ("ghosts at 0.10
//                       to 0.35 of 127"); backbeat and kick anchors sit above the measured step-0 / step-4 / step-12 onset
//                       probabilities (funk .87/.82/.72, hip-hop .80/.83/.70), which are the human floor the engine exceeds.
//  * kOtherKickMod etc. research table 6.2 (4-bar phrase modifiers); BREAK_ANALYSIS.md rule 10 (bar 4 carries 8-15 % more onsets).
//  * kFillRows ........ research table 6.3 (fill and glitch gestures), 4.3 (roll and stutter conventions).
//  * profiles ......... research table 6.5 plus the timing rules of 4.2 (swing, laid-back snare, late second kick).
//
// Data attribution: Groove MIDI Dataset v1.0.0, Magenta, Creative Commons Attribution 4.0 (see THIRD_PARTY_NOTICES.md).
#include "internal.hpp"

namespace chopfractal::breakeng {
namespace detail {

// clang-format off
//                    kick p  mu lo hi     snare p  mu lo hi     ghost p  mu lo hi     hat p   mu lo hi      open p
const StepRow kRows[kStepsPerBar] = {
    /* 1  1   */ {0.95, 118, 105, 127,  0.00,   0,   0,   0,  0.00,  0,  0,  0,  0.90, 100, 90, 110,  0.00},
    /* 2  e   */ {0.04,  85,  70, 100,  0.00,   0,   0,   0,  0.08, 28, 20, 45,  0.80,  68, 50,  85,  0.00},
    /* 3  &   */ {0.45, 102,  85, 115,  0.00,   0,   0,   0,  0.06, 28, 20, 45,  0.92,  88, 76, 100,  0.02},
    /* 4  a   */ {0.28,  95,  80, 110,  0.00,   0,   0,   0,  0.14, 30, 20, 48,  0.78,  68, 50,  85,  0.00},
    /* 5  2   */ {0.00,   0,   0,   0,  0.97, 110, 100, 127,  0.00,  0,  0,  0,  0.90, 100, 90, 110,  0.00},
    /* 6  e   */ {0.05,  80,  65,  95,  0.02,  70,  60,  85,  0.45, 30, 20, 48,  0.75,  66, 50,  85,  0.00},
    /* 7  &   */ {0.18,  92,  75, 108,  0.00,   0,   0,   0,  0.12, 28, 20, 45,  0.92,  88, 76, 100,  0.03},
    /* 8  a   */ {0.10,  85,  70, 100,  0.00,   0,   0,   0,  0.38, 30, 20, 48,  0.78,  68, 50,  85,  0.02},
    /* 9  3   */ {0.72, 112, 100, 125,  0.00,   0,   0,   0,  0.00,  0,  0,  0,  0.90, 100, 90, 110,  0.00},
    /* 10 e   */ {0.38,  98,  80, 112,  0.00,   0,   0,   0,  0.40, 30, 20, 48,  0.75,  66, 50,  85,  0.00},
    /* 11 &   */ {0.42, 100,  85, 115,  0.00,   0,   0,   0,  0.08, 28, 20, 45,  0.92,  88, 76, 100,  0.04},
    /* 12 a   */ {0.12,  88,  70, 105,  0.00,   0,   0,   0,  0.14, 30, 20, 48,  0.78,  68, 50,  85,  0.00},
    /* 13 4   */ {0.00,   0,   0,   0,  0.95, 112, 100, 127,  0.00,  0,  0,  0,  0.90, 100, 90, 110,  0.00},
    /* 14 e   */ {0.04,  80,  65,  95,  0.03,  72,  60,  85,  0.42, 30, 20, 48,  0.75,  66, 50,  85,  0.03},
    /* 15 &   */ {0.12,  90,  75, 105,  0.04,  80,  65,  95,  0.12, 28, 20, 45,  0.92,  88, 76, 100,  0.04},
    /* 16 a   */ {0.10,  88,  70, 105,  0.05,  78,  60,  95,  0.35, 36, 22, 60,  0.78,  68, 50,  85,  0.10},
};

// Phrase modifiers, bars 1..4 of the phrase (table 6.2). Kick and ghost modifiers have mean 1.0 so the density mapping holds.
const double kOtherKickMod[4] = {0.6, 0.8, 1.2, 1.4};
const double kGhostMod[4]     = {0.9, 0.9, 1.0, 1.2};
const double kFillChance[4]   = {0.0, 0.20, 0.10, 0.75};

const GestureRow kFillRows[5] = {
    {Gesture::Roll16,      0.45, 0.04, 0.45},
    {Gesture::Roll32,      0.20, 0.02, 0.20},
    {Gesture::DoubleSnare, 0.30, 0.05, 0.30},
    {Gesture::TomRun,      0.15, 0.00, 0.15},
    {Gesture::GateDrop,    0.08, 0.00, 0.08},
};
const GestureRow kGlitchRows[6] = {
    {Gesture::Stutter8,     0.10, 0.06, 1.0},
    {Gesture::Stutter16,    0.15, 0.08, 1.0},
    {Gesture::Stutter32,    0.10, 0.05, 1.0},
    {Gesture::SpeedRamp,    0.08, 0.02, 1.0},
    {Gesture::ReverseSwell, 0.12, 0.02, 1.0},
    {Gesture::TapeStop,     0.06, 0.01, 1.0},
};

// name,              dens,   swing,       ghost fill glitch soft  floor strict pitch glitchProfile
static const ProfileInfo kProfiles[kProfileCount] = {
    {"Amen / Funk",      6, 10, 0.30, 0.60, 1.0, 0.8, 0.3, false, 1.00, 0.85,  0, false},
    {"Jungle",           8, 12, 0.00, 0.24, 1.3, 1.0, 1.0, false, 1.00, 0.85,  1, false},
    {"DnB rolling",     10, 12, 0.00, 0.20, 1.6, 1.0, 0.8, false, 1.00, 0.80,  1, false},
    {"Breakcore",       10, 16, 0.00, 0.10, 1.0, 1.5, 3.0, true,  0.70, 0.30,  7, true},
    {"IDM / glitch",     6, 14, 0.00, 0.60, 0.7, 1.2, 3.0, true,  0.50, 0.25, 12, true},
    {"Hip-hop boom-bap", 5,  8, 0.45, 0.70, 0.8, 0.5, 0.2, false, 1.00, 0.90,  0, false},
};

//   kick1Mult[4]                 displace[4]                   hat16 crash snareLate kick2Late ghostLate hatSigma open loss
static const ProfileTiming kTimings[kProfileCount] = {
    {{1.00, 1.00, 1.00, 0.75}, {0.00, 0.00, 0.40, 0.45}, 0.55, 1.0,  4.0,  0.0, 6.0, 4.0, 1.0, 0.80},
    {{1.00, 0.95, 1.00, 0.85}, {0.00, 0.00, 0.00, 0.25}, 0.35, 0.5,  0.0, 12.0, 4.0, 2.5, 1.0, 0.68},
    {{1.00, 1.00, 1.00, 0.90}, {0.00, 0.00, 0.00, 0.10}, 0.85, 0.5,  0.0, 12.0, 4.0, 2.5, 1.0, 0.60},
    {{1.00, 0.85, 0.85, 0.60}, {0.00, 0.20, 0.30, 0.40}, 0.50, 1.0,  0.0,  0.0, 0.0, 2.0, 1.0, 0.80},
    {{0.90, 0.80, 0.70, 0.50}, {0.00, 0.20, 0.30, 0.40}, 0.30, 1.0,  0.0,  0.0, 0.0, 3.0, 1.0, 0.80},
    {{1.00, 1.00, 1.00, 0.90}, {0.00, 0.00, 0.00, 0.15}, 0.20, 0.7, 10.0,  0.0, 6.0, 3.0, 0.7, 0.80},
};
// clang-format on

const ProfileTiming& profileTiming(Profile p) {
  const int i = static_cast<int>(p);
  return kTimings[i >= 0 && i < kProfileCount ? i : 1];
}

const ProfileInfo& profileRow(Profile p) {
  const int i = static_cast<int>(p);
  return kProfiles[i >= 0 && i < kProfileCount ? i : 1];
}

}  // namespace detail

const ProfileInfo& profileInfo(Profile p) { return detail::profileRow(p); }
const char* profileName(Profile p) { return detail::profileRow(p).name; }

bool valid(const Options& o) {
  auto in01 = [](double v) { return v >= 0.0 && v <= 1.0; };
  const bool tempoOk = o.tempoHint == 0.0 || (o.tempoHint >= 20.0 && o.tempoHint <= 999.0);
  return static_cast<int>(o.profile) < kProfileCount && static_cast<int>(o.backbeat) <= 1 &&
         static_cast<int>(o.anchors) <= 2 && in01(o.groove) && in01(o.humanize) && in01(o.fill) && in01(o.glitch) && in01(o.chaos) &&
         (o.phraseStrictness < 0.0 || in01(o.phraseStrictness)) && tempoOk;
}

}  // namespace chopfractal::breakeng
