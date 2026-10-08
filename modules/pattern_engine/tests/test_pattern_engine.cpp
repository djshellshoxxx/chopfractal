#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>
#include <chopfractal/pattern_engine/session.hpp>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::pattern;

namespace {

ChopSnapshot makeChops(int n = 8) {
  ChopSnapshot s;
  s.source = SourceId{42};
  s.sourceFrames = 12000 * n;
  s.sampleRate = 48000;
  for (int i = 0; i < n; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 12000ll, (i + 1) * 12000ll}, i < 4 ? 1u : 0u, 0, 0});
  return s;
}

Settings baseSettings() {
  Settings s;
  s.bars = 4;
  s.density = 0.7;
  s.seed = 1;
  return s;
}

Pattern gen(const ChopSnapshot& c, const Settings& s, const ICandidatePolicy* pol = nullptr, GenerationReport* rep = nullptr) {
  auto r = generate(c, s, pol, rep);
  CHECK(r.ok());
  return r.ok() ? r.value() : Pattern{};
}

std::vector<std::uint8_t> barBytes(const Pattern& p, std::size_t bar) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  for (const Beat& bt : p.bars[bar].beats)
    for (const Event& e : bt.events) writeEvent(w, e);
  return out;
}

const Event* firstEventOf(const Pattern& p, int bar) {
  for (const Beat& bt : p.bars[static_cast<std::size_t>(bar)].beats)
    if (!bt.events.empty()) return &bt.events.front();
  return nullptr;
}

struct TestPolicy : ICandidatePolicy {
  ChopId forbidden;
  ChopId requiredChop;
  bool preserveFirst = false;
  Decision evaluate(const CandidateQuery& q) const override {
    Decision d;
    if (forbidden.valid() && q.chop == forbidden) {
      d.allowed = false;
      d.blockedBy = 7;
      d.trace.push_back({7, TraceEntry::Effect::Rejected});
    }
    return d;
  }
  bool isPreserved(const CandidateQuery& q) const override { return preserveFirst && q.firstInBar; }
  std::vector<Requirement> required(const ScopeContext& sc, const std::vector<ChopId>&) const override {
    if (requiredChop.valid() && sc.finalBeat()) return {{requiredChop, 9}};
    return {};
  }
};

}  // namespace

CHOP_TEST(identical_inputs_reproduce_identical_patterns) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.variation = {0.6, 0.6, 0.6, 0.8};
  s.allowReverse = s.allowPitch = s.allowRetrigger = true;
  s.maxShiftTicks = 20;
  s.swing = 0.4;
  const Pattern a = gen(c, s);
  const Pattern b = gen(c, s);
  CHECK(serialize(a) == serialize(b));
  Settings other = s;
  other.seed = 2;
  CHECK(serialize(gen(c, other)) != serialize(a));
  CHECK(eventCount(a) > 0);
}

CHOP_TEST(caps_density_and_boundaries_hold_across_many_configurations) {
  const ChopSnapshot c = makeChops();
  const int barsOpts[] = {1, 2, 4, 8};
  const double densities[] = {0.0, 0.3, 0.9, 1.0};
  const int caps[] = {512, 40};
  for (int bars : barsOpts)
    for (double d : densities)
      for (int cap : caps)
        for (std::uint64_t seed = 1; seed <= 6; ++seed) {
          Settings s = baseSettings();
          s.bars = bars;
          s.density = d;
          s.maxEvents = cap;
          s.seed = seed;
          s.grid = {32, false};
          s.variation = {1, 1, 1, 1};
          s.maxShiftTicks = 15;
          s.allowRetrigger = s.allowPitch = s.allowReverse = true;
          auto r = generate(c, s);
          CHECK(r.ok());
          if (!r.ok()) continue;
          CHECK(validate(r.value()).ok());
          CHECK(static_cast<int>(eventCount(r.value())) <= cap);
          if (d == 0.0) CHECK_EQ(eventCount(r.value()), 0u);
          auto flat = flatten(r.value(), c);
          CHECK(flat.ok());
          if (flat.ok()) {
            CHECK(static_cast<int>(flat.value().size()) <= cap);
            for (const FlatEvent& e : flat.value()) {
              CHECK(e.duration > 0);
              CHECK(e.start >= 0 && e.start + e.duration <= lengthTicks(r.value()));
            }
          }
        }
}

CHOP_TEST(full_density_fills_every_slot_and_swing_moves_only_odd_off_grid_slots) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.bars = 1;
  s.density = 1.0;
  s.variation = {0, 0, 0, 0};
  s.grid = {8, false};
  s.swing = 1.0;
  Pattern p = gen(c, s);
  CHECK_EQ(eventCount(p), 8u);
  const Ticks g = gridTicks(s.grid);
  int swung = 0;
  for (const Beat& bt : p.bars[0].beats)
    for (const Event& e : bt.events) {
      const Ticks r = e.start % g;
      CHECK(r == 0 || r == g / 3);
      if (r == g / 3) ++swung;
    }
  CHECK_EQ(swung, 4);
  s.swing = 0.0;
  p = gen(c, s);
  for (const Beat& bt : p.bars[0].beats)
    for (const Event& e : bt.events) CHECK_EQ(e.start % g, 0);
  s.grid = {8, true};  // triplet grids are never swung
  s.swing = 1.0;
  p = gen(c, s);
  for (const Beat& bt : p.bars[0].beats)
    for (const Event& e : bt.events) CHECK_EQ(e.start % gridTicks(s.grid), 0);
}

CHOP_TEST(other_time_signatures_and_grids_are_valid) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.timeSignature = {7, 8};
  s.grid = {16, true};
  auto r = generate(c, s);
  CHECK(r.ok() && validate(r.value()).ok());
  s.timeSignature = {3, 4};
  s.grid = {1, false};  // 1/1 is longer than a 3/4 bar
  CHECK(!generate(c, s).ok());
}

CHOP_TEST(settings_validation_rejects_bad_values) {
  const ChopSnapshot c = makeChops();
  auto bad = [&](auto mutateFn) {
    Settings s = baseSettings();
    mutateFn(s);
    CHECK(!generate(c, s).ok());
  };
  bad([](Settings& s) { s.bars = 3; });
  bad([](Settings& s) { s.density = 1.5; });
  bad([](Settings& s) { s.density = std::nan(""); });
  bad([](Settings& s) { s.swing = -0.1; });
  bad([](Settings& s) { s.maxEvents = 100000; });
  bad([](Settings& s) { s.maxEvents = 2; });  // fewer events than bars
  bad([](Settings& s) { s.grid = {7, false}; });
  bad([](Settings& s) { s.maxShiftTicks = 100000; });
  bad([](Settings& s) { s.pitchRange = 99; });
  bad([](Settings& s) { s.maxRetrigger = 1; });
  CHECK(!generate(ChopSnapshot{}, baseSettings()).ok());
}

CHOP_TEST(mutation_preserves_locked_scopes_byte_for_byte) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.density = 0.8;
  s.variation = {1, 1, 1, 1};
  Pattern p = gen(c, s);
  p = setLock(p, {ScopeLevel::Bar, 1, 0, {}}, true).value();
  const Event* locked = nullptr;
  for (const Beat& bt : p.bars[2].beats)
    if (!bt.events.empty()) {
      locked = &bt.events.front();
      break;
    }
  CHECK(locked != nullptr);
  if (!locked) return;
  const EventId lockedId = locked->id;
  p = setLock(p, {ScopeLevel::Event, 0, 0, lockedId}, true).value();
  const Event lockedCopy = *findEvent(p, lockedId);  // captured after locking so the flag is part of the comparison
  p = setLock(p, {ScopeLevel::Beat, 3, 1, {}}, true).value();
  const auto bar1 = barBytes(p, 1);
  MutateOptions o;
  o.seed = 99;
  o.amount = 1.0;
  auto m = mutate(p, c, o);
  CHECK(m.ok());
  if (!m.ok()) return;
  CHECK(barBytes(m.value(), 1) == bar1);
  const Event* after = findEvent(m.value(), lockedCopy.id);
  CHECK(after != nullptr);
  if (after) {
    std::vector<std::uint8_t> a, b;
    bytes::Writer wa(a), wb(b);
    writeEvent(wa, lockedCopy);
    writeEvent(wb, *after);
    CHECK(a == b);
  }
  // The locked beat in bar 3 is unchanged; other content did change.
  std::vector<std::uint8_t> before31, after31;
  bytes::Writer w1(before31), w2(after31);
  for (const Event& e : p.bars[3].beats[1].events) writeEvent(w1, e);
  for (const Event& e : m.value().bars[3].beats[1].events) writeEvent(w2, e);
  CHECK(before31 == after31);
  CHECK(barBytes(m.value(), 0) != barBytes(p, 0));
  CHECK_EQ(m.value().settings.seed, 99u);
  CHECK(validate(m.value()).ok());
}

CHOP_TEST(phrase_lock_and_zero_amount_leave_content_alone) {
  const ChopSnapshot c = makeChops();
  Pattern p = gen(c, baseSettings());
  Pattern locked = setLock(p, {ScopeLevel::Phrase, 0, 0, {}}, true).value();
  MutateOptions o;
  o.seed = 5;
  o.amount = 1.0;
  GenerationReport rep;
  auto m = mutate(locked, c, o, nullptr, &rep);
  CHECK(m.ok() && serialize(m.value()) == serialize(locked));  // not even the seed changes
  CHECK(!rep.notes.empty());
  o.amount = 0.0;
  auto z = mutate(p, c, o);
  CHECK(z.ok());
  if (z.ok())
    for (std::size_t b = 0; b < p.bars.size(); ++b) CHECK(barBytes(z.value(), b) == barBytes(p, b));
  o.amount = 2.0;
  CHECK(!mutate(p, c, o).ok());
}

CHOP_TEST(mutation_is_deterministic_and_respects_the_event_cap) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.maxEvents = 60;
  s.grid = {32, false};
  s.density = 1.0;
  Pattern p = gen(c, s);
  MutateOptions o;
  o.seed = 7;
  o.amount = 0.6;
  auto a = mutate(p, c, o);
  auto b = mutate(p, c, o);
  CHECK(a.ok() && b.ok());
  if (a.ok() && b.ok()) {
    CHECK(serialize(a.value()) == serialize(b.value()));
    CHECK(static_cast<int>(eventCount(a.value())) <= 60);
    CHECK(validate(a.value()).ok());
  }
  for (std::uint64_t seed = 1; seed < 20; ++seed) {
    o.seed = seed;
    auto m = mutate(p, c, o);
    CHECK(m.ok());
    if (m.ok()) CHECK(static_cast<int>(eventCount(m.value())) <= 60);
  }
}

CHOP_TEST(edited_events_survive_mutation_unless_requested) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.density = 0.3;
  Pattern p = gen(c, s);
  // Find a free spot in bar 0 and add a manual event.
  Ticks freeAt = -1;
  for (Ticks t = 120; t < 3840 && freeAt < 0; t += 120) {
    bool taken = false;
    for (const Beat& bt : p.bars[0].beats)
      for (const Event& e : bt.events)
        if (e.start <= t + 60 && t < e.start + e.duration + 60) taken = true;
    if (!taken) freeAt = t;
  }
  CHECK(freeAt >= 0);
  auto added = addEvent(p, 0, ChopId{3}, freeAt, 60, c);
  CHECK(added.ok());
  if (!added.ok()) return;
  p = added.value();
  const EventId manual{p.nextId - 1};
  CHECK(findEvent(p, manual) != nullptr && findEvent(p, manual)->userOwned);
  MutateOptions o;
  o.seed = 11;
  o.amount = 1.0;
  auto kept = mutate(p, c, o);
  CHECK(kept.ok() && findEvent(kept.value(), manual) != nullptr);
  o.includeEdited = true;
  auto replaced = mutate(p, c, o);
  CHECK(replaced.ok() && findEvent(replaced.value(), manual) == nullptr);
}

CHOP_TEST(policy_forbid_require_preserve_and_conflict_reporting) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.density = 0.8;
  s.variation = {0.5, 0.5, 0.5, 1.0};

  TestPolicy forbid;
  forbid.forbidden = ChopId{3};
  for (std::uint64_t seed = 1; seed <= 12; ++seed) {
    s.seed = seed;
    Pattern p = gen(c, s, &forbid);
    for (const Bar& b : p.bars)
      for (const Beat& bt : b.beats)
        for (const Event& e : bt.events) CHECK(e.chop != ChopId{3});
  }

  TestPolicy require;
  require.requiredChop = ChopId{6};
  s.seed = 3;
  GenerationReport rep;
  Pattern p = gen(c, s, &require, &rep);
  int checked = 0;
  for (const Bar& b : p.bars) {
    const Beat& last = b.beats.back();
    if (last.events.empty()) continue;
    bool has = false;
    for (const Event& e : last.events) has = has || e.chop == ChopId{6};
    CHECK(has);
    ++checked;
  }
  CHECK(checked > 0);

  TestPolicy conflict;
  conflict.forbidden = ChopId{6};
  conflict.requiredChop = ChopId{6};
  GenerationReport rep2;
  Pattern q = gen(c, s, &conflict, &rep2);
  bool sawBlocked = false;
  for (const Note& n : rep2.notes) sawBlocked = sawBlocked || (n.kind == Note::Kind::BlockedByRule && n.rule == 7);
  CHECK(sawBlocked);
  for (const Bar& b : q.bars)
    for (const Beat& bt : b.beats)
      for (const Event& e : bt.events) CHECK(e.chop != ChopId{6});

  TestPolicy keep;
  keep.preserveFirst = true;
  Pattern base = gen(c, s, &keep);
  std::set<std::uint64_t> firsts;
  for (int bar = 0; bar < s.bars; ++bar)
    if (const Event* e = firstEventOf(base, bar)) firsts.insert(e->id.value);
  MutateOptions o;
  o.seed = 55;
  o.amount = 1.0;
  auto m = mutate(base, c, o, &keep);
  CHECK(m.ok());
  if (m.ok())
    for (std::uint64_t id : firsts) CHECK(findEvent(m.value(), EventId{id}) != nullptr);
}

CHOP_TEST(edit_commands_validate_and_respect_locks) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.bars = 2;
  Pattern p = gen(c, s);
  const Event* e0 = firstEventOf(p, 0);
  CHECK(e0 != nullptr);
  if (!e0) return;
  const EventId id = e0->id;

  EventTransform tx;
  tx.level = 2.0f;
  auto t = setEventTransform(p, id, tx);
  CHECK(t.ok() && findEvent(t.value(), id)->tx.level == 2.0f && findEvent(t.value(), id)->userOwned);
  tx.level = 99.f;
  CHECK(!setEventTransform(p, id, tx).ok());
  tx.level = 1.f;
  tx.pan = 2.f;
  CHECK(!setEventTransform(p, id, tx).ok());

  auto ch = setEventChop(p, id, ChopId{8}, c);
  CHECK(ch.ok() && findEvent(ch.value(), id)->chop == ChopId{8});
  CHECK(!setEventChop(p, id, ChopId{999}, c).ok());

  auto moved = moveEvent(p, id, 1, 0);
  CHECK(moved.ok() && locate(moved.value(), id)->bar == 1);
  CHECK(!moveEvent(p, id, 5, 0).ok());
  CHECK(!moveEvent(p, id, 0, -1).ok());

  auto del = deleteEvent(p, id);
  CHECK(del.ok() && findEvent(del.value(), id) == nullptr);
  CHECK(!deleteEvent(p, EventId{424242}).ok());

  CHECK(!addEvent(p, 0, ChopId{1}, 3800, 100, c).ok());   // past the bar end
  CHECK(!addEvent(p, 0, ChopId{99}, 0, 100, c).ok());     // unknown chop
  CHECK(!addEvent(p, 7, ChopId{1}, 0, 100, c).ok());      // bad bar

  // Locks refuse structural edits at every level, and unlocking restores them.
  const Location loc = *locate(p, id);
  Pattern lockedBeat = setLock(p, {ScopeLevel::Beat, loc.bar, loc.beat, {}}, true).value();
  CHECK(!deleteEvent(lockedBeat, id).ok());
  CHECK(deleteEvent(lockedBeat, id).error().code == ErrorCode::Blocked);
  CHECK(deleteEvent(setLock(lockedBeat, {ScopeLevel::Beat, loc.bar, loc.beat, {}}, false).value(), id).ok());
  Pattern lockedEvent = setLock(p, {ScopeLevel::Event, 0, 0, id}, true).value();
  CHECK(isLocked(lockedEvent, id) && !setEventChop(lockedEvent, id, ChopId{2}, c).ok());
  Pattern lockedPhrase = setLock(p, {ScopeLevel::Phrase, 0, 0, {}}, true).value();
  CHECK(!moveEvent(lockedPhrase, id, 1, 0).ok());
  CHECK(!clearLocks(lockedPhrase).phraseLocked);
  CHECK(!setLock(p, {ScopeLevel::Bar, 9, 0, {}}, true).ok());
}

CHOP_TEST(event_cap_applies_to_manual_edits) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.bars = 1;
  s.maxEvents = 4;
  s.density = 1.0;
  Pattern p = gen(c, s);
  CHECK(static_cast<int>(eventCount(p)) <= 4);
  Pattern cur = p;
  bool hitLimit = false;
  for (Ticks t = 0; t < 3600 && !hitLimit; t += 240) {
    auto r = addEvent(cur, 0, ChopId{1}, t, 60, c);
    if (!r.ok()) {
      CHECK(r.error().code == ErrorCode::LimitExceeded);
      hitLimit = true;
    } else {
      cur = r.value();
    }
  }
  CHECK(hitLimit);
}

CHOP_TEST(duplicate_bar_and_restore_source_order) {
  const ChopSnapshot c = makeChops();
  Pattern p = gen(c, baseSettings());
  auto d = duplicateBar(p, 0, 2);
  CHECK(d.ok());
  if (d.ok()) {
    CHECK_EQ(d.value().bars[2].copyOf, 0);
    std::size_t n0 = 0, n2 = 0;
    for (const Beat& b : d.value().bars[0].beats) n0 += b.events.size();
    for (const Beat& b : d.value().bars[2].beats) n2 += b.events.size();
    CHECK_EQ(n0, n2);
    CHECK(validate(d.value()).ok());
  }
  CHECK(!duplicateBar(p, 1, 1).ok());
  Pattern locked = setLock(p, {ScopeLevel::Bar, 2, 0, {}}, true).value();
  CHECK(!duplicateBar(locked, 0, 2).ok());

  Pattern withLocks = setLock(p, {ScopeLevel::Phrase, 0, 0, {}}, true).value();
  auto r = restoreSourceOrder(withLocks, c);
  CHECK(r.ok());
  if (r.ok()) {
    CHECK(!r.value().phraseLocked);
    const Bar& b = r.value().bars[0];
    std::vector<Event> evs;
    for (const Beat& bt : b.beats) evs.insert(evs.end(), bt.events.begin(), bt.events.end());
    CHECK_EQ(evs.size(), c.chops.size());
    std::sort(evs.begin(), evs.end(), [](const Event& x, const Event& y) { return x.start < y.start; });
    for (std::size_t i = 0; i < evs.size(); ++i) CHECK(evs[i].chop == c.chops[i].id);
    CHECK(validate(r.value()).ok());
  }
}

CHOP_TEST(nested_child_patterns_attach_collapse_and_obey_limits) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.bars = 1;
  s.density = 1.0;
  s.variation = {0, 0, 0, 0};
  Pattern p = gen(c, s);
  const Event* parent = firstEventOf(p, 0);
  CHECK(parent != nullptr);
  if (!parent) return;
  const Event parentCopy = *parent;
  const std::size_t before = flatten(p, c).value().size();

  auto child = std::make_shared<NestedPattern>();
  child->windowDuration = parentCopy.duration;
  const ChopInfo* info = c.find(parentCopy.chop);
  const Ticks half = parentCopy.duration / 2;
  Event a, b;
  a.id = EventId{1000001};
  a.chop = parentCopy.chop;
  a.start = 0;
  a.duration = half;
  a.region = {info->range.start, info->range.start + 100};
  b = a;
  b.id = EventId{1000002};
  b.start = half;
  b.duration = parentCopy.duration - half;
  child->events = {a, b};
  p.nextId = std::max<std::uint64_t>(p.nextId, 1000003);

  auto with = setChild(p, parentCopy.id, child, c);
  CHECK(with.ok());
  if (with.ok()) {
    CHECK_EQ(flatten(with.value(), c).value().size(), before + 1);  // parent replaced by two children
    auto off = setChildActive(with.value(), parentCopy.id, false);
    CHECK(off.ok() && flatten(off.value(), c).value().size() == before);
    auto gone = collapse(with.value(), parentCopy.id);
    CHECK(gone.ok() && flatten(gone.value(), c).value().size() == before);
    const auto withBlob = serialize(with.value());
    auto reloaded = deserialize(withBlob.data(), withBlob.size());
    CHECK(reloaded.ok() && serialize(reloaded.value()) == withBlob);  // the child tree survives a state round trip
    CHECK(!setChild(setLock(p, {ScopeLevel::Bar, 0, 0, {}}, true).value(), parentCopy.id, child, c).ok());
  }

  auto wrongWindow = std::make_shared<NestedPattern>(*child);
  wrongWindow->windowDuration = parentCopy.duration + 1;
  CHECK(!setChild(p, parentCopy.id, wrongWindow, c).ok());

  auto outside = std::make_shared<NestedPattern>(*child);
  outside->events[0].region = {info->range.end, info->range.end + 50};  // beyond the parent's chop
  CHECK(!setChild(p, parentCopy.id, outside, c).ok());

  // Event-cap enforcement: replacing one parent with three children must not exceed a full cap.
  Settings tight = s;
  tight.maxEvents = 8;
  tight.grid = {8, false};
  Pattern full = gen(c, tight);
  CHECK_EQ(eventCount(full), 8u);
  const Event* fp = firstEventOf(full, 0);
  auto three = std::make_shared<NestedPattern>();
  three->windowDuration = fp->duration;
  for (int i = 0; i < 3; ++i) {
    Event e;
    e.id = EventId{full.nextId + 10 + static_cast<std::uint64_t>(i)};
    e.chop = fp->chop;
    e.start = fp->duration / 3 * i;
    e.duration = fp->duration / 3;
    three->events.push_back(e);
  }
  full.nextId += 20;
  auto over = setChild(full, fp->id, three, c);
  CHECK(!over.ok() && over.error().code == ErrorCode::LimitExceeded);
}

CHOP_TEST(depth_limit_is_enforced_by_flatten) {
  const ChopSnapshot c = makeChops();
  Event leaf;
  leaf.id = EventId{50};
  leaf.chop = ChopId{1};
  leaf.start = 0;
  leaf.duration = 960;
  Event e = leaf;
  for (int level = 0; level < 4; ++level) {
    auto n = std::make_shared<NestedPattern>();
    n->windowDuration = 960;
    n->depth = static_cast<std::uint8_t>(level + 1);
    n->events = {e};
    n->events[0].id = EventId{100 + static_cast<std::uint64_t>(level)};
    Event wrapper = leaf;
    wrapper.id = EventId{200 + static_cast<std::uint64_t>(level)};
    wrapper.child = n;
    e = wrapper;
  }
  FlatEventList out;
  Status s = flattenInto(out, {e}, 0, 3840, {}, c, {}, 1);
  CHECK(!s.ok() && s.error().code == ErrorCode::LimitExceeded);
  CHECK(!validateEvent(e).ok());
}

CHOP_TEST(session_undo_redo_restores_exact_pattern_and_seed) {
  const ChopSnapshot c = makeChops();
  PatternSession session;
  CHECK(!session.hasPattern());
  CHECK(!session.commit(makeError(ErrorCode::Corrupt, "x")).ok());
  CHECK(!session.hasPattern());
  Settings s = baseSettings();
  s.seed = 10;
  CHECK(session.commit(generate(c, s)).ok());
  const auto first = serialize(session.current());
  MutateOptions o;
  o.seed = 20;
  o.amount = 1.0;
  CHECK(session.commit(mutate(session.current(), c, o)).ok());
  CHECK_EQ(session.current().settings.seed, 20u);
  CHECK(session.undo());
  CHECK(serialize(session.current()) == first);
  CHECK_EQ(session.current().settings.seed, 10u);
  CHECK(session.redo());
  CHECK_EQ(session.current().settings.seed, 20u);
  // A/B snapshots.
  CHECK(session.storeSnapshot(0).ok());
  CHECK(session.undo());
  CHECK(session.storeSnapshot(1).ok());
  CHECK(session.recallSnapshot(0).ok());
  CHECK_EQ(session.current().settings.seed, 20u);
  CHECK(session.recallSnapshot(1).ok());
  CHECK_EQ(session.current().settings.seed, 10u);
  CHECK(!session.storeSnapshot(2).ok());
  session.reset();
  CHECK(!session.hasPattern() && !session.hasSnapshot(0));
}

CHOP_TEST(state_roundtrip_and_corruption_never_crash_or_slip_through) {
  const ChopSnapshot c = makeChops();
  Settings s = baseSettings();
  s.variation = {0.7, 0.7, 0.7, 0.9};
  s.allowReverse = s.allowPitch = s.allowRetrigger = true;
  Pattern p = gen(c, s);
  p = setLock(p, {ScopeLevel::Bar, 1, 0, {}}, true).value();
  const auto blob = serialize(p);
  auto back = deserialize(blob.data(), blob.size());
  CHECK(back.ok());
  if (back.ok()) {
    CHECK(serialize(back.value()) == blob);
    CHECK(back.value().bars[1].locked);
  }
  for (std::size_t cut = 0; cut < blob.size(); ++cut) CHECK(!deserialize(blob.data(), cut).ok());
  for (std::size_t i = 0; i < blob.size(); ++i) {
    auto bad = blob;
    bad[i] ^= 0xA5;
    auto r = deserialize(bad.data(), bad.size());
    if (r.ok()) CHECK(validate(r.value()).ok());  // a flip that survives must still be a valid pattern
  }
  auto newer = blob;
  newer[0] = 99;  // engine version byte
  auto r = deserialize(newer.data(), newer.size());
  CHECK(!r.ok() && r.error().code == ErrorCode::UnsupportedVersion);
  // Mutating a pattern from another engine version is refused rather than silently rewritten.
  Pattern old = p;
  old.engineVersion = 0;
  MutateOptions o;
  CHECK(!mutate(old, c, o).ok());
}

CHOP_TEST(sanitize_drops_vanished_chops_and_stray_regions) {
  const ChopSnapshot c = makeChops();
  Pattern p = gen(c, baseSettings());
  ChopSnapshot fewer = makeChops(4);
  Pattern q = sanitize(p, fewer);
  CHECK(validate(q).ok());
  CHECK(flatten(q, fewer).ok());
  CHECK(eventCount(q) <= eventCount(p));
  for (const Bar& b : q.bars)
    for (const Beat& bt : b.beats)
      for (const Event& e : bt.events) CHECK(fewer.find(e.chop) != nullptr);
}
