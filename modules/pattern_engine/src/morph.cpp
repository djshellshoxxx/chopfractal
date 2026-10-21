#include <chopfractal/pattern_engine/morph.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>

namespace chopfractal::pattern {
namespace {

double lerp(double x, double y, double t) { return x + (y - x) * t; }
float lerpf(float x, float y, double t) { return static_cast<float>(lerp(static_cast<double>(x), static_cast<double>(y), t)); }

using EventList = std::vector<Event>;

// The top-level events of each bar, sorted by (start, id): the order that defines "index" in the matching rules.
std::vector<EventList> eventsByBar(const Pattern& p) {
  std::vector<EventList> out(p.bars.size());
  for (std::size_t b = 0; b < p.bars.size(); ++b) {
    for (const Beat& bt : p.bars[b].beats) out[b].insert(out[b].end(), bt.events.begin(), bt.events.end());
    std::sort(out[b].begin(), out[b].end(), [](const Event& x, const Event& y) { return x.start != y.start ? x.start < y.start : x.id.value < y.id.value; });
  }
  return out;
}

struct BarMatch {
  std::vector<std::pair<std::size_t, std::size_t>> pairs;  // indexes into the sorted lists
  std::vector<std::size_t> onlyA, onlyB;
};

// Pass 1: same id, same chop, within one grid cell. Pass 2: nearest start within one grid cell (a different chop only at
// identical start), greedy by (cost, index in A, index in B). Deterministic: no hashing, no unordered iteration.
BarMatch matchBar(const EventList& a, const EventList& b, Ticks g) {
  BarMatch m;
  std::vector<char> usedA(a.size(), 0), usedB(b.size(), 0);
  for (std::size_t i = 0; i < a.size(); ++i)
    for (std::size_t j = 0; j < b.size(); ++j) {
      if (usedB[j] || a[i].id != b[j].id || a[i].chop != b[j].chop) continue;
      if (std::llabs(a[i].start - b[j].start) > g) continue;
      usedA[i] = usedB[j] = 1;
      m.pairs.emplace_back(i, j);
      break;
    }
  struct Cand {
    Ticks cost;
    std::size_t i, j;
  };
  std::vector<Cand> cands;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (usedA[i]) continue;
    for (std::size_t j = 0; j < b.size(); ++j) {
      if (usedB[j]) continue;
      const Ticks cost = std::llabs(a[i].start - b[j].start) + (a[i].chop != b[j].chop ? g : 0);
      if (cost <= g) cands.push_back({cost, i, j});
    }
  }
  std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return std::tie(x.cost, x.i, x.j) < std::tie(y.cost, y.i, y.j); });
  for (const Cand& c : cands) {
    if (usedA[c.i] || usedB[c.j]) continue;
    usedA[c.i] = usedB[c.j] = 1;
    m.pairs.emplace_back(c.i, c.j);
  }
  std::sort(m.pairs.begin(), m.pairs.end());
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!usedA[i]) m.onlyA.push_back(i);
  for (std::size_t j = 0; j < b.size(); ++j)
    if (!usedB[j]) m.onlyB.push_back(j);
  return m;
}

float neutralCutoff(FilterType t) { return t == FilterType::HighPass ? 0.f : 1.f; }

SampleRange lerpRegion(const Event& a, const Event& b, const Event& sw, double t, const ChopSnapshot& chops, ChopId chop) {
  if (a.region.empty() && b.region.empty()) return {};
  if (a.chop != b.chop || a.region.empty() || b.region.empty()) return sw.region;
  const ChopInfo* c = chops.find(chop);
  if (!c) return sw.region;
  std::int64_t s = std::llround(lerp(static_cast<double>(a.region.start), static_cast<double>(b.region.start), t));
  std::int64_t e = std::llround(lerp(static_cast<double>(a.region.end), static_cast<double>(b.region.end), t));
  s = std::min(std::max(s, c->range.start), c->range.end - 1);
  e = std::min(std::max(e, s + 1), c->range.end);
  return {s, e};
}

// Matched pair: numeric fields interpolate, discrete fields switch at t >= 0.5 (spec section 6 table). `bChild` reports
// that the merged hit carries a child tree that came from B.
Event mergeHits(const Event& ea, const Event& eb, double t, const ChopSnapshot& chops, Ticks barT, bool* bChild) {
  const bool pickB = t >= 0.5;
  const Event& sw = pickB ? eb : ea;
  Event m = ea;  // keeps A's id, lock, scope flags
  const bool childSide = sw.child != nullptr;
  m.chop = sw.chop;
  if (childSide) {  // the child window must stay valid, so the hit takes the chosen side's geometry
    m.start = sw.start;
    m.duration = sw.duration;
    m.region = sw.region;
  } else {
    Ticks dur = std::max<Ticks>(1, static_cast<Ticks>(std::llround(lerp(static_cast<double>(ea.duration), static_cast<double>(eb.duration), t))));
    dur = std::min(dur, barT);
    Ticks start = static_cast<Ticks>(std::llround(lerp(static_cast<double>(ea.start), static_cast<double>(eb.start), t)));
    start = std::min(std::max<Ticks>(start, 0), barT - dur);
    m.start = start;
    m.duration = dur;
    m.region = lerpRegion(ea, eb, sw, t, chops, m.chop);
  }
  m.child = sw.child;
  m.childActive = sw.childActive;
  m.enabled = sw.enabled;
  m.sourceOverride = sw.sourceOverride;
  *bChild = pickB && sw.child != nullptr;

  m.tx.level = lerpf(ea.tx.level, eb.tx.level, t);
  m.tx.pan = lerpf(ea.tx.pan, eb.tx.pan, t);
  m.tx.pitchSemitones = std::min(24.f, std::max(-24.f, lerpf(ea.tx.pitchSemitones, eb.tx.pitchSemitones, t)));
  m.tx.reverse = sw.tx.reverse;
  m.tx.retrigger = static_cast<std::uint8_t>(std::min<long long>(limits::kMaxRetrigger, std::max<long long>(1, std::llround(lerp(ea.tx.retrigger, eb.tx.retrigger, t)))));

  // Filter: equal types blend; one side Off follows the other side's type with a neutral cutoff on the Off side;
  // two different filters switch.
  const EventFx& fa = ea.fx;
  const EventFx& fb = eb.fx;
  if (fa.filter == fb.filter) {
    m.fx.filter = fa.filter;
    m.fx.cutoff = lerpf(fa.cutoff, fb.cutoff, t);
  } else if (fa.filter == FilterType::Off || fb.filter == FilterType::Off) {
    const FilterType type = fa.filter == FilterType::Off ? fb.filter : fa.filter;
    m.fx.filter = type;
    m.fx.cutoff = lerpf(fa.filter == FilterType::Off ? neutralCutoff(type) : fa.cutoff, fb.filter == FilterType::Off ? neutralCutoff(type) : fb.cutoff, t);
  } else {
    m.fx.filter = sw.fx.filter;
    m.fx.cutoff = sw.fx.cutoff;
  }
  m.fx.resonance = lerpf(fa.resonance, fb.resonance, t);
  m.fx.glideSemitones = lerpf(fa.glideSemitones, fb.glideSemitones, t);
  m.fx.crush = lerpf(fa.crush, fb.crush, t);
  // Gesture-effect blocks are discrete: the chosen side's block (or none) plays.
  m.echo = sw.echo;
  m.tremPan = sw.tremPan;
  m.stutter = sw.stutter;
  m.roll = sw.roll;

  m.probability = lerpf(ea.probability, eb.probability, t);
  m.locked = ea.locked;
  m.userOwned = ea.userOwned || eb.userOwned;
  return m;
}

struct Placed {
  Event e;
  int bar = 0;
  bool fromB = false;  // carries B-side children
};

Pattern assemble(const Pattern& a, const Pattern& b, double t, std::vector<Placed>& placed, std::uint64_t nextId, bool dropBChildren) {
  Pattern out = a;
  for (Bar& bar : out.bars)
    for (Beat& bt : bar.beats) bt.events.clear();
  const Ticks beatT = ticksPerBeat(a.settings.timeSignature);
  for (Placed& p : placed) {
    Bar& bar = out.bars[static_cast<std::size_t>(p.bar)];
    std::size_t idx = beatT > 0 ? static_cast<std::size_t>(p.e.start / beatT) : 0;
    idx = std::min(idx, bar.beats.size() - 1);
    Event e = p.e;
    if (dropBChildren && p.fromB) {
      e.child.reset();
      e.childActive = true;
    }
    bar.beats[idx].events.push_back(std::move(e));
  }
  for (std::size_t bi = 0; bi < out.bars.size(); ++bi) {
    Bar& bar = out.bars[bi];
    for (std::size_t j = 0; j < bar.beats.size(); ++j) {
      auto& ev = bar.beats[j].events;
      std::sort(ev.begin(), ev.end(), [](const Event& x, const Event& y) { return x.start != y.start ? x.start < y.start : x.id.value < y.id.value; });
      if (bi < b.bars.size() && j < b.bars[bi].beats.size()) bar.beats[j].userOwned = bar.beats[j].userOwned || b.bars[bi].beats[j].userOwned;
    }
    if (bi < b.bars.size()) bar.userOwned = bar.userOwned || b.bars[bi].userOwned;
  }
  Settings& s = out.settings;
  const Settings& sa = a.settings;
  const Settings& sb = b.settings;
  s.swing = lerp(sa.swing, sb.swing, t);
  s.density = lerp(sa.density, sb.density, t);
  s.variation.phrase = lerp(sa.variation.phrase, sb.variation.phrase, t);
  s.variation.bar = lerp(sa.variation.bar, sb.variation.bar, t);
  s.variation.beat = lerp(sa.variation.beat, sb.variation.beat, t);
  s.variation.event = lerp(sa.variation.event, sb.variation.event, t);
  s.fxIntensity = lerp(sa.fxIntensity, sb.fxIntensity, t);
  s.allowReverse = sa.allowReverse || sb.allowReverse;
  s.allowPitch = sa.allowPitch || sb.allowPitch;
  s.allowRetrigger = sa.allowRetrigger || sb.allowRetrigger;
  s.allowFilter = sa.allowFilter || sb.allowFilter;
  s.allowGlide = sa.allowGlide || sb.allowGlide;
  s.allowCrunch = sa.allowCrunch || sb.allowCrunch;
  s.pitchRange = std::max(sa.pitchRange, sb.pitchRange);
  s.maxRetrigger = std::max(sa.maxRetrigger, sb.maxRetrigger);
  s.maxShiftTicks = std::max(sa.maxShiftTicks, sb.maxShiftTicks);
  s.maxEvents = std::min(sa.maxEvents, sb.maxEvents);
  out.nextId = nextId;
  return out;
}

}  // namespace

Result<Pattern> morph(const Pattern& a, const Pattern& b, double t, const ChopSnapshot& chops, bool freeze, MorphReport* report) {
  if (report) *report = MorphReport{};
  if (!std::isfinite(t)) return makeError(ErrorCode::InvalidArgument, "the morph amount must be a number");
  if (Status v = validate(a); !v.ok()) return v.error();
  if (Status v = validate(b); !v.ok()) return v.error();
  if (a.settings.bars != b.settings.bars || a.bars.size() != b.bars.size() || !(a.settings.timeSignature == b.settings.timeSignature) ||
      !(a.settings.grid == b.settings.grid))
    return makeError(ErrorCode::Conflict, "A and B differ in pattern length, meter or grid, so they cannot be morphed.");
  if (t <= 0.0) return a;
  if (t >= 1.0) return b;

  const Pattern sa = sanitize(a, chops);
  const Pattern sb = sanitize(b, chops);
  const Ticks g = gridTicks(sa.settings.grid) * kMorphMatchGridCells;
  const Ticks barT = barTicks(sa.settings);
  const std::vector<EventList> ea = eventsByBar(sa);
  const std::vector<EventList> eb = eventsByBar(sb);

  std::vector<Placed> placed;
  std::vector<BarMatch> matches(ea.size());
  MorphReport rep;
  for (std::size_t bar = 0; bar < ea.size(); ++bar) {
    matches[bar] = matchBar(ea[bar], eb[bar], g);
    rep.matched += matches[bar].pairs.size();
    rep.onlyA += matches[bar].onlyA.size();
    rep.onlyB += matches[bar].onlyB.size();
  }
  const std::size_t cap = static_cast<std::size_t>(std::min(sa.settings.maxEvents, sb.settings.maxEvents));
  const std::size_t total = rep.matched + rep.onlyA + rep.onlyB;
  if (total > cap)
    return makeError(ErrorCode::LimitExceeded, "A and B have too many different hits to morph (" + std::to_string(total) + " > " + std::to_string(cap) + ").",
                     static_cast<std::int64_t>(cap));

  // B-only hits get fresh ids ranked by (bar, start, original id), so the ids do not depend on t.
  const std::uint64_t base = std::max(sa.nextId, sb.nextId);
  struct BOnly {
    int bar;
    Ticks start;
    std::uint64_t id;
    std::size_t index;
  };
  std::vector<BOnly> bonly;
  for (std::size_t bar = 0; bar < eb.size(); ++bar)
    for (std::size_t j : matches[bar].onlyB) bonly.push_back({static_cast<int>(bar), eb[bar][j].start, eb[bar][j].id.value, j});
  std::sort(bonly.begin(), bonly.end(), [](const BOnly& x, const BOnly& y) { return std::tie(x.bar, x.start, x.id) < std::tie(y.bar, y.start, y.id); });
  std::vector<std::vector<std::uint64_t>> freshId(eb.size());
  for (std::size_t bar = 0; bar < eb.size(); ++bar) freshId[bar].assign(eb[bar].size(), 0);
  for (std::size_t k = 0; k < bonly.size(); ++k) freshId[static_cast<std::size_t>(bonly[k].bar)][bonly[k].index] = base + k;

  const std::uint64_t probSeed = probabilitySeedFor(sa.settings.seed);
  for (std::size_t bar = 0; bar < ea.size(); ++bar) {
    for (const auto& pr : matches[bar].pairs) {
      bool bChild = false;
      Event m = mergeHits(ea[bar][pr.first], eb[bar][pr.second], t, chops, barT, &bChild);
      placed.push_back({std::move(m), static_cast<int>(bar), bChild});
    }
    for (std::size_t i : matches[bar].onlyA) {
      Event e = ea[bar][i];
      const float weight = static_cast<float>(static_cast<double>(e.probability) * (1.0 - t));
      if (freeze) {
        if (!probabilityPasses(probSeed, e.id, weight)) continue;  // stays at its base probability
      } else {
        e.probability = weight;
      }
      placed.push_back({std::move(e), static_cast<int>(bar), false});
    }
    for (std::size_t j : matches[bar].onlyB) {
      Event e = eb[bar][j];
      e.id = EventId{freshId[bar][j]};
      const float weight = static_cast<float>(static_cast<double>(e.probability) * t);
      if (freeze) {
        if (!probabilityPasses(probSeed, e.id, weight)) continue;
      } else {
        e.probability = weight;
      }
      placed.push_back({std::move(e), static_cast<int>(bar), true});
    }
  }

  const std::uint64_t nextId = base + bonly.size();
  Pattern out = assemble(sa, sb, t, placed, nextId, false);
  Status v = validate(out);
  if (!v.ok()) {  // derived child ids of B-side hits can collide with ids from A: drop those children and try again
    out = assemble(sa, sb, t, placed, nextId, true);
    v = validate(out);
    if (!v.ok()) return v.error();
    rep.childrenDropped = true;
  }
  if (report) *report = rep;
  return out;
}

}  // namespace chopfractal::pattern
