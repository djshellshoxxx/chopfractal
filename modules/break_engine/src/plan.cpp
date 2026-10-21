// Bar planner: anchors first, then fills, kicks, glitch gestures, ghosts, hats, crash; every addition passes the
// collision and density rules; timing and velocity are applied last (research sections 5, 6, 4.1, 4.2, 4.3).
#include "internal.hpp"

#include <algorithm>

namespace chopfractal::breakeng {
using namespace detail;

namespace {

constexpr Ticks kBarTicks = kBarTicks44;
constexpr int kRoleCount = 9;

int roleIndex(DrumRole r) { return static_cast<int>(r) % kRoleCount; }

int roundVel(double v) { return static_cast<int>(v + 0.5); }

class Planner {
 public:
  Planner(const PlanSettings& s, int bar)
      : s_(s), o_(s.options), bar_(bar), slot_(slotOf(bar, s.bars)), pi_(profileRow(s.options.profile)), pt_(profileTiming(s.options.profile)) {
    strict_ = effectivePhraseStrictness(s);
    hp_ = humanizePos(o_);
    gp_ = groovePos(o_);
    bpm_ = o_.tempoHint > 0.0 ? o_.tempoHint : 120.0;
    glitchEff_ = std::max(o_.glitch, o_.chaos);
  }

  BarPlan run() {
    plan_.bar = bar_;
    plan_.slot = slot_;
    anchors();
    placeFill();
    kicks();
    forceMinimum();
    placeGlitch();
    ghosts();
    weakSnares();
    hats();
    crash();
    applyGate();
    variation();
    timing();
    std::stable_sort(hits_.begin(), hits_.end(), [](const Hit& a, const Hit& b) {
      if (a.start != b.start) return a.start < b.start;
      if (a.role != b.role) return static_cast<int>(a.role) < static_cast<int>(b.role);
      return a.gestureIndex < b.gestureIndex;
    });
    plan_.hits = std::move(hits_);
    return std::move(plan_);
  }

 private:
  const PlanSettings& s_;
  const Options& o_;
  int bar_, slot_;
  const ProfileInfo& pi_;
  const ProfileTiming& pt_;
  double strict_ = 0.85, hp_ = 1.0, gp_ = 1.0, bpm_ = 120.0, glitchEff_ = 0.5;
  BarPlan plan_;
  std::vector<Hit> hits_;
  std::vector<Cell> cells_;
  bool displaced_ = false;
  bool fillPlaced_ = false;
  int gateFrom_ = kStepsPerBar;

  // ---- random access with phrase strictness ----
  // Cells of bar 2 (and, less often, 3 and 4) reuse the draws of the phrase's first bar, so the groove repeats
  // with a controlled amount of variation instead of being redrawn.
  int copyBar(std::uint64_t tag, int step) const {
    if (slot_ == 0 || s_.bars == 1) return bar_;
    const double weight = slot_ == 1 ? 1.0 : (slot_ == 2 ? 0.5 : 0.25);
    Rng d = cellRng(s_, kTagCopy, static_cast<std::uint64_t>(bar_), tag * 32 + static_cast<std::uint64_t>(step));
    if (d.uniform01() < strict_ * weight) return s_.bars == 2 ? 0 : bar_ - slot_;
    return bar_;
  }
  double u(std::uint64_t tag, int step) const {
    Rng r = cellRng(s_, tag, static_cast<std::uint64_t>(copyBar(tag, step)), static_cast<std::uint64_t>(step));
    return r.uniform01();
  }

  // ---- hit construction ----
  int velocityFor(int mu, int lo, int hi, double sigma, DrumRole role, int step) const {
    Rng r = cellRng(s_, kTagVel, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(step * 16 + roleIndex(role)));
    return clampi(roundVel(static_cast<double>(mu) + sigma * hp_ * gauss(r)), lo, hi);
  }

  bool add(Hit h, bool anchor, bool gesture) {
    h.anchor = anchor;
    const Cell c{h.role, h.step, gesture};
    if (!canAdd(s_, cells_, c, anchor)) return false;
    hits_.push_back(h);
    cells_.push_back(c);
    return true;
  }

  Hit gestureHit(DrumRole role, Ticks tick, int velocity, Gesture g, int index, int count) const {
    Hit h;
    h.role = role;
    h.step = static_cast<int>(std::min<Ticks>(tick / kStepTicks, kStepsPerBar - 1));
    h.start = tick;
    h.gesture = g;
    h.gestureIndex = index;
    h.gestureCount = count;
    Rng r = cellRng(s_, kTagVel, static_cast<std::uint64_t>(bar_), 4096 + static_cast<std::uint64_t>(tick) * 16 + static_cast<std::uint64_t>(roleIndex(role)));
    const double jitter = clampd(2.0 * hp_ * gauss(r), -3.0, 3.0);  // per-repeat variation, small enough to keep ramps monotone
    h.velocity = clampi(roundVel(static_cast<double>(velocity) + jitter), 1, 127);
    return h;
  }

  void pitchJitter(Hit& h, double rampSemitones) const {
    if (!s_.allow.pitch) return;
    Rng r = cellRng(s_, kTagFx, static_cast<std::uint64_t>(bar_), 8192 + static_cast<std::uint64_t>(h.start) * 4 + static_cast<std::uint64_t>(h.gestureIndex));
    h.tx.pitchSemitones = static_cast<float>(rampSemitones + (r.uniform01() * 2.0 - 1.0) * 0.3);
  }

  // ---- 1. anchors ----
  void anchors() {
    const StepRow& k0 = kRows[0];
    if (u(kTagKick, 0) < cellProbability(s_, slot_, DrumRole::Kick, 0)) {
      Hit h;
      h.role = DrumRole::Kick;
      h.step = 0;
      h.velocity = velocityFor(k0.kMu, k0.kLo, k0.kHi, 4.0, DrumRole::Kick, 0);
      add(h, true, false);
    }
    const StepRow& s4 = kRows[4];
    if (u(kTagSnare, 4) < anchorProbability(o_, false, false)) {
      Hit h;
      h.role = DrumRole::Snare;
      h.step = 4;
      h.velocity = velocityFor(s4.sMu, s4.sLo, s4.sHi, 4.0, DrumRole::Snare, 4);
      add(h, true, false);
    }
    const double d = o_.anchors == AnchorMode::Strict ? 0.0 : pt_.displace[slot_];
    const bool wantDisplace = u(kTagDisplace, 12) < d;
    if (u(kTagSnare, 12) < anchorProbability(o_, true, false)) {
      const StepRow& s12 = kRows[12];
      Hit h;
      h.role = DrumRole::Snare;
      h.step = wantDisplace ? 14 : 12;
      displaced_ = wantDisplace;
      // The second backbeat is played a little harder than the first (research 4.1: 5 to 8 louder).
      h.velocity = velocityFor(std::min(s12.sHi, s12.sMu + 4), s12.sLo, s12.sHi, 4.0, DrumRole::Snare, h.step);
      add(h, true, false);
    }
  }

  void forceMinimum() {
    const bool force = o_.anchors == AnchorMode::Strict || (o_.anchors == AnchorMode::Profile && !pi_.softAnchors);
    if (!force) return;
    bool kick = false, snare = false;
    for (const Hit& h : hits_) {
      if (h.role == DrumRole::Kick) kick = true;
      if (h.role == DrumRole::Snare) snare = true;
    }
    if (!kick) {
      const StepRow& r = kRows[8];
      Hit h;
      h.role = DrumRole::Kick;
      h.step = 8;
      h.velocity = velocityFor(r.kMu, r.kLo, r.kHi, 4.0, DrumRole::Kick, 8);
      if (!add(h, false, false)) {
        h.step = 0;
        add(h, true, false);
      }
    }
    if (!snare) {
      const StepRow& r = kRows[4];
      Hit h;
      h.role = DrumRole::Snare;
      h.step = 4;
      h.velocity = velocityFor(r.sMu, r.sLo, r.sHi, 4.0, DrumRole::Snare, 4);
      add(h, true, false);
    }
  }

  // ---- 2. fills ----
  void placeFill() {
    const double fp = fillProbability(s_, slot_);
    Rng fr = cellRng(s_, kTagFill, static_cast<std::uint64_t>(bar_), 0);
    const double uFill = fr.uniform01(), uPick = fr.uniform01(), uCount = fr.uniform01();
    if (!(uFill < fp)) return;
    double weights[5];
    double total = 0.0;
    for (int i = 0; i < 5; ++i) {
      weights[i] = kFillRows[i].weight;
      if (kFillRows[i].g == Gesture::TomRun && !s_.avail.perc) weights[i] = 0.0;
      if (kFillRows[i].g == Gesture::GateDrop && (displaced_ || slot_ != 3)) weights[i] = 0.0;  // drums-out only at the phrase end (research metric 25)
      total += weights[i];
    }
    if (!(total > 0.0)) return;
    double target = uPick * total, acc = 0.0;
    Gesture g = kFillRows[4].g;
    for (int i = 0; i < 5; ++i) {
      acc += weights[i];
      if (target < acc) {
        g = kFillRows[i].g;
        break;
      }
    }
    switch (g) {
      case Gesture::Roll16: roll16(uCount); break;
      case Gesture::Roll32: roll32(uCount); break;
      case Gesture::DoubleSnare: doubleSnare(); break;
      case Gesture::TomRun: tomRun(); break;
      case Gesture::GateDrop: gateFrom_ = 16 - (2 + static_cast<int>(uCount * 2.0)); break;
      default: break;
    }
    plan_.gestures.push_back(g);
    fillPlaced_ = true;
  }

  void roll16(double uCount) {
    const int n = 4 + 2 * static_cast<int>(uCount * 3.0);  // 4, 6 or 8 hits on the last n sixteenths
    int lastVel = 0;
    for (int k = 0; k < n; ++k) {
      const double frac = 0.50 + 0.50 * static_cast<double>(k) / static_cast<double>(n - 1);  // ramp 50 -> 100 percent
      Hit h = gestureHit(DrumRole::Snare, static_cast<Ticks>(16 - n + k) * kStepTicks, roundVel(frac * 110.0), Gesture::Roll16, k, n);
      h.velocity = std::max(h.velocity, lastVel);
      pitchJitter(h, 2.0 * static_cast<double>(k) / static_cast<double>(n - 1));
      if (add(h, false, true)) lastVel = h.velocity;
    }
  }

  void roll32(double uCount) {
    const int n = uCount < 0.6 ? 4 : 6;  // 1/32 hits ending at the bar line
    int lastVel = 0;
    for (int k = 0; k < n; ++k) {
      const double frac = 0.40 + 0.60 * static_cast<double>(k) / static_cast<double>(n - 1);
      Hit h = gestureHit(DrumRole::Snare, kBarTicks - static_cast<Ticks>(n - k) * (kStepTicks / 2), roundVel(frac * 110.0), Gesture::Roll32, k, n);
      h.velocity = std::max(h.velocity, lastVel);
      pitchJitter(h, 2.0 * static_cast<double>(k) / static_cast<double>(n - 1));
      if (add(h, false, true)) lastVel = h.velocity;
    }
  }

  void doubleSnare() {
    add(gestureHit(DrumRole::Snare, 14 * kStepTicks, 85, Gesture::DoubleSnare, 0, 2), false, true);
    add(gestureHit(DrumRole::Snare, 15 * kStepTicks, 100, Gesture::DoubleSnare, 1, 2), false, true);
  }

  void tomRun() {
    for (int k = 0; k < 4; ++k) add(gestureHit(DrumRole::Perc, static_cast<Ticks>(12 + k) * kStepTicks, 110 - 7 * k, Gesture::TomRun, k, 4), false, true);
  }

  void applyGate() {
    if (gateFrom_ >= kStepsPerBar) return;
    for (std::size_t i = 0; i < hits_.size();) {
      const Hit& h = hits_[i];
      if (h.step >= gateFrom_ && !h.anchor && h.gesture != Gesture::Crash) {
        hits_.erase(hits_.begin() + static_cast<std::ptrdiff_t>(i));
        cells_.erase(cells_.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
  }

  // ---- 3. kicks ----
  std::vector<int> stepsByProbability(DrumRole role, int from) const {
    std::vector<int> order;
    for (int st = from; st < kStepsPerBar; ++st) order.push_back(st);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cellProbability(s_, slot_, role, a) > cellProbability(s_, slot_, role, b); });
    return order;
  }

  void kicks() {
    for (int st : stepsByProbability(DrumRole::Kick, 1)) {
      const double p = cellProbability(s_, slot_, DrumRole::Kick, st);
      if (!(p > 0.0) || !(u(kTagKick, st) < p)) continue;
      const StepRow& r = kRows[st];
      Hit h;
      h.role = DrumRole::Kick;
      h.step = st;
      h.velocity = velocityFor(r.kMu, r.kLo, r.kHi, 4.0, DrumRole::Kick, st);
      add(h, false, false);
    }
  }

  // ---- 4. glitch gestures ----
  void placeGlitch() {
    const double gs = pi_.glitchX * 2.0 * glitchEff_;
    if (!(gs > 0.0)) return;
    int placed = 0;
    for (int i = 0; i < 6 && placed < 2; ++i) {
      const GestureRow& row = kGlitchRows[i];
      if ((row.g == Gesture::ReverseSwell && !s_.allow.reverse) || (row.g == Gesture::TapeStop && !s_.allow.glide)) continue;
      if ((row.g == Gesture::Stutter8 || row.g == Gesture::Stutter16 || row.g == Gesture::Stutter32 || row.g == Gesture::SpeedRamp) && !s_.allow.retrigger) continue;
      if (row.g == Gesture::SpeedRamp && !pi_.glitchProfile) continue;  // 1/64 rolls belong to the glitch profiles only (research metric 13)
      const double p = std::min(0.9, (slot_ == 3 ? row.pPhrase : row.pBar) * gs);
      Rng gr = cellRng(s_, kTagGlitch, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(i));
      const double ug = gr.uniform01(), u1 = gr.uniform01(), u2 = gr.uniform01();
      if (!(ug < p)) continue;
      bool ok = false;
      switch (row.g) {
        case Gesture::Stutter8: ok = stutter(u1, u2, Gesture::Stutter8, 480, 2, 3, 0.7079); break;
        case Gesture::Stutter16: ok = stutter(u1, u2, Gesture::Stutter16, 240, 3, 4, 0.8414); break;
        case Gesture::Stutter32: ok = stutter(u1, u2, Gesture::Stutter32, 120, 4, 6, 1.0); break;
        case Gesture::SpeedRamp: ok = speedRamp(u1); break;
        case Gesture::ReverseSwell: ok = reverseSwell(u1); break;
        case Gesture::TapeStop: ok = tapeStop(u1); break;
        default: break;
      }
      if (ok) {
        ++placed;
        plan_.gestures.push_back(row.g);
      }
    }
  }

  bool stutter(double u1, double u2, Gesture g, Ticks spacing, int minCount, int maxCount, double decay) {
    std::vector<std::size_t> sources;
    for (std::size_t i = 0; i < hits_.size(); ++i)
      if (hits_[i].gesture == Gesture::None && (hits_[i].role == DrumRole::Kick || hits_[i].role == DrumRole::Snare) && hits_[i].step <= 11) sources.push_back(i);
    if (sources.empty()) return false;
    const Hit src = hits_[sources[std::min(sources.size() - 1, static_cast<std::size_t>(u1 * static_cast<double>(sources.size())))]];
    const int count = minCount + (u2 < 0.5 ? 0 : maxCount - minCount);
    const Ticks base = static_cast<Ticks>(src.step) * kStepTicks;
    const int lastStep = static_cast<int>(std::min<Ticks>((base + spacing * (count - 1)) / kStepTicks, kStepsPerBar - 1));
    // The span after the source belongs to the stutter: clear same-family non-anchor hits in it first.
    const bool kickSrc = src.role == DrumRole::Kick;
    for (std::size_t i = 0; i < hits_.size();) {
      const Hit& h = hits_[i];
      const bool fam = kickSrc ? h.role == DrumRole::Kick : (h.role == DrumRole::Snare || h.role == DrumRole::Ghost);
      if (fam && !h.anchor && h.gesture == Gesture::None && h.step > src.step && h.step <= lastStep) {
        hits_.erase(hits_.begin() + static_cast<std::ptrdiff_t>(i));
        cells_.erase(cells_.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
    double vel = static_cast<double>(src.velocity);
    int placedHits = 0;
    for (int k = 1; k < count; ++k) {
      const Ticks tick = base + spacing * k;
      if (tick >= kBarTicks) break;
      vel *= decay;
      Hit h = gestureHit(src.role, tick, roundVel(vel), g, k, count);
      if (g == Gesture::Stutter32 && s_.allow.pitch) pitchJitter(h, -5.0 * static_cast<double>(k) / static_cast<double>(count - 1));
      else pitchJitter(h, 0.0);
      if (add(h, false, true)) ++placedHits;
    }
    return placedHits > 0;
  }

  bool speedRamp(double u1) {
    const int beat = 1 + static_cast<int>(u1 * 3.0);  // beats 2..4
    static const Ticks offsets[6] = {0, 240, 480, 600, 720, 780};
    const Ticks base = static_cast<Ticks>(beat) * kTicksPerQuarter;
    for (std::size_t i = 0; i < hits_.size();) {
      const Hit& h = hits_[i];
      if ((h.role == DrumRole::Snare || h.role == DrumRole::Ghost) && !h.anchor && h.gesture == Gesture::None && h.step >= beat * 4 && h.step < beat * 4 + 4) {
        hits_.erase(hits_.begin() + static_cast<std::ptrdiff_t>(i));
        cells_.erase(cells_.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
    int placedHits = 0, lastVel = 0;
    for (int k = 0; k < 6; ++k) {
      Hit h = gestureHit(DrumRole::Snare, base + offsets[k], 55 + 12 * k, Gesture::SpeedRamp, k, 6);
      h.velocity = std::max(h.velocity, lastVel);
      pitchJitter(h, 0.0);
      if (add(h, false, true)) {
        ++placedHits;
        lastVel = h.velocity;
      }
    }
    return placedHits > 0;
  }

  bool reverseSwell(double u1) {
    DrumRole role;
    if (s_.avail.cymbal) role = DrumRole::Cymbal;
    else if (s_.avail.ghost) role = DrumRole::Ghost;
    else return false;
    const Ticks window = u1 < 0.6 ? 4 * kStepTicks : 8 * kStepTicks;
    Hit h = gestureHit(role, kBarTicks - window, 88, Gesture::ReverseSwell, 0, 1);
    h.tx.reverse = true;
    h.endAt = kBarTicks;
    h.maxWindow = window;
    return add(h, false, true);
  }

  bool tapeStop(double u1) {
    if (fillPlaced_) return false;
    Hit h = gestureHit(DrumRole::Snare, 13 * kStepTicks, 82, Gesture::TapeStop, 0, 1);
    h.duration = kBarTicks - 13 * kStepTicks;
    h.fx.glideSemitones = -static_cast<float>(12 + 6 * static_cast<int>(u1 * 3.0));
    return add(h, false, true);
  }

  // ---- 5. ghosts and weak snares ----
  void ghosts() {
    if (!s_.avail.ghost) return;
    for (int st : stepsByProbability(DrumRole::Ghost, 1)) {
      const double p = cellProbability(s_, slot_, DrumRole::Ghost, st);
      if (!(p > 0.0) || !(u(kTagGhost, st) < p)) continue;
      const StepRow& r = kRows[st];
      Hit h;
      h.role = DrumRole::Ghost;
      h.step = st;
      h.velocity = velocityFor(r.gMu, r.gLo, r.gHi, 6.0, DrumRole::Ghost, st);
      if (st == 15) {  // a ghost that leads into the next bar is played harder (research 4.1, 62 .. 79)
        Rng r2 = cellRng(s_, kTagMisc, static_cast<std::uint64_t>(bar_), 15);
        if (r2.uniform01() < 0.25) h.velocity = clampi(roundVel(70.0 + 5.0 * hp_ * gauss(r2)), 55, 80);
      }
      add(h, false, false);
    }
  }

  void weakSnares() {
    for (int st : {5, 13, 14, 15}) {
      const double p = cellProbability(s_, slot_, DrumRole::Snare, st);
      if (!(p > 0.0)) continue;
      // The displaced backbeat already sits on step 15 / 14 as an anchor; this draw is only the weak extra snare.
      if (st == 14 && displaced_) continue;
      Rng r = cellRng(s_, kTagSnare, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(st) + 64);
      if (!(r.uniform01() < kRows[st].snareP * densityScale(s_))) continue;
      const StepRow& row = kRows[st];
      Hit h;
      h.role = DrumRole::Snare;
      h.step = st;
      h.velocity = velocityFor(row.sMu, row.sLo, row.sHi, 6.0, DrumRole::Snare, st);
      add(h, false, false);
    }
  }

  // ---- 6. hats ----
  void hats() {
    if (!s_.avail.hat) return;
    const bool rolling = rollingOn(o_);
    Rng mr = cellRng(s_, kTagHatMode, static_cast<std::uint64_t>(bar_), 0);
    const double hat16 = rolling ? std::max(pt_.hat16, 0.85) : pt_.hat16;
    const bool mode16 = mr.uniform01() < hat16;
    const double drift = (mr.uniform01() * 2.0 - 1.0) * 6.0;  // slow level wander of the hat bed from bar to bar
    int openStep = -1;
    if (s_.avail.openHat)
      for (int st = 0; st < kStepsPerBar && openStep < 0; ++st) {
        Rng orr = cellRng(s_, kTagOpen, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(st));
        if (orr.uniform01() < kRows[st].openP * pt_.openScale) openStep = st;
      }
    for (int st = 0; st < kStepsPerBar; ++st) {
      const StepRow& r = kRows[st];
      double p = (st % 2 == 0) ? (pi_.glitchProfile ? 0.97 : 1.0) : (mode16 ? std::min(0.97, r.hatP * (rolling ? 1.2 : 1.0)) : 0.0);
      Rng hr = cellRng(s_, kTagHat, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(st));
      const bool present = hr.uniform01() < p;
      const bool closedAfterOpen = openStep >= 0 && st == openStep + 1;
      const bool isOpen = st == openStep;
      if (!present && !closedAfterOpen && !isOpen) continue;
      Hit h;
      h.role = isOpen ? DrumRole::OpenHat : DrumRole::Hat;
      h.step = st;
      const int mu = isOpen ? 95 : roundVel(static_cast<double>(r.hMu) + drift);
      h.velocity = velocityFor(clampi(mu, r.hLo, r.hHi), isOpen ? 80 : r.hLo, isOpen ? 110 : r.hHi, 6.0, h.role, st);
      add(h, false, false);
    }
  }

  // ---- 7. crash ----
  void crash() {
    if (!s_.avail.cymbal) return;
    const int phrase = s_.bars >= 4 ? bar_ / 4 : 0;
    Rng pr = cellRng(s_, kTagCrash, static_cast<std::uint64_t>(phrase), 0);
    const double p3 = std::min(0.9, 0.35 * pt_.crashScale), p0 = std::min(0.9, 0.10 * pt_.crashScale);
    const double u0 = pr.uniform01(), u3 = pr.uniform01(), uStep = pr.uniform01();
    int chosen = -1;  // slot that carries the crash
    if (pi_.glitchProfile) {
      if (slot_ == 3 && u3 < p3) chosen = 3;
      if (slot_ == 0 && u0 < p0) chosen = 0;
    } else {
      // At most one crash per phrase: the turnaround wins over the downbeat.
      const int want = u3 < p3 ? 3 : (u0 < p0 ? 0 : -1);
      if (want == slot_) chosen = want;
    }
    if (chosen < 0) return;
    Hit h;
    h.role = DrumRole::Cymbal;
    h.step = chosen == 0 ? 0 : (uStep < 0.43 ? 7 : 8);
    h.start = static_cast<Ticks>(h.step) * kStepTicks;
    h.gesture = Gesture::Crash;
    h.gestureCount = 1;
    Rng vr = cellRng(s_, kTagVel, static_cast<std::uint64_t>(bar_), 2000);
    h.velocity = clampi(roundVel(105.0 + 6.0 * hp_ * gauss(vr)), 90, 127);
    if (add(h, false, true)) plan_.gestures.push_back(Gesture::Crash);
  }

  // ---- 8. variation: pitch, filter, glide, crush on non-anchor hits ----
  void variation() {
    const double ev = clampd(s_.variationEvent, 0.0, 1.0);
    if (!(ev > 0.0)) return;
    const int cap = std::min(pi_.pitchSemitones, std::max(0, s_.pitchRange));
    const double fi = clampd(s_.fxIntensity, 0.0, 1.0);
    for (Hit& h : hits_) {
      if (h.anchor || h.gesture != Gesture::None) continue;
      Rng r = cellRng(s_, kTagFx, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(h.step * 16 + roleIndex(h.role)));
      const double dPitch = r.uniform01(), vPitch = r.uniform01();
      const double dFilt = r.uniform01(), dFType = r.uniform01(), dCut = r.uniform01(), dRes = r.uniform01();
      const double dGlide = r.uniform01(), dGDir = r.uniform01(), dGMag = r.uniform01();
      const double dCrush = r.uniform01(), dCMag = r.uniform01();
      if (s_.allow.pitch && dPitch < 0.2 * ev) {
        if (cap == 0) h.tx.pitchSemitones = static_cast<float>((vPitch * 2.0 - 1.0) * 0.5);
        else h.tx.pitchSemitones = static_cast<float>(static_cast<int>(vPitch * static_cast<double>(2 * cap + 1)) - cap);
      }
      if (s_.allow.filter && dFilt < 0.3 * ev) {
        const bool low = dFType < 0.6;
        h.fx.filter = low ? FilterType::LowPass : FilterType::HighPass;
        h.fx.cutoff = static_cast<float>(low ? 1.0 - (1.0 - 0.35) * fi * dCut : 0.65 * fi * dCut);
        h.fx.resonance = static_cast<float>(0.6 * fi * dRes);
      }
      if (s_.allow.glide && dGlide < 0.2 * ev) {
        const double mag = (1.0 + 11.0 * dGMag) * fi;
        h.fx.glideSemitones = static_cast<float>(dGDir < 0.7 ? -mag : mag);
        if (h.fx.glideSemitones == 0.f) h.fx.glideSemitones = -0.5f;
      }
      if (s_.allow.crunch && dCrush < 0.2 * ev) h.fx.crush = static_cast<float>(std::max(0.05, 0.9 * fi * dCMag));
    }
  }

  // ---- 9. timing: swing on odd steps, laid-back offsets and seeded jitter per role ----
  void timing() {
    const double sw = effectiveSwing(s_);
    const Ticks swingTicks = static_cast<Ticks>(80.0 * sw + 0.5);  // engine swing 1.0 = triplet = 80 ticks on a 16th pair
    for (Hit& h : hits_) {
      const bool exact = h.gesture != Gesture::None && h.gesture != Gesture::Crash;
      if (exact) {
        if (h.endAt < 0) h.start = std::min<Ticks>(std::max<Ticks>(h.start, 0), kBarTicks - 1);
        continue;
      }
      Rng tr = cellRng(s_, kTagTiming, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(h.step * 16 + roleIndex(h.role)));
      const double g = gauss(tr);
      double mean = 0.0, sigma = 2.0;
      switch (h.role) {
        case DrumRole::Kick:
          if (h.anchor) { sigma = 1.0; }
          else if (h.step == 8) { mean = pt_.kick2LateMs * gp_; sigma = 2.5; }
          else { sigma = 2.5; }
          break;
        case DrumRole::Snare:
          if (h.anchor) { mean = pt_.snareLateMs * gp_; sigma = 1.5; }
          else { mean = pt_.ghostLateMs * gp_; sigma = 3.0; }
          break;
        case DrumRole::Ghost: mean = pt_.ghostLateMs * gp_; sigma = 3.0; break;
        case DrumRole::Hat:
        case DrumRole::OpenHat: sigma = pt_.hatSigmaMs; break;
        case DrumRole::Cymbal: sigma = 2.0; break;
        default: sigma = 3.0; break;
      }
      const double cap = maxTimingMs(s_, h.role, h.anchor);
      const double ms = clampd(mean + sigma * hp_ * g, -cap, cap);
      Ticks t = static_cast<Ticks>(h.step) * kStepTicks + msToTicks(ms, bpm_);
      if ((h.step % 2) == 1 && !h.anchor) t += swingTicks;
      h.start = std::min<Ticks>(std::max<Ticks>(t, 0), kBarTicks - 1);
    }
  }
};

}  // namespace

BarPlan planBar(const PlanSettings& s, int bar) { return Planner(s, bar).run(); }

std::vector<BarPlan> planPattern(const PlanSettings& s) {
  std::vector<BarPlan> out;
  for (int b = 0; b < s.bars; ++b) out.push_back(planBar(s, b));
  return out;
}

}  // namespace chopfractal::breakeng
