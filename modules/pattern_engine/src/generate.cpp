#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

#include "internal.hpp"

namespace chopfractal::pattern {

Ticks barTicks(const Settings& s) { return ticksPerBar(s.timeSignature); }
Ticks lengthTicks(const Pattern& p) { return barTicks(p.settings) * p.settings.bars; }

std::size_t eventCount(const Pattern& p) {
  std::size_t n = 0;
  for (const Bar& b : p.bars)
    for (const Beat& bt : b.beats) n += bt.events.size();
  return n;
}

Status validateSettings(const Settings& s) {
  auto in01 = [](double v) { return v >= 0.0 && v <= 1.0; };
  if (!(s.bars == 1 || s.bars == 2 || s.bars == 4 || s.bars == 8)) return makeError(ErrorCode::InvalidArgument, "pattern length must be 1, 2, 4 or 8 bars");
  if (!s.timeSignature.valid()) return makeError(ErrorCode::InvalidArgument, "invalid time signature");
  if (!s.grid.valid()) return makeError(ErrorCode::InvalidArgument, "invalid grid");
  const Ticks g = gridTicks(s.grid);
  if (barTicks(s) / g < 1) return makeError(ErrorCode::InvalidArgument, "grid is longer than a bar");
  if (!in01(s.density) || !in01(s.variation.phrase) || !in01(s.variation.bar) || !in01(s.variation.beat) ||
      !in01(s.variation.event) || !in01(s.swing))
    return makeError(ErrorCode::OutOfRange, "density, variation and swing must lie in [0, 1]");
  if (s.maxEvents < s.bars || s.maxEvents > static_cast<int>(limits::kMaxEventsPerPattern))
    return makeError(ErrorCode::OutOfRange, "event cap out of range", static_cast<std::int64_t>(limits::kMaxEventsPerPattern));
  if (s.pitchRange < 0 || s.pitchRange > static_cast<int>(limits::kMaxPitchSemitones)) return makeError(ErrorCode::OutOfRange, "pitch range out of range");
  if (s.maxRetrigger < 2 || s.maxRetrigger > limits::kMaxRetrigger) return makeError(ErrorCode::OutOfRange, "retrigger bound out of range");
  if (s.maxShiftTicks < 0 || s.maxShiftTicks > g / 8) return makeError(ErrorCode::OutOfRange, "micro-shift exceeds one eighth of a grid cell", g / 8);
  return {};
}

namespace {

enum : std::uint64_t { kStreamBar = 2, kStreamBeat = 3, kStreamEvent = 4, kStreamMutate = 5 };

std::size_t pickByU(const std::vector<double>& w, double u) {
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

struct Made {
  Event primary;
  std::optional<Event> sub;
};

class Gen {
 public:
  Gen(const ChopSnapshot& chops, const Settings& s, const ICandidatePolicy* policy, GenerationReport* report, std::uint64_t seed)
      : chops_(chops), s_(s), policy_(policy), report_(report), seed_(seed) {
    barT_ = barTicks(s);
    beatT_ = detail::beatTicksOf(s);
    g_ = gridTicks(s.grid);
    slots_ = static_cast<int>(barT_ / g_);
    beats_ = s.timeSignature.numerator;
    double sum = 0.0;
    for (int i = 0; i < slots_; ++i) sum += detail::metricWeight(i * g_, beatT_);
    avgW_ = sum / slots_;
  }

  int beatBudget(int beat) const {
    const int perBar = s_.maxEvents / s_.bars;
    return perBar / beats_ + (beat < perBar % beats_ ? 1 : 0);
  }

  void note(Note::Kind k, std::uint32_t rule, int bar, int beat) const {
    if (report_) report_->notes.push_back({k, rule, bar, beat});
  }

  CandidateQuery query(const Pattern& p, int bar, Ticks startInBar, ChopId chop) const {
    struct E {
      int bar;
      Ticks start;
      std::uint64_t id;
      ChopId chop;
    };
    std::vector<E> before;
    for (int b = 0; b <= bar && b < static_cast<int>(p.bars.size()); ++b)
      for (const Beat& bt : p.bars[static_cast<std::size_t>(b)].beats)
        for (const Event& e : bt.events)
          if (b < bar || e.start < startInBar) before.push_back({b, e.start, e.id.value, e.chop});
    std::sort(before.begin(), before.end(), [](const E& a, const E& b) {
      if (a.bar != b.bar) return a.bar < b.bar;
      return a.start != b.start ? a.start < b.start : a.id < b.id;
    });
    CandidateQuery q;
    q.chop = chop;
    q.startInBar = startInBar;
    q.scope.level = ScopeLevel::Event;
    q.scope.bar = bar;
    q.scope.barCount = s_.bars;
    q.scope.beat = static_cast<int>(std::min<Ticks>(beats_ - 1, startInBar / beatT_));
    q.scope.beatsPerBar = beats_;
    for (const E& e : before) {
      if (e.bar == bar) q.barSoFar.push_back(e.chop);
    }
    const std::size_t from = before.size() > 16 ? before.size() - 16 : 0;
    for (std::size_t i = from; i < before.size(); ++i) q.recent.push_back(before[i].chop);
    q.firstInBar = q.barSoFar.empty();
    return q;
  }

  // Chooses the chop for a slot, honouring the optional policy. Returns an invalid id when blocked.
  ChopId selectChop(const Pattern& p, int bar, int beat, int slotIdx, Ticks t, double dAlt, double dPick) const {
    const std::size_t n = chops_.chops.size();
    const std::size_t baseIdx = std::min(n - 1, static_cast<std::size_t>(slotIdx) * n / static_cast<std::size_t>(slots_));
    const ChopInfo& base = chops_.chops[baseIdx];
    const bool alt = dAlt < s_.variation.event;
    std::vector<ChopId> cands;
    if (alt) {
      if (base.group != 0)
        for (const ChopInfo& c : chops_.chops)
          if (c.group == base.group) cands.push_back(c.id);
      if (cands.size() < 2) {
        cands.clear();
        for (const ChopInfo& c : chops_.chops) cands.push_back(c.id);
      }
    } else {
      cands.push_back(base.id);
    }
    std::uint32_t blocked = 0;
    auto filter = [&](const std::vector<ChopId>& list, std::vector<ChopId>& allowed, std::vector<double>& weights) {
      for (ChopId c : list) {
        if (!policy_) {
          allowed.push_back(c);
          weights.push_back(1.0);
          continue;
        }
        const Decision d = policy_->evaluate(query(p, bar, t, c));
        if (d.allowed) {
          allowed.push_back(c);
          weights.push_back(d.weight);
        } else if (blocked == 0) {
          blocked = d.blockedBy;
        }
      }
    };
    std::vector<ChopId> allowed;
    std::vector<double> weights;
    filter(cands, allowed, weights);
    if (allowed.empty() && !alt) {
      std::vector<ChopId> all;
      for (const ChopInfo& c : chops_.chops) all.push_back(c.id);
      filter(all, allowed, weights);
    }
    if (allowed.empty()) {
      note(blocked ? Note::Kind::BlockedByRule : Note::Kind::NoCandidate, blocked, bar, beat);
      return ChopId{};
    }
    return allowed[pickByU(weights, dPick)];
  }

  std::optional<Made> makeEvent(const Pattern& p, int bar, int beat, int slotIdx) const {
    Rng er(deriveKey(seed_, kStreamEvent, static_cast<std::uint64_t>(bar), static_cast<std::uint64_t>(slotIdx)));
    const double dAlt = er.uniform01();
    const double dPick = er.uniform01();
    const double dRev = er.uniform01();
    const double dPitch = er.uniform01();
    const std::uint64_t vPitch = er.uniform(static_cast<std::uint64_t>(2 * s_.pitchRange + 1));
    const double dRet = er.uniform01();
    const std::uint64_t vRet = er.uniform(static_cast<std::uint64_t>(s_.maxRetrigger - 1));
    const double dShift = er.uniform01();
    const std::uint64_t vShift = er.uniform(static_cast<std::uint64_t>(2 * s_.maxShiftTicks + 1));
    const double dSub = er.uniform01();

    Ticks t = static_cast<Ticks>(slotIdx) * g_;
    const Ticks gridStart = t;
    const ChopId chop = selectChop(p, bar, beat, slotIdx, t, dAlt, dPick);
    if (!chop.valid()) return std::nullopt;

    const double ev = s_.variation.event;
    Event e;
    e.chop = chop;
    e.tx.level = (gridStart % beatT_ == 0) ? 1.0f : 0.85f;
    if (s_.allowReverse && dRev < 0.15 * ev) e.tx.reverse = true;
    if (s_.allowPitch && s_.pitchRange > 0 && dPitch < 0.2 * ev) e.tx.pitchSemitones = static_cast<float>(static_cast<int>(vPitch) - s_.pitchRange);
    if (s_.allowRetrigger && dRet < 0.15 * ev) e.tx.retrigger = static_cast<std::uint8_t>(2 + vRet);
    if (!s_.grid.triplet && s_.grid.division >= 8 && (slotIdx % 2 == 1)) {
      const std::int64_t permille = std::llround(s_.swing * 1000.0);  // single rounding, no fusable multiply-add
      t += g_ * permille / 3000;
    }
    if (s_.maxShiftTicks > 0 && dShift < ev) t += static_cast<Ticks>(vShift) - s_.maxShiftTicks;
    const Ticks minDur = std::max<Ticks>(1, g_ / 4);
    t = std::max<Ticks>(0, std::min<Ticks>(t, barT_ - minDur));
    e.start = t;
    e.duration = minDur;

    Made m;
    m.primary = e;
    if (dSub < 0.15 * ev && g_ / 2 >= 120) {
      Event sub = e;
      sub.start = std::min<Ticks>(gridStart + g_ / 2, barT_ - minDur);
      sub.tx.level = e.tx.level * 0.7f;
      sub.tx.reverse = false;
      sub.tx.retrigger = 1;
      if (sub.start > e.start) m.sub = sub;
    }
    return m;
  }

  bool covered(const Beat& bt, Ticks t) const {
    for (const Event& e : bt.events)
      if (e.start <= t && t < e.start + e.duration) return true;
    return false;
  }

  void fillBeat(Pattern& p, int bar, int beat, int budget, std::unordered_set<std::uint64_t>& fresh, bool forceOne) {
    if (budget <= 0) return;
    Beat& bt = p.bars[static_cast<std::size_t>(bar)].beats[static_cast<std::size_t>(beat)];
    const Ticks bStart = static_cast<Ticks>(beat) * beatT_;
    const Ticks bEnd = bStart + beatT_;
    Rng br(deriveKey(seed_, kStreamBeat, static_cast<std::uint64_t>(bar), static_cast<std::uint64_t>(beat)));
    struct Slot {
      int idx;
      double w;
    };
    std::vector<Slot> active;
    std::vector<int> candidatesInBeat;
    for (int i = 0; i < slots_; ++i) {
      const Ticks t = static_cast<Ticks>(i) * g_;
      if (t < bStart || t >= bEnd) continue;
      const double w = detail::metricWeight(t, beatT_);
      const double prob = s_.density >= 1.0 ? 1.0 : std::min(1.0, s_.density * w / avgW_);
      const double u = br.uniform01();  // always consumed so later slots never shift
      if (covered(bt, t)) continue;
      candidatesInBeat.push_back(i);
      if (u < prob) active.push_back({i, w});
    }
    if (active.empty() && forceOne && s_.density > 0.0 && !candidatesInBeat.empty())
      active.push_back({candidatesInBeat.front(), detail::metricWeight(static_cast<Ticks>(candidatesInBeat.front()) * g_, beatT_)});
    if (static_cast<int>(active.size()) > budget) {
      std::stable_sort(active.begin(), active.end(), [](const Slot& a, const Slot& b) { return a.w != b.w ? a.w > b.w : a.idx < b.idx; });
      active.resize(static_cast<std::size_t>(budget));
      std::sort(active.begin(), active.end(), [](const Slot& a, const Slot& b) { return a.idx < b.idx; });
      note(Note::Kind::LimitReached, 0, bar, beat);
    }
    int placed = 0;
    for (const Slot& sl : active) {
      if (placed >= budget) break;
      auto made = makeEvent(p, bar, beat, sl.idx);
      if (!made) continue;
      made->primary.id = EventId{p.nextId++};
      bt.events.push_back(made->primary);
      fresh.insert(made->primary.id.value);
      ++placed;
      if (made->sub && placed < budget) {
        made->sub->id = EventId{p.nextId++};
        bt.events.push_back(*made->sub);
        fresh.insert(made->sub->id.value);
        ++placed;
      }
    }
  }

  void enforceRequired(Pattern& p, int bar, int beat, const std::unordered_set<std::uint64_t>& fresh) {
    if (!policy_) return;
    Beat& bt = p.bars[static_cast<std::size_t>(bar)].beats[static_cast<std::size_t>(beat)];
    ScopeContext sc;
    sc.level = ScopeLevel::Beat;
    sc.bar = bar;
    sc.barCount = s_.bars;
    sc.beat = beat;
    sc.beatsPerBar = beats_;
    auto placedChops = [&]() {
      std::vector<ChopId> v;
      for (const Event& e : bt.events) v.push_back(e.chop);
      return v;
    };
    for (const Requirement& rq : policy_->required(sc, placedChops())) {
      const std::vector<ChopId> placed = placedChops();
      if (std::find(placed.begin(), placed.end(), rq.chop) != placed.end()) continue;
      Event* target = nullptr;
      for (auto it = bt.events.rbegin(); it != bt.events.rend(); ++it)
        if (fresh.count(it->id.value)) {
          target = &*it;
          break;
        }
      if (!target) {
        note(Note::Kind::BlockedByRule, rq.rule, bar, beat);
        continue;
      }
      const Decision d = policy_->evaluate(query(p, bar, target->start, rq.chop));
      if (!d.allowed) {
        note(Note::Kind::BlockedByRule, d.blockedBy ? d.blockedBy : rq.rule, bar, beat);
        continue;
      }
      target->chop = rq.chop;
    }
  }

  // Hard rules must hold for every generated event, including ones copied from another beat or bar (a
  // copy can land in a position the original never occupied). Walks the bar in time order; a copied or
  // fresh event the policy now rejects is swapped for an allowed chop, or removed if none is allowed.
  void enforceHard(Pattern& p, int bar, const std::unordered_set<std::uint64_t>& fresh) {
    if (!policy_) return;
    Bar& B = p.bars[static_cast<std::size_t>(bar)];
    std::vector<std::pair<Ticks, std::uint64_t>> order;
    for (const Beat& bt : B.beats)
      for (const Event& e : bt.events)
        if (fresh.count(e.id.value)) order.push_back({e.start, e.id.value});
    std::sort(order.begin(), order.end());
    for (const auto& [start, idv] : order) {
      Beat* holder = nullptr;
      std::size_t index = 0;
      for (Beat& bt : B.beats)
        for (std::size_t i = 0; i < bt.events.size(); ++i)
          if (bt.events[i].id.value == idv) {
            holder = &bt;
            index = i;
          }
      if (!holder) continue;
      Event& e = holder->events[index];
      const Decision d = policy_->evaluate(query(p, bar, e.start, e.chop));
      if (d.allowed) continue;
      std::vector<ChopId> allowed;
      std::vector<double> weights;
      for (const ChopInfo& c : chops_.chops) {
        const Decision dc = policy_->evaluate(query(p, bar, e.start, c.id));
        if (dc.allowed) {
          allowed.push_back(c.id);
          weights.push_back(dc.weight);
        }
      }
      if (allowed.empty()) {
        note(Note::Kind::BlockedByRule, d.blockedBy, bar, static_cast<int>(std::min<Ticks>(beats_ - 1, e.start / beatT_)));
        holder->events.erase(holder->events.begin() + static_cast<std::ptrdiff_t>(index));
        continue;
      }
      Rng r(deriveKey(seed_, kStreamEvent + 100, static_cast<std::uint64_t>(bar), idv));
      e.chop = allowed[pickByU(weights, r.uniform01())];
      e.region = {};
    }
  }

  Event copyOfEvent(Pattern& p, const Event& src, Ticks shift) const {
    Event e = src;
    e.id = EventId{p.nextId++};
    e.start += shift;
    e.locked = false;
    e.userOwned = false;
    e.child.reset();
    e.childActive = true;
    return e;
  }

  void generateBar(Pattern& p, int bar) {
    {
      Bar b;
      b.id = ScopeId{p.nextId++};
      b.beats.resize(static_cast<std::size_t>(beats_));
      for (Beat& bt : b.beats) bt.id = ScopeId{p.nextId++};
      p.bars.push_back(std::move(b));
    }
    const std::size_t bi = static_cast<std::size_t>(bar);
    std::unordered_set<std::uint64_t> fresh;
    Rng br(deriveKey(seed_, kStreamBar, static_cast<std::uint64_t>(bar)));
    const double uCopy = br.uniform01();
    const std::uint64_t pickSrc = br.uniform(bar > 0 ? static_cast<std::uint64_t>(bar) : 1);
    const bool copy = bar > 0 && uCopy >= s_.variation.phrase;
    if (copy) {
      const std::size_t j = static_cast<std::size_t>(pickSrc);
      p.bars[bi].copyOf = static_cast<int>(j);
      for (int bt = 0; bt < beats_; ++bt) {
        const std::size_t b = static_cast<std::size_t>(bt);
        for (const Event& src : p.bars[j].beats[b].events) {
          Event e = copyOfEvent(p, src, 0);
          fresh.insert(e.id.value);
          p.bars[bi].beats[b].events.push_back(std::move(e));
        }
        const double u = br.uniform01();
        if (u < s_.variation.bar) {
          p.bars[bi].beats[b].events.clear();
          fillBeat(p, bar, bt, beatBudget(bt), fresh, false);
        }
        enforceRequired(p, bar, bt, fresh);  // every beat, however it was assembled
      }
    } else {
      for (int bt = 0; bt < beats_; ++bt) {
        const std::size_t b = static_cast<std::size_t>(bt);
        const double u = br.uniform01();
        bool repeated = false;
        if (bt > 0 && u >= s_.variation.beat) {
          const std::vector<Event> prev = p.bars[bi].beats[b - 1].events;
          if (!prev.empty() && static_cast<int>(prev.size()) <= beatBudget(bt)) {
            for (const Event& src : prev) {
              if (src.start + beatT_ >= barT_) continue;
              Event e = copyOfEvent(p, src, beatT_);
              fresh.insert(e.id.value);
              p.bars[bi].beats[b].events.push_back(std::move(e));
            }
            repeated = !p.bars[bi].beats[b].events.empty();
          }
        }
        if (!repeated) fillBeat(p, bar, bt, beatBudget(bt), fresh, false);
        enforceRequired(p, bar, bt, fresh);
      }
    }
    bool empty = true;
    for (const Beat& bt : p.bars[bi].beats)
      if (!bt.events.empty()) empty = false;
    if (empty && s_.density > 0.0 && s_.maxEvents / s_.bars >= 1) {
      fillBeat(p, bar, 0, 1, fresh, true);
      enforceRequired(p, bar, 0, fresh);
    }
    enforceHard(p, bar, fresh);
    detail::assignDurations(p, bar, &fresh);
  }

  // ---- mutation ----
  bool retained(const Pattern& orig, int bar, const Event& e, bool includeEdited) const {
    if (e.locked) return true;
    if (e.userOwned && !includeEdited) return true;
    if (policy_ && policy_->isPreserved(query(orig, bar, e.start, e.chop))) return true;
    return false;
  }

  Result<Pattern> mutateImpl(const Pattern& in, const MutateOptions& opt) {
    Pattern work = in;
    work.settings.seed = opt.seed;
    struct Target {
      int bar;
      int beat;
      std::vector<bool> keep;
    };
    std::vector<Target> targets;
    for (int bar = 0; bar < s_.bars; ++bar) {
      const Bar& B = in.bars[static_cast<std::size_t>(bar)];
      if (B.locked || (B.userOwned && !opt.includeEdited)) {
        note(Note::Kind::LockedSkipped, 0, bar, -1);
        continue;
      }
      for (int beat = 0; beat < beats_; ++beat) {
        const Beat& Bt = B.beats[static_cast<std::size_t>(beat)];
        if (Bt.locked || (Bt.userOwned && !opt.includeEdited)) {
          note(Note::Kind::LockedSkipped, 0, bar, beat);
          continue;
        }
        Rng r(deriveKey(opt.seed, kStreamMutate, static_cast<std::uint64_t>(bar), static_cast<std::uint64_t>(beat)));
        if (!(r.uniform01() < opt.amount)) continue;
        Target t{bar, beat, {}};
        for (const Event& e : Bt.events) t.keep.push_back(retained(in, bar, e, opt.includeEdited));
        targets.push_back(std::move(t));
      }
    }
    // Remove everything that is not retained, then regenerate with the remaining budget.
    for (const Target& t : targets) {
      auto& evs = work.bars[static_cast<std::size_t>(t.bar)].beats[static_cast<std::size_t>(t.beat)].events;
      std::vector<Event> kept;
      for (std::size_t i = 0; i < evs.size(); ++i)
        if (t.keep[i]) kept.push_back(evs[i]);
      evs = std::move(kept);
    }
    if (targets.empty()) return work;
    const int total = static_cast<int>(eventCount(work));
    const int remaining = std::max(0, s_.maxEvents - total);
    const int k = static_cast<int>(targets.size());
    std::unordered_set<std::uint64_t> fresh;
    std::unordered_set<int> touched;
    for (int i = 0; i < k; ++i) {
      const int budget = remaining / k + (i < remaining % k ? 1 : 0);
      const Target& tg = targets[static_cast<std::size_t>(i)];
      fillBeat(work, tg.bar, tg.beat, budget, fresh, false);
      enforceRequired(work, tg.bar, tg.beat, fresh);
      touched.insert(tg.bar);
    }
    for (int bar : touched) {
      enforceHard(work, bar, fresh);
      detail::assignDurations(work, bar, &fresh);
    }
    return work;
  }

 private:
  const ChopSnapshot& chops_;
  const Settings& s_;
  const ICandidatePolicy* policy_;
  GenerationReport* report_;
  std::uint64_t seed_;
  Ticks barT_ = 0, beatT_ = 0, g_ = 0;
  int slots_ = 1, beats_ = 4;
  double avgW_ = 1.0;
};

}  // namespace

Result<Pattern> generate(const ChopSnapshot& chops, const Settings& settings, const ICandidatePolicy* policy, GenerationReport* report) {
  Status v = validateSettings(settings);
  if (!v.ok()) return v.error();
  if (chops.chops.empty()) return makeError(ErrorCode::InvalidArgument, "there are no enabled chops to arrange");
  Pattern p;
  p.settings = settings;
  p.phraseId = ScopeId{p.nextId++};
  Gen gen(chops, p.settings, policy, report, settings.seed);
  for (int bar = 0; bar < settings.bars; ++bar) gen.generateBar(p, bar);
  return p;
}

Result<Pattern> mutate(const Pattern& pattern, const ChopSnapshot& chops, const MutateOptions& options,
                       const ICandidatePolicy* policy, GenerationReport* report) {
  Status v = validateSettings(pattern.settings);
  if (!v.ok()) return v.error();
  if (!(options.amount >= 0.0 && options.amount <= 1.0)) return makeError(ErrorCode::OutOfRange, "mutation amount must lie in [0, 1]");
  if (chops.chops.empty()) return makeError(ErrorCode::InvalidArgument, "there are no enabled chops to arrange");
  if (pattern.engineVersion != kEngineVersion)
    return makeError(ErrorCode::UnsupportedVersion, "pattern was generated by a different engine version; regenerate it", pattern.engineVersion);
  if (static_cast<int>(pattern.bars.size()) != pattern.settings.bars) return makeError(ErrorCode::Corrupt, "pattern structure does not match its settings");
  if (pattern.phraseLocked) {
    if (report) report->notes.push_back({Note::Kind::LockedSkipped, 0, -1, -1});
    return pattern;  // byte-for-byte unchanged, seed included
  }
  Pattern scratch = pattern;
  scratch.settings.seed = options.seed;
  Gen gen(chops, scratch.settings, policy, report, options.seed);
  return gen.mutateImpl(pattern, options);
}

}  // namespace chopfractal::pattern
