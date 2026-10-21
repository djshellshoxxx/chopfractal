// Engine v2: break-aware generation and mutation. The rhythm comes from break_engine (role plans); this file maps
// plans onto chops of the right role, builds events (level from velocity, ticks from the plan), assigns gate
// lengths per instrument family, and implements an anchor-preserving Mutate.
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

#include "internal.hpp"

namespace chopfractal::pattern {
namespace {

using breakeng::BarPlan;
using breakeng::Gesture;
using breakeng::Hit;
using breakeng::PlanSettings;

constexpr int kRoleSlots = 9;  // DrumRole values 0..8
enum : std::uint64_t { kStreamPick = 21, kStreamMutateCell = 22, kStreamMutatePick = 23 };

int ri(DrumRole r) { return static_cast<int>(r); }

// ---------------------------------------------------------------------------------------------
// role pools
// ---------------------------------------------------------------------------------------------
struct Pools {
  std::vector<ChopId> by[kRoleSlots];       // pool per playable role; Ghost falls back to the snare pool
  std::unordered_map<std::uint64_t, DrumRole> effective;  // chop -> the role it plays in v2
  const std::vector<ChopId>& of(DrumRole r) const { return by[ri(r)]; }
};

Pools buildPools(const ChopSnapshot& chops) {
  Pools p;
  std::vector<ChopId> unlabelled, all;
  bool anyLabelled = false;
  for (const ChopInfo& c : chops.chops) {
    all.push_back(c.id);
    switch (c.role) {
      case DrumRole::Kick: case DrumRole::Snare: case DrumRole::Ghost: case DrumRole::Hat:
      case DrumRole::OpenHat: case DrumRole::Cymbal: case DrumRole::Perc:
        p.by[ri(c.role)].push_back(c.id);
        p.effective[c.id.value] = c.role;
        if (c.role == DrumRole::Kick || c.role == DrumRole::Snare || c.role == DrumRole::Hat) anyLabelled = true;
        break;
      default: unlabelled.push_back(c.id); break;
    }
  }
  const std::size_t n = all.size();
  if (!anyLabelled && p.by[ri(DrumRole::Ghost)].empty() && p.by[ri(DrumRole::Cymbal)].empty() && p.by[ri(DrumRole::Perc)].empty() && p.by[ri(DrumRole::OpenHat)].empty()) {
    // Nobody labelled anything: source order stands in for roles (the first slice of a loop is usually the kick,
    // the second the snare), the rest are hats.
    p.by[ri(DrumRole::Kick)].push_back(all[0]);
    p.by[ri(DrumRole::Snare)].push_back(all[std::min<std::size_t>(1, n - 1)]);
    for (std::size_t i = 2; i < n; ++i) p.by[ri(DrumRole::Hat)].push_back(all[i]);
    for (const auto& roleIdx : {DrumRole::Kick, DrumRole::Snare, DrumRole::Hat})
      for (ChopId id : p.by[ri(roleIdx)]) p.effective[id.value] = roleIdx;
  } else {
    std::size_t next = 0;
    auto fill = [&](DrumRole r) {
      if (!p.by[ri(r)].empty()) return;
      ChopId id = next < unlabelled.size() ? unlabelled[next++] : all[(r == DrumRole::Kick ? 0 : n - 1) % n];
      p.by[ri(r)].push_back(id);
      if (!p.effective.count(id.value)) p.effective[id.value] = r;
    };
    fill(DrumRole::Kick);
    fill(DrumRole::Snare);
    for (ChopId id : unlabelled)
      if (!p.effective.count(id.value)) p.effective[id.value] = DrumRole::Perc;  // unlabelled leftovers only classify; they never play by themselves
  }
  // A ghost note is a quiet snare: without a dedicated ghost chop the snare pool plays it.
  if (p.by[ri(DrumRole::Ghost)].empty()) p.by[ri(DrumRole::Ghost)] = p.by[ri(DrumRole::Snare)];
  return p;
}

DrumRole effectiveRole(const Pools& pools, ChopId chop) {
  auto it = pools.effective.find(chop.value);
  return it == pools.effective.end() ? DrumRole::Unknown : it->second;
}

int familyOf(DrumRole r) {
  switch (r) {
    case DrumRole::Kick: return 0;
    case DrumRole::Snare: case DrumRole::Ghost: return 1;
    case DrumRole::Hat: case DrumRole::OpenHat: return 2;
    case DrumRole::Cymbal: return 3;
    case DrumRole::Perc: return 4;
    default: return 5;
  }
}

breakeng::Avail availFrom(const Pools& pools) {
  breakeng::Avail a;
  a.hat = !pools.of(DrumRole::Hat).empty();
  a.openHat = a.hat && !pools.of(DrumRole::OpenHat).empty();
  a.ghost = !pools.of(DrumRole::Ghost).empty();
  a.cymbal = !pools.of(DrumRole::Cymbal).empty();
  a.perc = !pools.of(DrumRole::Perc).empty();
  return a;
}

// ---------------------------------------------------------------------------------------------
// chop choice
// ---------------------------------------------------------------------------------------------
class Chooser {
 public:
  Chooser(const Pools& pools, const Settings& s, const ICandidatePolicy* policy, std::uint64_t seed, GenerationReport* report)
      : pools_(pools), s_(s), policy_(policy), seed_(seed), report_(report) {
    for (int i = 0; i < kRoleSlots; ++i) last_[i] = -1;
  }

  void startBar(int bar) {
    bar_ = bar;
    barSoFar_.clear();
    lastOfRole_.clear();
  }

  void seedHistory(ChopId chop, DrumRole role) {
    recent_.push_back(chop);
    lastOfRole_[ri(role)] = chop;
    const auto& pool = pools_.of(role);
    for (std::size_t i = 0; i < pool.size(); ++i)
      if (pool[i] == chop) last_[ri(role)] = static_cast<int>(i);
  }

  ChopId pick(const Hit& h, int ordinal) {
    const bool repeatsSource = h.gesture == Gesture::Stutter8 || h.gesture == Gesture::Stutter16 || h.gesture == Gesture::Stutter32;
    if (repeatsSource) {
      auto it = lastOfRole_.find(ri(h.role));
      if (it != lastOfRole_.end()) return remember(it->second, h.role);
    }
    const std::vector<ChopId>& pool = pools_.of(h.role);
    if (pool.empty()) return ChopId{};
    std::vector<std::size_t> allowed;
    std::vector<double> weights;
    std::uint32_t blocked = 0;
    for (std::size_t i = 0; i < pool.size(); ++i) {
      if (!policy_) {
        allowed.push_back(i);
        weights.push_back(1.0);
        continue;
      }
      const Decision d = policy_->evaluate(query(h.start, pool[i]));
      if (d.allowed) {
        allowed.push_back(i);
        weights.push_back(d.weight);
      } else if (blocked == 0) {
        blocked = d.blockedBy;
      }
    }
    if (allowed.empty()) {
      if (report_) report_->notes.push_back({blocked ? Note::Kind::BlockedByRule : Note::Kind::NoCandidate, blocked, bar_, static_cast<int>(h.start / (kTicksPerQuarter))});
      return ChopId{};
    }
    Rng r(deriveKey(seed_, kStreamPick, static_cast<std::uint64_t>(bar_), static_cast<std::uint64_t>(ordinal)));
    const double dAlt = r.uniform01(), dPick = r.uniform01();
    bool weighted = false;
    for (double w : weights)
      if (w != 1.0) weighted = true;
    const int last = last_[ri(h.role)];
    std::size_t chosen = allowed[0];
    if (allowed.size() > 1) {
      std::vector<std::size_t> cand;
      std::vector<double> cw;
      for (std::size_t k = 0; k < allowed.size(); ++k)
        if (static_cast<int>(allowed[k]) != last) {
          cand.push_back(allowed[k]);
          cw.push_back(weights[k]);
        }
      if (cand.empty()) {
        cand = allowed;
        cw = weights;
      }
      if (dAlt < 0.5 * s_.variation.event || weighted) {
        chosen = cand[pickWeighted(cw, dPick)];
      } else {
        // Round-robin: the first candidate after the previous one, so the same chop never plays twice in a row.
        chosen = cand[0];
        for (std::size_t c : cand)
          if (static_cast<int>(c) > last) {
            chosen = c;
            break;
          }
      }
    }
    last_[ri(h.role)] = static_cast<int>(chosen);
    return remember(pool[chosen], h.role);
  }

 private:
  static std::size_t pickWeighted(const std::vector<double>& w, double u) {
    double sum = 0.0;
    for (double x : w) sum += x > 0.0 ? x : 0.0;
    if (!(sum > 0.0)) return 0;
    const double target = u * sum;
    double acc = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) {
      acc += w[i] > 0.0 ? w[i] : 0.0;
      if (target < acc) return i;
    }
    return w.size() - 1;
  }

  ChopId remember(ChopId chop, DrumRole role) {
    recent_.push_back(chop);
    if (recent_.size() > 64) recent_.erase(recent_.begin(), recent_.begin() + 32);
    barSoFar_.push_back(chop);
    lastOfRole_[ri(role)] = chop;
    return chop;
  }

  CandidateQuery query(Ticks startInBar, ChopId chop) const {
    const Ticks beatT = detail::beatTicksOf(s_);
    const int beats = s_.timeSignature.numerator;
    CandidateQuery q;
    q.chop = chop;
    q.startInBar = startInBar;
    q.scope.level = ScopeLevel::Event;
    q.scope.bar = bar_;
    q.scope.barCount = s_.bars;
    q.scope.beat = static_cast<int>(std::min<Ticks>(beats - 1, std::max<Ticks>(0, startInBar) / beatT));
    q.scope.beatsPerBar = beats;
    q.barSoFar = barSoFar_;
    q.firstInBar = barSoFar_.empty();
    const std::size_t from = recent_.size() > 16 ? recent_.size() - 16 : 0;
    q.recent.assign(recent_.begin() + static_cast<std::ptrdiff_t>(from), recent_.end());
    return q;
  }

  const Pools& pools_;
  const Settings& s_;
  const ICandidatePolicy* policy_;
  std::uint64_t seed_;
  GenerationReport* report_;
  int bar_ = 0;
  int last_[kRoleSlots];
  std::vector<ChopId> recent_, barSoFar_;
  std::map<int, ChopId> lastOfRole_;
};

// ---------------------------------------------------------------------------------------------
// plan -> events
// ---------------------------------------------------------------------------------------------
double bpmOf(const Settings& s) { return s.brk.tempoHint > 0.0 ? s.brk.tempoHint : 120.0; }

Ticks chopTicks(const ChopInfo& c, std::int32_t sampleRate, double bpm) {
  if (sampleRate <= 0) return 0;
  const double seconds = static_cast<double>(c.range.length()) / static_cast<double>(sampleRate);
  return static_cast<Ticks>(seconds * bpm / 60.0 * static_cast<double>(kTicksPerQuarter));
}

Event eventFromHit(const Hit& h, ChopId chop, const ChopSnapshot& chops, const Settings& s) {
  Event e;
  e.chop = chop;
  const ChopInfo* ci = chops.find(chop);
  e.start = h.start;
  e.duration = h.duration;
  e.tx = h.tx;
  const double trim = ci && ci->gainTrim > 0.f ? static_cast<double>(ci->gainTrim) : 1.0;
  const double level = breakeng::velocityToLevel(h.velocity) * trim;
  e.tx.level = static_cast<float>(std::min<double>(limits::kMaxLevel, std::max(0.0, level)));
  e.fx = h.fx;
  if (h.endAt >= 0 && ci) {
    // A reversed hit must END on the grid line: its window is the (tempo-hinted) chop length, at most maxWindow.
    const Ticks len = chopTicks(*ci, chops.sampleRate, bpmOf(s));
    const Ticks window = std::max<Ticks>(breakeng::kStepTicks / 2, std::min(h.maxWindow > 0 ? h.maxWindow : len, len));
    e.start = h.endAt - window;
    e.duration = window;
  }
  return e;
}

// Gate length per instrument family: until the next hit of the same family (so a kick is not cut by a hat, and an
// open hat is closed by the next hat), capped to two beats, and never past the bar.
void assignFamilyDurations(std::vector<Event*>& evs, const Pools& pools, Ticks barT, const std::unordered_set<std::uint64_t>* only) {
  std::stable_sort(evs.begin(), evs.end(), [](const Event* a, const Event* b) { return a->start != b->start ? a->start < b->start : a->id < b->id; });
  for (std::size_t i = 0; i < evs.size(); ++i) {
    Event& e = *evs[i];
    if (only && only->count(e.id.value) == 0) continue;
    if (e.tx.reverse || e.fx.glideSemitones <= -12.f) continue;  // reversed swells and tape stops carry their own window
    const int fam = familyOf(effectiveRole(pools, e.chop));
    Ticks next = barT;
    for (std::size_t j = i + 1; j < evs.size(); ++j)
      if (evs[j]->start > e.start && familyOf(effectiveRole(pools, evs[j]->chop)) == fam) {
        next = evs[j]->start;
        break;
      }
    const Ticks cap = 2 * kTicksPerQuarter;
    e.duration = std::max<Ticks>(1, std::min<Ticks>({next - e.start, barT - e.start, cap}));
  }
}

int priorityOf(const Hit& h) {
  if (h.anchor) return 100;
  if (h.gesture == Gesture::Crash) return 60;
  if (h.gesture != Gesture::None) return 70;
  switch (h.role) {
    case DrumRole::Kick: return 50;
    case DrumRole::Snare: return 45;
    case DrumRole::Perc: return 40;
    case DrumRole::Cymbal: return 38;
    case DrumRole::Ghost: return 30;
    case DrumRole::OpenHat: return 20;
    default: return 10;
  }
}

void maskGrid(std::vector<BarPlan>& plans, const Settings& s) {
  if (s.grid.division >= 16) return;
  for (BarPlan& bp : plans)
    bp.hits.erase(std::remove_if(bp.hits.begin(), bp.hits.end(), [](const Hit& h) { return h.gesture == Gesture::None && (h.step % 2) == 1; }), bp.hits.end());
}

// Removes the lowest-priority hits until at most `cap` remain; later bars and later positions go first.
bool trimToCap(std::vector<BarPlan>& plans, std::size_t cap) {
  std::size_t total = 0;
  for (const BarPlan& bp : plans) total += bp.hits.size();
  if (total <= cap) return false;
  struct Ref {
    int prio;
    int bar;
    Ticks start;
    std::size_t idx;
  };
  std::vector<Ref> refs;
  for (std::size_t b = 0; b < plans.size(); ++b)
    for (std::size_t i = 0; i < plans[b].hits.size(); ++i) refs.push_back({priorityOf(plans[b].hits[i]), static_cast<int>(b), plans[b].hits[i].start, i});
  std::sort(refs.begin(), refs.end(), [](const Ref& a, const Ref& b) {
    if (a.prio != b.prio) return a.prio < b.prio;
    if (a.bar != b.bar) return a.bar > b.bar;
    return a.start != b.start ? a.start > b.start : a.idx > b.idx;
  });
  std::set<std::pair<int, std::size_t>> drop;
  for (std::size_t k = 0; k < total - cap; ++k) drop.insert({refs[k].bar, refs[k].idx});
  for (std::size_t b = 0; b < plans.size(); ++b) {
    std::vector<Hit> keep;
    for (std::size_t i = 0; i < plans[b].hits.size(); ++i)
      if (!drop.count({static_cast<int>(b), i})) keep.push_back(plans[b].hits[i]);
    plans[b].hits = std::move(keep);
  }
  return true;
}

int beatIndexOf(Ticks start, Ticks beatT, int beats) { return static_cast<int>(std::min<Ticks>(beats - 1, std::max<Ticks>(0, start) / beatT)); }

}  // namespace

// ---------------------------------------------------------------------------------------------
// public helpers
// ---------------------------------------------------------------------------------------------
bool supportsBreakEngine(const Settings& s) {
  return s.timeSignature.numerator == 4 && s.timeSignature.denominator == 4 && !s.grid.triplet && s.grid.division >= 8;
}

breakeng::Avail availableRoles(const ChopSnapshot& chops) {
  if (chops.chops.empty()) return breakeng::Avail{false, false, false, false, false};
  return availFrom(buildPools(chops));
}

breakeng::PlanSettings planSettingsOf(const Settings& s, const ChopSnapshot& chops) {
  PlanSettings ps;
  ps.options = s.brk;
  ps.density = s.density;
  ps.swing = s.swing;
  ps.variationPhrase = s.variation.phrase;
  ps.variationEvent = s.variation.event;
  ps.allow = {s.allowReverse, s.allowPitch, s.allowRetrigger, s.allowGlide, s.allowFilter, s.allowCrunch};
  ps.pitchRange = s.pitchRange;
  ps.fxIntensity = s.fxIntensity;
  ps.avail = availableRoles(chops);
  ps.bars = s.bars;
  ps.seed = s.seed;
  return ps;
}

namespace {
EventRole classify(const Event& e, const Pools& pools, bool snareAtStep12Missing, double trim) {
  EventRole r;
  r.role = effectiveRole(pools, e.chop);
  r.step = static_cast<int>(std::min<Ticks>(15, std::max<Ticks>(0, (e.start + breakeng::kStepTicks / 2) / breakeng::kStepTicks)));
  const int vel = breakeng::levelToVelocity(static_cast<double>(e.tx.level) / (trim > 0.0 ? trim : 1.0));
  if (r.role == DrumRole::Snare && vel < 64 && r.step != 4 && r.step != 12) r.role = DrumRole::Ghost;  // a quiet snare is a ghost
  r.anchor = breakeng::isAnchorCell(r.role, r.step, snareAtStep12Missing);
  return r;
}
}  // namespace

std::optional<EventRole> describeEvent(const Pattern& p, EventId id, const ChopSnapshot& chops) {
  if (p.engineVersion < kEngineVersionBreak || chops.chops.empty()) return std::nullopt;
  auto loc = locate(p, id);
  if (!loc) return std::nullopt;
  const Pools pools = buildPools(chops);
  const Bar& bar = p.bars[static_cast<std::size_t>(loc->bar)];
  bool snare12 = false;
  for (const Beat& bt : bar.beats)
    for (const Event& e : bt.events) {
      const EventRole r = classify(e, pools, false, 1.0);
      if (r.role == DrumRole::Snare && r.step == 12) snare12 = true;
    }
  const Event* ev = findEvent(p, id);
  if (!ev) return std::nullopt;
  const ChopInfo* ci = chops.find(ev->chop);
  return classify(*ev, pools, !snare12, ci ? static_cast<double>(ci->gainTrim) : 1.0);
}

// ---------------------------------------------------------------------------------------------
// generation
// ---------------------------------------------------------------------------------------------
namespace detail {

Result<Pattern> generateBreak(const ChopSnapshot& chops, const Settings& settings, const ICandidatePolicy* policy, GenerationReport* report) {
  const Pools pools = buildPools(chops);
  PlanSettings ps = planSettingsOf(settings, chops);
  std::vector<BarPlan> plans = breakeng::planPattern(ps);
  maskGrid(plans, settings);
  if (trimToCap(plans, static_cast<std::size_t>(settings.maxEvents)) && report) report->notes.push_back({Note::Kind::LimitReached, 0, -1, -1});

  Pattern p;
  p.engineVersion = kEngineVersionBreak;
  p.settings = settings;
  p.phraseId = ScopeId{p.nextId++};
  const Ticks barT = barTicks(settings);
  const Ticks beatT = beatTicksOf(settings);
  const int beats = settings.timeSignature.numerator;
  Chooser chooser(pools, settings, policy, settings.seed, report);
  for (int b = 0; b < settings.bars; ++b) {
    Bar bar;
    bar.id = ScopeId{p.nextId++};
    bar.beats.resize(static_cast<std::size_t>(beats));
    for (Beat& bt : bar.beats) bt.id = ScopeId{p.nextId++};
    chooser.startBar(b);
    std::vector<Event> made;
    int ordinal = 0;
    for (const Hit& h : plans[static_cast<std::size_t>(b)].hits) {
      const ChopId chop = chooser.pick(h, ordinal++);
      if (!chop.valid()) continue;
      Event e = eventFromHit(h, chop, chops, settings);
      e.start = std::max<Ticks>(0, std::min<Ticks>(e.start, barT - 1));
      made.push_back(std::move(e));
    }
    std::stable_sort(made.begin(), made.end(), [](const Event& a, const Event& c) { return a.start < c.start; });
    for (Event& e : made) e.id = EventId{p.nextId++};
    std::vector<Event*> ptrs;
    for (Event& e : made) ptrs.push_back(&e);
    assignFamilyDurations(ptrs, pools, barT, nullptr);
    for (Event& e : made) {
      e.duration = std::max<Ticks>(1, std::min<Ticks>(e.duration, barT - e.start));
      bar.beats[static_cast<std::size_t>(beatIndexOf(e.start, beatT, beats))].events.push_back(std::move(e));
    }
    p.bars.push_back(std::move(bar));
  }
  return p;
}

// ---------------------------------------------------------------------------------------------
// mutation: anchors stay; ghosts, hats, extra kicks and fills change, more towards the end of the phrase
// ---------------------------------------------------------------------------------------------
namespace {

int classOf(DrumRole r) {
  switch (r) {
    case DrumRole::Kick: return 0;
    case DrumRole::Snare: return 1;
    case DrumRole::Ghost: return 2;
    case DrumRole::Hat: case DrumRole::OpenHat: return 3;
    case DrumRole::Cymbal: return 4;
    default: return 5;
  }
}

}  // namespace

Result<Pattern> mutateBreak(const Pattern& in, const ChopSnapshot& chops, const MutateOptions& opt, const ICandidatePolicy* policy, GenerationReport* report) {
  const Pools pools = buildPools(chops);
  Pattern work = in;
  work.settings.seed = opt.seed;
  const Settings& st = work.settings;
  PlanSettings ps = planSettingsOf(st, chops);
  std::vector<BarPlan> plans = breakeng::planPattern(ps);
  maskGrid(plans, st);
  const Ticks barT = barTicks(st);
  const Ticks beatT = beatTicksOf(st);
  const int beats = st.timeSignature.numerator;
  const breakeng::ProfileInfo& pi = breakeng::profileInfo(st.brk.profile);
  const bool anchorsMutable = st.brk.anchors == breakeng::AnchorMode::Free || (st.brk.anchors == breakeng::AnchorMode::Profile && pi.softAnchors);
  Chooser chooser(pools, st, policy, opt.seed, report);

  auto note = [&](Note::Kind k, int bar, int beat) {
    if (report) report->notes.push_back({k, 0, bar, beat});
  };
  auto query = [&](int bar, Ticks start, ChopId chop) {
    CandidateQuery q;
    q.chop = chop;
    q.startInBar = start;
    q.scope.level = ScopeLevel::Event;
    q.scope.bar = bar;
    q.scope.barCount = st.bars;
    q.scope.beat = beatIndexOf(start, beatT, beats);
    q.scope.beatsPerBar = beats;
    return q;
  };

  bool changed = false;
  for (int bar = 0; bar < st.bars; ++bar) {
    Bar& B = work.bars[static_cast<std::size_t>(bar)];
    if (B.locked || (B.userOwned && !opt.includeEdited)) {
      note(Note::Kind::LockedSkipped, bar, -1);
      continue;
    }
    const int slot = breakeng::phraseSlot(bar, st.bars);
    const double barWeight = st.bars == 1 ? 1.0 : 0.7 + 0.2 * slot;  // changes grow towards the end of the phrase

    // Existing events with their roles. Retained = locked, hand-edited, in a locked beat, or kept by a rule.
    struct Old {
      std::size_t beat;
      std::size_t index;
      EventRole info;
      bool retained;
      bool replaced;
    };
    std::vector<Old> olds;
    bool snare12 = false;
    for (std::size_t bi = 0; bi < B.beats.size(); ++bi)
      for (std::size_t i = 0; i < B.beats[bi].events.size(); ++i) {
        const Event& e = B.beats[bi].events[i];
        const ChopInfo* ci = chops.find(e.chop);
        EventRole r = classify(e, pools, false, ci ? static_cast<double>(ci->gainTrim) : 1.0);
        if (r.role == DrumRole::Snare && r.step == 12) snare12 = true;
        olds.push_back({bi, i, r, false, false});
      }
    for (Old& o : olds) {
      const Beat& bt = B.beats[o.beat];
      const Event& e = bt.events[o.index];
      o.info.anchor = breakeng::isAnchorCell(o.info.role, o.info.step, !snare12);
      o.retained = e.locked || (e.child && e.child->locked) || (e.userOwned && !opt.includeEdited) || bt.locked || (bt.userOwned && !opt.includeEdited) ||
                   (policy && policy->isPreserved(query(bar, e.start, e.chop)));
    }

    auto cellReplaced = [&](int cls, int step, bool anchorCell) {
      const bool fillRegion = slot == 3 && step >= 12 && !anchorCell;
      double w;
      std::uint64_t key = static_cast<std::uint64_t>(cls);
      if (anchorCell) {
        w = anchorsMutable ? 0.1 : 0.0;
      } else if (fillRegion) {
        w = 1.5;
        key = 7;  // one decision for the whole fill block, so a roll is replaced or kept as a unit
      } else {
        w = cls == 2 ? 1.0 : (cls == 3 ? 0.8 : (cls == 0 ? 0.5 : 0.7));
      }
      const double m = std::min(1.0, opt.amount * w * barWeight);
      Rng r(deriveKey(opt.seed, kStreamMutateCell, static_cast<std::uint64_t>(bar), key * 32 + static_cast<std::uint64_t>(fillRegion ? 12 : step) + (anchorCell ? 16 : 0)));
      return r.uniform01() < m;
    };
    for (Old& o : olds) o.replaced = !o.retained && cellReplaced(classOf(o.info.role), o.info.step, o.info.anchor);

    // Present cells after removing the replaced events.
    std::vector<breakeng::Cell> present;
    std::set<std::pair<std::size_t, std::size_t>> removed;
    for (const Old& o : olds) {
      if (o.replaced) {
        removed.insert({o.beat, o.index});
        continue;
      }
      present.push_back({o.info.role, o.info.step, false});
    }
    // New content from the fresh plan for replaced cells.
    chooser.startBar(bar);
    for (const Old& o : olds)
      if (!o.replaced) chooser.seedHistory(B.beats[o.beat].events[o.index].chop, o.info.role);
    std::vector<Event> added;
    int ordinal = 0;
    const breakeng::PlanSettings& psr = ps;
    for (const Hit& h : plans[static_cast<std::size_t>(bar)].hits) {
      const int hs = h.step;
      if (!cellReplaced(classOf(h.role), hs, h.anchor)) continue;
      const Beat& target = B.beats[static_cast<std::size_t>(beatIndexOf(h.start, beatT, beats))];
      if (target.locked || (target.userOwned && !opt.includeEdited)) continue;
      const breakeng::Cell cell{h.role, hs, h.gesture != Gesture::None};
      if (!breakeng::canAdd(psr, present, cell, h.anchor)) continue;
      const ChopId chop = chooser.pick(h, ordinal++);
      if (!chop.valid()) continue;
      Event e = eventFromHit(h, chop, chops, st);
      e.start = std::max<Ticks>(0, std::min<Ticks>(e.start, barT - 1));
      added.push_back(std::move(e));
      present.push_back(cell);
    }
    if (removed.empty() && added.empty()) continue;

    // Event budget: new events may not push the pattern past its cap (lowest-value additions go first).
    const std::size_t total = eventCount(work);
    const std::size_t afterRemoval = total - removed.size();
    const std::size_t cap = static_cast<std::size_t>(st.maxEvents);
    if (afterRemoval + added.size() > cap) {
      note(Note::Kind::LimitReached, bar, -1);
      const std::size_t room = cap > afterRemoval ? cap - afterRemoval : 0;
      std::stable_sort(added.begin(), added.end(), [&](const Event& a, const Event& b) { return a.tx.level > b.tx.level; });
      added.resize(std::min(added.size(), room));
    }

    // Rebuild the bar's beats: kept events keep their order and ids; additions get fresh ids in time order.
    std::vector<Beat> newBeats = B.beats;
    for (Beat& bt : newBeats) bt.events.clear();
    for (std::size_t bi = 0; bi < B.beats.size(); ++bi)
      for (std::size_t i = 0; i < B.beats[bi].events.size(); ++i)
        if (!removed.count({bi, i})) newBeats[bi].events.push_back(B.beats[bi].events[i]);
    std::stable_sort(added.begin(), added.end(), [](const Event& a, const Event& b) { return a.start < b.start; });
    std::unordered_set<std::uint64_t> fresh;
    for (Event& e : added) {
      e.id = EventId{work.nextId++};
      fresh.insert(e.id.value);
      newBeats[static_cast<std::size_t>(beatIndexOf(e.start, beatT, beats))].events.push_back(e);
    }
    B.beats = std::move(newBeats);

    // Gate lengths follow the new neighbours, but only for events the engine owns.
    std::vector<Event*> ptrs;
    std::unordered_set<std::uint64_t> engineOwned;
    for (Beat& bt : B.beats)
      for (Event& e : bt.events) {
        ptrs.push_back(&e);
        if (!e.userOwned && !e.locked && !bt.locked) engineOwned.insert(e.id.value);
      }
    assignFamilyDurations(ptrs, pools, barT, &engineOwned);
    for (Beat& bt : B.beats)
      for (Event& e : bt.events) e.duration = std::max<Ticks>(1, std::min<Ticks>(e.duration, barT - e.start));
    changed = true;
  }
  (void)changed;
  return work;
}

}  // namespace detail
}  // namespace chopfractal::pattern
