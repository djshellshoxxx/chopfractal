// Probability model, anchor protection and the collision / density rules (research sections 6.1, 6.2, 6.4, 6.5).
#include "internal.hpp"

#include <algorithm>
#include <cstdlib>

namespace chopfractal::breakeng {
namespace detail {

int slotOf(int bar, int bars) {
  if (bars <= 1) return 0;
  if (bars == 2) return bar == 0 ? 0 : 3;
  return ((bar % 4) + 4) % 4;
}

double groovePos(const Options& o) { return o.groove * 2.0; }
double humanizePos(const Options& o) { return o.humanize * 2.0; }
bool rollingOn(const Options& o) { return o.backbeat == Backbeat::Rolling || o.profile == Profile::DnbRolling; }

double strictAnchorP(const Options&, bool second) { return second ? kBackbeatP2 : kBackbeatP1; }

namespace {
enum class Family { Low, Snare, Hat, Cymbal, Perc };
Family familyOf(DrumRole r) {
  switch (r) {
    case DrumRole::Kick: return Family::Low;
    case DrumRole::Snare:
    case DrumRole::Ghost: return Family::Snare;
    case DrumRole::Hat:
    case DrumRole::OpenHat: return Family::Hat;
    case DrumRole::Cymbal: return Family::Cymbal;
    default: return Family::Perc;
  }
}

// Probability before density scaling and slot modifiers, for roles that have a per-step table.
double rawProbability(const PlanSettings& s, DrumRole role, int step) {
  const StepRow& r = kRows[step];
  const ProfileInfo& pi = profileRow(s.options.profile);
  switch (role) {
    case DrumRole::Kick: return step == 0 ? 0.0 : r.kickP;
    case DrumRole::Snare:
      return (step == 4 || step == 12) ? 0.0 : r.snareP;
    case DrumRole::Ghost: {
      if (!s.avail.ghost) return 0.0;
      double p = r.ghostP;
      if (rollingOn(s.options) && (step == 5 || step == 13)) p = 0.80;
      return p * pi.ghostX;
    }
    default: return 0.0;
  }
}

double displaceP(const PlanSettings& s, int slot) {
  if (s.options.anchors == AnchorMode::Strict) return 0.0;
  return profileTiming(s.options.profile).displace[slot];
}
}  // namespace

// Expected non-anchor event mass of the neutral table, used to map the density control onto the profile's band.
double densityScale(const PlanSettings& s) {
  const ProfileInfo& pi = profileRow(s.options.profile);
  const double d = clampd(s.density, 0.0, 1.0);
  const double target = pi.densityLo + (pi.densityHi - pi.densityLo) * d;
  double mass = 0.0;
  for (int st = 0; st < kStepsPerBar; ++st) {
    mass += rawProbability(s, DrumRole::Kick, st) + rawProbability(s, DrumRole::Snare, st) + rawProbability(s, DrumRole::Ghost, st);
  }
  // Anchors the profile and mode actually produce (displacement does not change the count), and the share of drawn
  // hits the collision / adjacency / ghost rules throw away again (measured over the profiles, see tests).
  const double anchors = anchorProbability(s.options, false, true) * 0.93 + anchorProbability(s.options, false, false) + anchorProbability(s.options, true, false);
  const double loss = profileTiming(s.options.profile).ruleLoss;
  const double scale = mass > 0.0 ? (target - anchors) / (mass * loss) : 1.0;
  return clampd(scale, 0.25, 3.0);
}

}  // namespace detail

using namespace detail;

int phraseSlot(int bar, int bars) { return slotOf(bar, bars); }
double velocityToLevel(int velocity) { return static_cast<double>(clampi(velocity, 0, 127)) / 127.0; }
int levelToVelocity(double level) {
  const double v = level * 127.0 + 0.5;
  return clampi(static_cast<int>(v < 0.0 ? 0.0 : (v > 127.0 ? 127.0 : v)), 0, 127);
}

Ticks msToTicks(double ms, double bpm) {
  const double b = bpm > 0.0 ? bpm : 120.0;
  const double t = ms * b * (static_cast<double>(kTicksPerQuarter) / 60000.0);
  return static_cast<Ticks>(t < 0.0 ? -static_cast<double>(static_cast<Ticks>(-t + 0.5)) : static_cast<double>(static_cast<Ticks>(t + 0.5)));
}

double anchorProbability(const Options& o, bool secondBackbeat, bool kick) {
  const double strict = kick ? kKickAnchorP : strictAnchorP(o, secondBackbeat);
  const ProfileInfo& pi = profileRow(o.profile);
  switch (o.anchors) {
    case AnchorMode::Strict: return strict;
    case AnchorMode::Free: return std::min(strict, 0.55);
    case AnchorMode::Profile: break;
  }
  if (!pi.softAnchors) return strict;
  const double w = std::max(0.5, clampd(o.chaos, 0.0, 1.0));
  return pi.anchorFloor + (strict - pi.anchorFloor) * (1.0 - w);
}

double effectivePhraseStrictness(const PlanSettings& s) {
  const double base = s.options.phraseStrictness >= 0.0 ? s.options.phraseStrictness : profileRow(s.options.profile).strictness;
  return clampd(base - (s.variationPhrase - 0.5) * 0.8, 0.0, 1.0);
}

double effectiveSwing(const PlanSettings& s) {
  const ProfileInfo& pi = profileRow(s.options.profile);
  const double sw = pi.swingLo + (pi.swingHi - pi.swingLo) * clampd(s.swing, 0.0, 1.0);
  return clampd(sw * groovePos(s.options), 0.0, 1.0);
}

double fillProbability(const PlanSettings& s, int slot) {
  const ProfileInfo& pi = profileRow(s.options.profile);
  double base = kFillChance[clampi(slot, 0, 3)];
  if (s.bars == 1) base = kFillChance[3] * 0.5;  // a one-bar loop turns around at its end every other time
  return std::min(0.95, base * pi.fillX * 2.0 * s.options.fill);
}

double cellProbability(const PlanSettings& s, int slot, DrumRole role, int step) {
  if (step < 0 || step >= kStepsPerBar) return 0.0;
  slot = clampi(slot, 0, 3);
  const ProfileTiming& pt = profileTiming(s.options.profile);
  const double dens = densityScale(s);
  switch (role) {
    case DrumRole::Kick:
      if (step == 0) return std::min(kMaxCellP, anchorProbability(s.options, false, true) * pt.kick1Mult[slot]);
      return std::min(kMaxCellP, rawProbability(s, role, step) * kOtherKickMod[slot] * dens);
    case DrumRole::Snare: {
      const double d = displaceP(s, slot);
      if (step == 4) return anchorProbability(s.options, false, false);
      if (step == 12) return anchorProbability(s.options, true, false) * (1.0 - d);
      if (step == 14) return std::min(kMaxCellP, rawProbability(s, role, step) * dens + anchorProbability(s.options, true, false) * d);
      return std::min(kMaxCellP, rawProbability(s, role, step) * dens);
    }
    case DrumRole::Ghost:
      return std::min(kMaxCellP, rawProbability(s, role, step) * kGhostMod[slot] * dens);
    default: return 0.0;
  }
}

double maxTimingMs(const PlanSettings& s, DrumRole, bool anchor) {
  const bool laidBack = s.options.profile == Profile::HipHop;
  return anchor && laidBack ? 20.0 : 12.0;
}

bool isAnchorCell(DrumRole role, int step, bool snareAtStep12Missing) {
  if (role == DrumRole::Kick) return step == 0;
  if (role == DrumRole::Snare) return step == 4 || step == 12 || (step == 14 && snareAtStep12Missing);
  return false;
}

bool canAdd(const PlanSettings& s, const std::vector<Cell>& present, const Cell& add, bool anchor) {
  const ProfileInfo& pi = profileRow(s.options.profile);
  const Family fam = familyOf(add.role);
  // One hit per family and step (gesture sub-steps of one gesture may share a step).
  for (const Cell& c : present)
    if (c.step == add.step && familyOf(c.role) == fam && !(add.gesture && c.gesture)) return false;
  // Kick and snare family never share a step unless layered.
  if (!s.options.kickSnareLayer) {
    if (add.role == DrumRole::Kick) {
      for (const Cell& c : present)
        if (c.step == add.step && familyOf(c.role) == Family::Snare) return false;
    } else if (fam == Family::Snare) {
      for (const Cell& c : present)
        if (c.step == add.step && c.role == DrumRole::Kick) return false;
    }
  }
  if (anchor || add.gesture) return true;
  // Adjacent low hits: at most two kick / snare hits inside any three consecutive steps.
  if (!pi.glitchProfile && (add.role == DrumRole::Kick || add.role == DrumRole::Snare)) {
    for (int w = add.step - 2; w <= add.step; ++w) {
      if (w < 0 || w + 2 >= kStepsPerBar) continue;
      int n = 1;
      for (const Cell& c : present)
        if ((c.role == DrumRole::Kick || c.role == DrumRole::Snare) && c.step >= w && c.step <= w + 2) ++n;
      if (n > 2) return false;
    }
  }
  // Ghost caps: at most 6 per bar, 2 per beat, never three consecutive snare-family steps.
  if (add.role == DrumRole::Ghost || add.role == DrumRole::Snare) {
    bool snareStep[kStepsPerBar] = {};
    int ghosts = 0, ghostsInBeat = 0;
    for (const Cell& c : present) {
      if (c.gesture) continue;
      if (c.role == DrumRole::Snare || c.role == DrumRole::Ghost) snareStep[c.step] = true;
      if (c.role == DrumRole::Ghost) {
        ++ghosts;
        if (c.step / 4 == add.step / 4) ++ghostsInBeat;
      }
    }
    snareStep[add.step] = true;
    if (!pi.glitchProfile)
      for (int w = 0; w + 2 < kStepsPerBar; ++w)
        if (snareStep[w] && snareStep[w + 1] && snareStep[w + 2]) return false;
    if (add.role == DrumRole::Ghost && (ghosts >= 6 || ghostsInBeat >= (pi.glitchProfile ? 3 : 2))) return false;
  }
  // Open hat: one per bar outside glitch profiles, never two within two steps.
  if (add.role == DrumRole::OpenHat) {
    for (const Cell& c : present)
      if (c.role == DrumRole::OpenHat && (!pi.glitchProfile || std::abs(c.step - add.step) <= 2)) return false;
  }
  // Events per bar without hats and without gestures.
  if (add.role != DrumRole::Hat && add.role != DrumRole::OpenHat) {
    int n = 0;
    for (const Cell& c : present)
      if (!c.gesture && c.role != DrumRole::Hat && c.role != DrumRole::OpenHat) ++n;
    const int cap = pi.glitchProfile ? pi.densityHi + 4 : std::min(12, pi.densityHi + 2);
    if (n >= cap) return false;
  }
  return true;
}

}  // namespace chopfractal::breakeng
