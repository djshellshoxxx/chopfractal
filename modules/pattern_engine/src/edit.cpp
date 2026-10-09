#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>

#include "internal.hpp"

namespace chopfractal::pattern {
namespace {

Error blocked(const std::string& what) { return makeError(ErrorCode::Blocked, what + " is locked; unlock it first"); }

Event& at(Pattern& p, const Location& l) { return p.bars[static_cast<std::size_t>(l.bar)].beats[static_cast<std::size_t>(l.beat)].events[l.index]; }

bool locationLocked(const Pattern& p, const Location& l) {
  const Bar& b = p.bars[static_cast<std::size_t>(l.bar)];
  const Beat& bt = b.beats[static_cast<std::size_t>(l.beat)];
  return p.phraseLocked || b.locked || bt.locked || bt.events[l.index].locked;
}

bool scopeLocked(const Pattern& p, int bar, int beat) {
  const Bar& b = p.bars[static_cast<std::size_t>(bar)];
  return p.phraseLocked || b.locked || (beat >= 0 && b.beats[static_cast<std::size_t>(beat)].locked);
}

int beatOf(const Pattern& p, Ticks startInBar) {
  const int beats = p.settings.timeSignature.numerator;
  return static_cast<int>(std::min<Ticks>(beats - 1, std::max<Ticks>(0, startInBar) / detail::beatTicksOf(p.settings)));
}

void collectIds(const Event& e, std::unordered_set<std::uint64_t>& ids, bool& dup) {
  if (!ids.insert(e.id.value).second) dup = true;
  if (e.child)
    for (const Event& c : e.child->events) collectIds(c, ids, dup);
}

}  // namespace

std::optional<Location> locate(const Pattern& p, EventId id) {
  for (std::size_t b = 0; b < p.bars.size(); ++b)
    for (std::size_t t = 0; t < p.bars[b].beats.size(); ++t) {
      const auto& evs = p.bars[b].beats[t].events;
      for (std::size_t i = 0; i < evs.size(); ++i)
        if (evs[i].id == id) return Location{static_cast<int>(b), static_cast<int>(t), i};
    }
  return std::nullopt;
}

const Event* findEvent(const Pattern& p, EventId id) {
  auto loc = locate(p, id);
  if (!loc) return nullptr;
  return &p.bars[static_cast<std::size_t>(loc->bar)].beats[static_cast<std::size_t>(loc->beat)].events[loc->index];
}

bool isLocked(const Pattern& p, EventId id) {
  auto loc = locate(p, id);
  return loc && locationLocked(p, *loc);
}

Status validate(const Pattern& p) {
  Status s = validateSettings(p.settings);
  if (!s.ok()) return s;
  if (!p.phraseId.valid()) return makeError(ErrorCode::Corrupt, "pattern has no phrase id");
  if (static_cast<int>(p.bars.size()) != p.settings.bars) return makeError(ErrorCode::Corrupt, "bar count does not match settings");
  const Ticks barT = barTicks(p.settings);
  const std::size_t beats = static_cast<std::size_t>(p.settings.timeSignature.numerator);
  std::unordered_set<std::uint64_t> ids;
  bool dup = false;
  ids.insert(p.phraseId.value);
  std::size_t total = 0;
  for (std::size_t b = 0; b < p.bars.size(); ++b) {
    const Bar& bar = p.bars[b];
    if (!bar.id.valid() || !ids.insert(bar.id.value).second) return makeError(ErrorCode::Corrupt, "bar id invalid or duplicated");
    if (bar.copyOf < -1 || bar.copyOf >= static_cast<int>(b)) return makeError(ErrorCode::Corrupt, "bar copy reference is invalid");
    if (bar.beats.size() != beats) return makeError(ErrorCode::Corrupt, "beat count does not match the time signature");
    for (const Beat& bt : bar.beats) {
      if (!bt.id.valid() || !ids.insert(bt.id.value).second) return makeError(ErrorCode::Corrupt, "beat id invalid or duplicated");
      for (const Event& e : bt.events) {
        Status v = validateEvent(e);
        if (!v.ok()) return v;
        if (e.start + e.duration > barT) return makeError(ErrorCode::OutOfRange, "event extends past its bar");
        collectIds(e, ids, dup);
        ++total;
      }
    }
  }
  if (dup) return makeError(ErrorCode::Corrupt, "duplicate event ids");
  if (total > static_cast<std::size_t>(p.settings.maxEvents)) return makeError(ErrorCode::LimitExceeded, "pattern exceeds its event cap", p.settings.maxEvents);
  for (std::uint64_t id : ids)
    if (!isDerivedId(id) && id >= p.nextId) return makeError(ErrorCode::Corrupt, "id counter is behind an existing id");
  return {};
}

Result<FlatEventList> flatten(const Pattern& p, const ChopSnapshot& chops) {
  FlatEventList out;
  FlattenLimits lim;
  lim.maxEvents = static_cast<std::size_t>(p.settings.maxEvents);
  const Ticks barT = barTicks(p.settings);
  const std::uint64_t probSeed = hashCombine(p.settings.seed, 0x50524F42ull);
  for (std::size_t b = 0; b < p.bars.size(); ++b)
    for (const Beat& bt : p.bars[b].beats) {
      Status s = flattenInto(out, bt.events, static_cast<Ticks>(b) * barT, barT, {}, chops, lim, probSeed);
      if (!s.ok()) return s.error();
    }
  sortFlat(out);
  return out;
}

Result<Pattern> setLock(const Pattern& p, const ScopeRef& scope, bool locked) {
  Pattern q = p;
  switch (scope.level) {
    case ScopeLevel::Phrase:
      q.phraseLocked = locked;
      return q;
    case ScopeLevel::Bar:
      if (scope.bar < 0 || scope.bar >= static_cast<int>(q.bars.size())) return makeError(ErrorCode::OutOfRange, "bar index out of range");
      q.bars[static_cast<std::size_t>(scope.bar)].locked = locked;
      return q;
    case ScopeLevel::Beat:
      if (scope.bar < 0 || scope.bar >= static_cast<int>(q.bars.size()) || scope.beat < 0 ||
          scope.beat >= static_cast<int>(q.bars[static_cast<std::size_t>(scope.bar)].beats.size()))
        return makeError(ErrorCode::OutOfRange, "beat index out of range");
      q.bars[static_cast<std::size_t>(scope.bar)].beats[static_cast<std::size_t>(scope.beat)].locked = locked;
      return q;
    case ScopeLevel::Event: {
      auto loc = locate(q, scope.event);
      if (!loc) return makeError(ErrorCode::NotFound, "event not found");
      at(q, *loc).locked = locked;
      return q;
    }
  }
  return makeError(ErrorCode::InvalidArgument, "unknown lock scope");
}

Pattern clearLocks(const Pattern& p) {
  Pattern q = p;
  q.phraseLocked = false;
  for (Bar& b : q.bars) {
    b.locked = false;
    for (Beat& bt : b.beats) {
      bt.locked = false;
      for (Event& e : bt.events) {
        e.locked = false;
        if (e.child) {
          auto c = std::make_shared<NestedPattern>(*e.child);
          c->locked = false;
          for (Event& ce : c->events) ce.locked = false;
          e.child = std::move(c);
        }
      }
    }
  }
  return q;
}

Result<Pattern> addEvent(const Pattern& p, int bar, ChopId chop, Ticks startInBar, Ticks duration, const ChopSnapshot& chops) {
  if (bar < 0 || bar >= static_cast<int>(p.bars.size())) return makeError(ErrorCode::OutOfRange, "bar index out of range");
  if (!chops.find(chop)) return makeError(ErrorCode::NotFound, "unknown chop");
  const Ticks barT = barTicks(p.settings);
  if (startInBar < 0 || duration <= 0 || duration > barT || startInBar > barT - duration) return makeError(ErrorCode::OutOfRange, "event must lie inside the bar");
  const int beat = beatOf(p, startInBar);
  if (scopeLocked(p, bar, beat)) return blocked("this beat");
  if (static_cast<int>(eventCount(p)) >= p.settings.maxEvents) return makeError(ErrorCode::LimitExceeded, "event cap reached", p.settings.maxEvents);
  Pattern q = p;
  Event e;
  e.id = EventId{q.nextId++};
  e.chop = chop;
  e.start = startInBar;
  e.duration = duration;
  e.userOwned = true;
  q.bars[static_cast<std::size_t>(bar)].beats[static_cast<std::size_t>(beat)].events.push_back(e);
  q.bars[static_cast<std::size_t>(bar)].beats[static_cast<std::size_t>(beat)].userOwned = true;
  return q;
}

Result<Pattern> deleteEvent(const Pattern& p, EventId id) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  if (locationLocked(p, *loc)) return blocked("this event");
  Pattern q = p;
  auto& evs = q.bars[static_cast<std::size_t>(loc->bar)].beats[static_cast<std::size_t>(loc->beat)].events;
  evs.erase(evs.begin() + static_cast<std::ptrdiff_t>(loc->index));
  q.bars[static_cast<std::size_t>(loc->bar)].beats[static_cast<std::size_t>(loc->beat)].userOwned = true;
  return q;
}

Result<Pattern> moveEvent(const Pattern& p, EventId id, int newBar, Ticks newStartInBar) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  if (newBar < 0 || newBar >= static_cast<int>(p.bars.size())) return makeError(ErrorCode::OutOfRange, "bar index out of range");
  const Ticks barT = barTicks(p.settings);
  if (newStartInBar < 0 || newStartInBar >= barT) return makeError(ErrorCode::OutOfRange, "start lies outside the bar");
  if (locationLocked(p, *loc)) return blocked("this event");
  const int destBeat = beatOf(p, newStartInBar);
  if (scopeLocked(p, newBar, destBeat)) return blocked("the destination beat");
  Pattern q = p;
  Event e = at(q, *loc);
  auto& src = q.bars[static_cast<std::size_t>(loc->bar)].beats[static_cast<std::size_t>(loc->beat)];
  src.events.erase(src.events.begin() + static_cast<std::ptrdiff_t>(loc->index));
  src.userOwned = true;
  e.start = newStartInBar;
  const Ticks maxDur = barT - newStartInBar;
  if (e.duration > maxDur) {
    e.duration = maxDur;
    e.child.reset();  // a clipped parent no longer matches its child window
    e.childActive = true;
  }
  e.userOwned = true;
  auto& dst = q.bars[static_cast<std::size_t>(newBar)].beats[static_cast<std::size_t>(destBeat)];
  dst.events.push_back(std::move(e));
  dst.userOwned = true;
  return q;
}

Result<Pattern> setEventChop(const Pattern& p, EventId id, ChopId chop, const ChopSnapshot& chops) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  if (!chops.find(chop)) return makeError(ErrorCode::NotFound, "unknown chop");
  if (locationLocked(p, *loc)) return blocked("this event");
  Pattern q = p;
  Event& e = at(q, *loc);
  e.chop = chop;
  e.region = {};  // a sub-region of the previous chop is meaningless for the new one
  e.child.reset();
  e.childActive = true;
  e.userOwned = true;
  return q;
}

Result<Pattern> setEventTransform(const Pattern& p, EventId id, const EventTransform& tx) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  if (locationLocked(p, *loc)) return blocked("this event");
  Pattern q = p;
  Event& e = at(q, *loc);
  e.tx = tx;
  e.userOwned = true;
  Status v = validateEvent(e);
  if (!v.ok()) return v.error();
  return q;
}

Result<Pattern> setEventFx(const Pattern& p, EventId id, const EventFx& fx) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  if (locationLocked(p, *loc)) return blocked("this event");
  Pattern q = p;
  Event& e = at(q, *loc);
  e.fx = fx;
  e.userOwned = true;
  Status v = validateEvent(e);
  if (!v.ok()) return v.error();
  return q;
}

std::pair<Pattern, EventId> reserveIds(const Pattern& p, std::uint64_t count) {
  Pattern q = p;
  const EventId first{q.nextId};
  if (count > (kDerivedIdBit >> 1) || q.nextId > (kDerivedIdBit >> 1)) return {std::move(q), EventId{}};  // exhausted: an invalid id tells the caller
  q.nextId += count;
  return {std::move(q), first};
}

Result<Pattern> setBarEvents(const Pattern& p, int bar, std::vector<Event> events, const ChopSnapshot& chops) {
  if (bar < 0 || bar >= static_cast<int>(p.bars.size())) return makeError(ErrorCode::OutOfRange, "bar index out of range");
  const Bar& old = p.bars[static_cast<std::size_t>(bar)];
  if (p.phraseLocked || old.locked) return blocked("this bar");
  for (const Beat& bt : old.beats) {
    if (bt.locked) return blocked("a beat in this bar");
    for (const Event& e : bt.events)
      if (e.locked) return blocked("an event in this bar");
  }
  const Ticks bt = barTicks(p.settings);
  Pattern q = p;
  Bar& dst = q.bars[static_cast<std::size_t>(bar)];
  const Ticks beatT = ticksPerBeat(p.settings.timeSignature);
  for (Beat& b : dst.beats) b.events.clear();
  std::size_t count = eventCount(q);
  if (count + events.size() > static_cast<std::size_t>(p.settings.maxEvents))
    return makeError(ErrorCode::LimitExceeded, "too many events for the pattern", p.settings.maxEvents);
  for (Event& e : events) {
    Status v = validateEvent(e);
    if (!v.ok()) return v.error();
    if (e.start + e.duration > bt || !chops.find(e.chop)) return makeError(ErrorCode::OutOfRange, "event lies outside the bar or its chop is unknown");
    if (e.id.value >= p.nextId && !isDerivedId(e.id.value)) return makeError(ErrorCode::InvalidArgument, "event id was not reserved");
    const std::size_t bi = std::min<std::size_t>(dst.beats.size() - 1, static_cast<std::size_t>(e.start / beatT));
    dst.beats[bi].events.push_back(std::move(e));
  }
  for (Beat& b : dst.beats) b.userOwned = true;
  dst.userOwned = true;
  dst.copyOf = -1;
  Status v = validate(q);
  if (!v.ok()) return v.error();
  return q;
}

Result<Pattern> duplicateBar(const Pattern& p, int fromBar, int toBar) {
  const int n = static_cast<int>(p.bars.size());
  if (fromBar < 0 || fromBar >= n || toBar < 0 || toBar >= n || fromBar == toBar) return makeError(ErrorCode::OutOfRange, "invalid bar indices");
  if (p.phraseLocked || p.bars[static_cast<std::size_t>(toBar)].locked) return blocked("the destination bar");
  for (const Beat& bt : p.bars[static_cast<std::size_t>(toBar)].beats) {
    if (bt.locked) return blocked("a beat in the destination bar");
    for (const Event& e : bt.events)
      if (e.locked) return blocked("an event in the destination bar");
  }
  Pattern q = p;
  Bar& dst = q.bars[static_cast<std::size_t>(toBar)];
  const Bar& src = p.bars[static_cast<std::size_t>(fromBar)];
  std::size_t count = eventCount(q);
  for (const Beat& bt : dst.beats) count -= bt.events.size();
  std::size_t add = 0;
  for (const Beat& bt : src.beats) add += bt.events.size();
  if (count + add > static_cast<std::size_t>(p.settings.maxEvents)) return makeError(ErrorCode::LimitExceeded, "event cap reached", p.settings.maxEvents);
  for (std::size_t b = 0; b < dst.beats.size(); ++b) {
    dst.beats[b].events.clear();
    dst.beats[b].locked = false;
    dst.beats[b].userOwned = true;
    for (Event e : src.beats[b].events) {
      e.id = EventId{q.nextId++};
      e.locked = false;
      e.userOwned = true;
      e.child.reset();
      e.childActive = true;
      dst.beats[b].events.push_back(std::move(e));
    }
  }
  dst.copyOf = toBar > fromBar ? fromBar : -1;
  dst.userOwned = true;
  return q;
}

Result<Pattern> restoreSourceOrder(const Pattern& p, const ChopSnapshot& chops) {
  if (chops.chops.empty()) return makeError(ErrorCode::InvalidArgument, "there are no enabled chops");
  Pattern q = clearLocks(p);
  const Ticks barT = barTicks(q.settings);
  const Ticks g = gridTicks(q.settings.grid);
  const std::size_t slots = static_cast<std::size_t>(barT / g);
  const std::size_t n = chops.chops.size();
  const std::size_t perBar = std::min<std::size_t>(n, static_cast<std::size_t>(q.settings.maxEvents) / static_cast<std::size_t>(q.settings.bars));
  for (std::size_t b = 0; b < q.bars.size(); ++b) {
    for (Beat& bt : q.bars[b].beats) {
      bt.events.clear();
      bt.userOwned = false;
    }
    q.bars[b].copyOf = -1;
    q.bars[b].userOwned = false;
    for (std::size_t i = 0; i < perBar; ++i) {
      Event e;
      e.id = EventId{q.nextId++};
      e.chop = chops.chops[i].id;
      e.start = static_cast<Ticks>(i * slots / perBar) * g;
      e.duration = 1;
      q.bars[b].beats[static_cast<std::size_t>(beatOf(q, e.start))].events.push_back(e);
    }
    detail::assignDurations(q, static_cast<int>(b), nullptr);
  }
  return q;
}

Result<Pattern> setChild(const Pattern& p, EventId id, std::shared_ptr<const NestedPattern> child, const ChopSnapshot& chops) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  if (!child) return makeError(ErrorCode::InvalidArgument, "child pattern is missing");
  if (locationLocked(p, *loc)) return blocked("this event");
  Pattern q = p;
  Event& e = at(q, *loc);
  if (child->windowDuration != e.duration) return makeError(ErrorCode::InvalidArgument, "child window must equal the parent event duration");
  e.child = std::move(child);
  e.childActive = true;
  e.userOwned = true;
  Status v = validate(q);
  if (!v.ok()) return v.error();
  auto flat = flatten(q, chops);  // verifies bounds, depth, and the event cap without truncating
  if (!flat.ok()) return flat.error();
  return q;
}

Result<Pattern> collapse(const Pattern& p, EventId id) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  if (locationLocked(p, *loc)) return blocked("this event");
  Pattern q = p;
  Event& e = at(q, *loc);
  e.child.reset();
  e.childActive = true;
  return q;
}

Result<Pattern> setChildActive(const Pattern& p, EventId id, bool active) {
  auto loc = locate(p, id);
  if (!loc) return makeError(ErrorCode::NotFound, "event not found");
  Pattern q = p;
  Event& e = at(q, *loc);
  if (!e.child) return makeError(ErrorCode::InvalidArgument, "the event has no child pattern");
  e.childActive = active;  // a view toggle: allowed even when locked
  return q;
}

namespace {
// True when every descendant still references an existing chop and stays inside the source bounds that
// flatten() will enforce (its ancestor's resolved range, unless it explicitly overrides the source).
bool subtreeValid(const Event& parent, SampleRange requested, const ChopSnapshot& chops) {
  if (!parent.child) return true;
  for (const Event& c : parent.child->events) {
    const ChopInfo* ci = chops.find(c.chop);
    if (!ci) return false;
    const SampleRange req = c.region.empty() ? ci->range : c.region;
    const SampleRange bound = c.sourceOverride ? ci->range : requested;
    if (req.empty() || !bound.contains(req)) return false;
    if (!subtreeValid(c, req, chops)) return false;
  }
  return true;
}
}  // namespace

Pattern sanitize(const Pattern& p, const ChopSnapshot& chops) {
  Pattern q = p;
  for (Bar& b : q.bars)
    for (Beat& bt : b.beats) {
      std::vector<Event> kept;
      for (Event e : bt.events) {
        const ChopInfo* c = chops.find(e.chop);
        if (!c) continue;
        if (!e.region.empty() && !c->range.contains(e.region)) {
          e.region = {};
          e.child.reset();
          e.childActive = true;
        }
        if (e.child && !subtreeValid(e, e.region.empty() ? c->range : e.region, chops)) {
          e.child.reset();  // the zoomed content no longer fits its chops: the hit plays on its own
          e.childActive = true;
        }
        kept.push_back(std::move(e));
      }
      bt.events = std::move(kept);
    }
  return q;
}

}  // namespace chopfractal::pattern
