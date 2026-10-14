#include <chopfractal/pattern_engine/morph.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace chopfractal::pattern {
namespace {

struct Item {
  Event e;
  std::size_t order = 0;  // index after sorting the bar's events by (start, id)
  bool matched = false;
};

double lerp(double x, double y, double t) { return x + (y - x) * t; }

std::vector<Item> sideEvents(const Bar& bar, const ChopSnapshot& chops) {
  std::vector<Item> v;
  for (const Beat& bt : bar.beats)
    for (const Event& e : bt.events)
      if (chops.find(e.chop)) v.push_back({e, 0, false});
  std::sort(v.begin(), v.end(), [](const Item& l, const Item& r) {
    return l.e.start != r.e.start ? l.e.start < r.e.start : l.e.id.value < r.e.id.value;
  });
  for (std::size_t i = 0; i < v.size(); ++i) v[i].order = i;
  return v;
}

float neutralCutoff(FilterType f) { return f == FilterType::HighPass ? 0.f : 1.f; }

EventFx blendFx(const EventFx& a, const EventFx& b, double t) {
  EventFx r;
  const bool aOff = a.filter == FilterType::Off, bOff = b.filter == FilterType::Off;
  if (a.filter == b.filter) r.filter = a.filter;
  else if (aOff) r.filter = b.filter;
  else if (bOff) r.filter = a.filter;
  else r.filter = t < 0.5 ? a.filter : b.filter;
  if (r.filter != FilterType::Off) {
    const bool differ = !aOff && !bOff && a.filter != b.filter;
    const float ca = aOff ? neutralCutoff(r.filter) : a.cutoff, cb = bOff ? neutralCutoff(r.filter) : b.cutoff;
    r.cutoff = differ ? (t < 0.5 ? a.cutoff : b.cutoff) : static_cast<float>(lerp(ca, cb, t));
  }
  r.resonance = static_cast<float>(lerp(a.resonance, b.resonance, t));
  r.glideSemitones = static_cast<float>(lerp(a.glideSemitones, b.glideSemitones, t));
  r.crush = static_cast<float>(lerp(a.crush, b.crush, t));
  return r;
}

Event blend(const Event& a, const Event& b, double t, Ticks barT, const ChopSnapshot& chops) {
  const bool toB = t >= 0.5;
  const Event& pick = toB ? b : a;
  Event r = a;  // keeps A's id and locked flag
  r.userOwned = a.userOwned || b.userOwned;
  r.tx.level = static_cast<float>(lerp(a.tx.level, b.tx.level, t));
  r.tx.pan = static_cast<float>(lerp(a.tx.pan, b.tx.pan, t));
  r.tx.pitchSemitones = std::clamp(static_cast<float>(lerp(a.tx.pitchSemitones, b.tx.pitchSemitones, t)), -limits::kMaxPitchSemitones, limits::kMaxPitchSemitones);
  r.tx.reverse = pick.tx.reverse;
  r.tx.retrigger = static_cast<std::uint8_t>(std::clamp<long long>(std::llround(lerp(a.tx.retrigger, b.tx.retrigger, t)), 1, limits::kMaxRetrigger));
  r.fx = blendFx(a.fx, b.fx, t);
  r.probability = static_cast<float>(lerp(a.probability, b.probability, t));
  r.enabled = pick.enabled;
  r.sourceOverride = pick.sourceOverride;
  r.childActive = pick.childActive;
  r.child = pick.child;
  r.chop = pick.chop;
  if (pick.child) {  // the child window must keep equaling its parent's duration
    r.start = pick.start;
    r.duration = pick.duration;
    r.region = pick.region;
    return r;
  }
  r.duration = std::max<Ticks>(1, std::llround(lerp(static_cast<double>(a.duration), static_cast<double>(b.duration), t)));
  r.start = std::clamp<Ticks>(std::llround(lerp(static_cast<double>(a.start), static_cast<double>(b.start), t)), 0, barT - r.duration);
  if (a.region.empty() && b.region.empty()) r.region = {};
  else if (a.chop == b.chop && !a.region.empty() && !b.region.empty()) {
    const ChopInfo* c = chops.find(a.chop);
    std::int64_t s = std::llround(lerp(static_cast<double>(a.region.start), static_cast<double>(b.region.start), t));
    std::int64_t e = std::llround(lerp(static_cast<double>(a.region.end), static_cast<double>(b.region.end), t));
    if (c) {
      s = std::clamp(s, c->range.start, c->range.end - 1);
      e = std::clamp(e, s + 1, c->range.end);
    }
    r.region = {s, e};
  } else {
    r.region = pick.region;
  }
  return r;
}

void dropChildren(std::vector<std::vector<Event>>& bars, const std::vector<std::vector<bool>>& fromB) {
  for (std::size_t i = 0; i < bars.size(); ++i)
    for (std::size_t j = 0; j < bars[i].size(); ++j)
      if (fromB[i][j] && bars[i][j].child) {
        bars[i][j].child.reset();
        bars[i][j].childActive = true;
      }
}

Result<Pattern> build(const Pattern& a, const Pattern& b, const std::vector<std::vector<Event>>& events, std::uint64_t nextId, double t) {
  Pattern q = a;
  const Ticks barT = barTicks(a.settings);
  for (std::size_t i = 0; i < q.bars.size(); ++i) {
    Bar& bar = q.bars[i];
    if (i < b.bars.size()) bar.userOwned = a.bars[i].userOwned || b.bars[i].userOwned;
    const Ticks beatT = std::max<Ticks>(1, barT / static_cast<Ticks>(bar.beats.size()));
    for (std::size_t k = 0; k < bar.beats.size(); ++k) {
      bar.beats[k].events.clear();
      if (i < b.bars.size() && k < b.bars[i].beats.size()) bar.beats[k].userOwned = a.bars[i].beats[k].userOwned || b.bars[i].beats[k].userOwned;
    }
    for (const Event& e : events[i]) {
      const std::size_t beat = std::min<std::size_t>(bar.beats.size() - 1, static_cast<std::size_t>(e.start / beatT));
      bar.beats[beat].events.push_back(e);
    }
    for (Beat& bt : bar.beats)
      std::sort(bt.events.begin(), bt.events.end(), [](const Event& l, const Event& r) {
        return l.start != r.start ? l.start < r.start : l.id.value < r.id.value;
      });
  }
  Settings& s = q.settings;
  const Settings& sa = a.settings;
  const Settings& sb = b.settings;
  s.density = lerp(sa.density, sb.density, t);
  s.swing = lerp(sa.swing, sb.swing, t);
  s.variation = {lerp(sa.variation.phrase, sb.variation.phrase, t), lerp(sa.variation.bar, sb.variation.bar, t),
                 lerp(sa.variation.beat, sb.variation.beat, t), lerp(sa.variation.event, sb.variation.event, t)};
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
  q.nextId = nextId;
  Status v = validate(q);
  if (!v.ok()) return v.error();
  return q;
}

}  // namespace

Result<Pattern> morph(const Pattern& a, const Pattern& b, double t, const ChopSnapshot& chops, bool freeze, MorphReport* report) {
  if (!(t == t)) return makeError(ErrorCode::InvalidArgument, "the morph amount is not a number");
  if (report) *report = {};
  if (a.settings.bars != b.settings.bars || !(a.settings.timeSignature.numerator == b.settings.timeSignature.numerator &&
                                              a.settings.timeSignature.denominator == b.settings.timeSignature.denominator) ||
      a.settings.grid.division != b.settings.grid.division || a.settings.grid.triplet != b.settings.grid.triplet ||
      a.bars.size() != b.bars.size())
    return makeError(ErrorCode::Conflict, "A and B differ in pattern length, meter or grid, so they cannot be morphed.");
  if (!validate(a).ok() || !validate(b).ok()) return makeError(ErrorCode::InvalidArgument, "A or B is not a valid pattern.");
  if (t <= 0.0) return a;
  if (t >= 1.0) return b;

  const Ticks g = gridTicks(a.settings.grid);
  const Ticks barT = barTicks(a.settings);
  const std::uint64_t base = std::max(a.nextId, b.nextId);
  const std::uint64_t probSeed = probabilitySeedFor(a.settings.seed);

  struct Pair { std::size_t ia, ib; };
  std::vector<std::vector<Item>> sideA(a.bars.size()), sideB(a.bars.size());
  std::vector<std::vector<Pair>> pairs(a.bars.size());
  std::size_t matched = 0, onlyA = 0, onlyB = 0;
  for (std::size_t i = 0; i < a.bars.size(); ++i) {
    auto& ea = sideA[i];
    auto& eb = sideB[i];
    ea = sideEvents(a.bars[i], chops);
    eb = sideEvents(b.bars[i], chops);
    for (std::size_t x = 0; x < ea.size(); ++x)  // identity pass
      for (std::size_t y = 0; y < eb.size(); ++y)
        if (!eb[y].matched && ea[x].e.id.value == eb[y].e.id.value && ea[x].e.chop == eb[y].e.chop &&
            std::llabs(ea[x].e.start - eb[y].e.start) <= g) {
          ea[x].matched = eb[y].matched = true;
          pairs[i].push_back({x, y});
          break;
        }
    struct Cand { Ticks cost; std::size_t x, y; };  // proximity pass
    std::vector<Cand> cands;
    for (std::size_t x = 0; x < ea.size(); ++x) {
      if (ea[x].matched) continue;
      for (std::size_t y = 0; y < eb.size(); ++y) {
        if (eb[y].matched) continue;
        const Ticks cost = std::llabs(ea[x].e.start - eb[y].e.start) + (ea[x].e.chop != eb[y].e.chop ? g : 0);
        if (cost <= g) cands.push_back({cost, x, y});
      }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& l, const Cand& r) {
      if (l.cost != r.cost) return l.cost < r.cost;
      return l.x != r.x ? l.x < r.x : l.y < r.y;
    });
    for (const Cand& c : cands)
      if (!ea[c.x].matched && !eb[c.y].matched) {
        ea[c.x].matched = eb[c.y].matched = true;
        pairs[i].push_back({c.x, c.y});
      }
    matched += pairs[i].size();
    for (const Item& it : ea) onlyA += !it.matched;
    for (const Item& it : eb) onlyB += !it.matched;
  }
  if (report) {
    report->matched = matched;
    report->onlyA = onlyA;
    report->onlyB = onlyB;
  }
  const std::size_t total = matched + onlyA + onlyB;
  const std::size_t cap = static_cast<std::size_t>(std::min(a.settings.maxEvents, b.settings.maxEvents));
  if (total > cap)
    return makeError(ErrorCode::LimitExceeded, "A and B have too many different hits to morph (" + std::to_string(total) + " > " + std::to_string(cap) + ").",
                     static_cast<std::int64_t>(cap));

  // B-only ids: rank by (bar, start, original id) so they are the same for every t.
  struct BRef { std::size_t bar, idx; };
  std::vector<BRef> bOnly;
  for (std::size_t i = 0; i < sideB.size(); ++i)
    for (std::size_t y = 0; y < sideB[i].size(); ++y)
      if (!sideB[i][y].matched) bOnly.push_back({i, y});  // already sorted by (bar, start, id)

  auto assemble = [&](bool dropBChildren, std::vector<std::vector<Event>>& out) {
    out.assign(a.bars.size(), {});
    std::vector<std::vector<bool>> fromB(a.bars.size());
    for (std::size_t i = 0; i < a.bars.size(); ++i) {
      for (const Pair& p : pairs[i]) {
        out[i].push_back(blend(sideA[i][p.ia].e, sideB[i][p.ib].e, t, barT, chops));
        fromB[i].push_back(t >= 0.5);
      }
      for (const Item& it : sideA[i]) {
        if (it.matched) continue;
        Event e = it.e;
        const float w = static_cast<float>(it.e.probability * (1.0 - t));
        if (freeze) {
          if (!probabilityPasses(probSeed, e.id, w)) continue;
        } else {
          e.probability = w;
        }
        out[i].push_back(e);
        fromB[i].push_back(false);
      }
    }
    for (std::size_t k = 0; k < bOnly.size(); ++k) {
      Event e = sideB[bOnly[k].bar][bOnly[k].idx].e;
      e.id = EventId{base + k};
      const float w = static_cast<float>(e.probability * t);
      if (freeze) {
        if (!probabilityPasses(probSeed, e.id, w)) continue;
      } else {
        e.probability = w;
      }
      out[bOnly[k].bar].push_back(e);
      fromB[bOnly[k].bar].push_back(true);
    }
    if (dropBChildren) dropChildren(out, fromB);
  };

  std::vector<std::vector<Event>> events;
  assemble(false, events);
  auto q = build(a, b, events, base + bOnly.size(), t);
  if (!q.ok()) {
    assemble(true, events);
    q = build(a, b, events, base + bOnly.size(), t);
    if (q.ok() && report) report->childrenDropped = true;
  }
  return q;
}

}  // namespace chopfractal::pattern
