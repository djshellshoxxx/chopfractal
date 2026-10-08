#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/policy.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/chop_contracts/time.hpp>
#include <chopfractal/chop_contracts/undo.hpp>
#include <unordered_set>

#include "chop_test.hpp"

using namespace chopfractal;

namespace {
ChopSnapshot makeChops() {
  ChopSnapshot s;
  s.source = SourceId{1};
  s.sourceFrames = 4000;
  s.sampleRate = 48000;
  s.chops.push_back({ChopId{1}, {0, 1000}, 0, 0, 0});
  s.chops.push_back({ChopId{2}, {1000, 2000}, 0, 0, 0});
  return s;
}
Event makeEvent(std::uint64_t id, std::uint64_t chop, Ticks start, Ticks dur) {
  Event e;
  e.id = EventId{id};
  e.chop = ChopId{chop};
  e.start = start;
  e.duration = dur;
  return e;
}
}  // namespace

CHOP_TEST(time_grid_values_are_exact_integers) {
  CHECK_EQ(gridTicks({4, false}), 960);
  CHECK_EQ(gridTicks({16, false}), 240);
  CHECK_EQ(gridTicks({32, false}), 120);
  CHECK_EQ(gridTicks({4, true}), 640);
  CHECK_EQ(gridTicks({32, true}), 80);
  CHECK_EQ(gridTicks({1, true}), 2560);
  CHECK_EQ(ticksPerBar(TimeSignature{4, 4}), 3840);
  CHECK_EQ(ticksPerBar(TimeSignature{7, 8}), 3360);
  CHECK(!(TimeSignature{0, 4}.valid()));
  CHECK(!(TimeSignature{4, 3}.valid()));
  CHECK(!(Grid{3, false}.valid()));
}

CHOP_TEST(rng_is_deterministic_and_streams_are_independent) {
  Rng a(42), b(42), c(43);
  for (int i = 0; i < 100; ++i) CHECK_EQ(a.next(), b.next());
  CHECK(Rng(42).next() != c.next());
  // Golden values pin the generator across platforms and compilers (SplitMix64).
  Rng g(0);
  CHECK_EQ(g.next(), 0xE220A8397B1DCDAFull);
  CHECK_EQ(g.next(), 0x6E789E6AA1B965F4ull);
  CHECK(deriveKey(1, 2, 3) != deriveKey(1, 2, 4));
  Rng r(7);
  for (int i = 0; i < 1000; ++i) {
    const double u = r.uniform01();
    CHECK(u >= 0.0 && u < 1.0);
    CHECK(r.uniform(5) < 5);
  }
  Rng w(9);
  CHECK_EQ(w.weightedPick({0.0, 0.0, 5.0}), 2u);
}

CHOP_TEST(bytes_roundtrip_and_malformed_input_is_rejected) {
  std::vector<std::uint8_t> buf;
  bytes::Writer w(buf);
  w.u8(7);
  w.u16(65000);
  w.u32(123456789u);
  w.u64(0x0123456789ABCDEFull);
  w.i64(-5);
  w.f32(1.5f);
  w.str("hello");
  bytes::Reader r(buf.data(), buf.size());
  CHECK_EQ(r.u8(), 7);
  CHECK_EQ(r.u16(), 65000);
  CHECK_EQ(r.u32(), 123456789u);
  CHECK_EQ(r.u64(), 0x0123456789ABCDEFull);
  CHECK_EQ(r.i64(), -5);
  CHECK_NEAR(r.f32(), 1.5, 0.0);
  CHECK(r.str(16) == "hello");
  CHECK(r.ok());
  CHECK_EQ(r.remaining(), 0u);
  r.u8();  // read past the end
  CHECK(!r.ok());

  // Hostile length prefix must fail without allocating.
  std::vector<std::uint8_t> evil;
  bytes::Writer ew(evil);
  ew.u32(0xFFFFFFFFu);
  bytes::Reader er(evil.data(), evil.size());
  CHECK(er.str(1024).empty());
  CHECK(!er.ok());
  bytes::Reader cr(evil.data(), evil.size());
  CHECK_EQ(cr.count(100, 4), 0u);
  CHECK(!cr.ok());

  // Non-finite floats are rejected.
  std::vector<std::uint8_t> nan;
  bytes::Writer nw(nan);
  nw.u32(0x7FC00000u);
  bytes::Reader nr(nan.data(), nan.size());
  nr.f32();
  CHECK(!nr.ok());
}

CHOP_TEST(undo_stack_semantics) {
  SnapshotStack<int> s(0, 3);
  s.commit(1);
  s.commit(2);
  CHECK_EQ(s.current(), 2);
  CHECK(s.undo());
  CHECK_EQ(s.current(), 1);
  s.commit(9);  // discards redo tail
  CHECK(!s.canRedo());
  s.commit(10);
  s.commit(11);  // capacity 3 drops oldest
  CHECK_EQ(s.current(), 11);
  CHECK(s.undo() && s.undo());
  CHECK(!s.undo());
  CHECK_EQ(s.current(), 9);
}

CHOP_TEST(flatten_positions_regions_and_sorting) {
  const ChopSnapshot chops = makeChops();
  std::vector<Event> events{makeEvent(2, 2, 960, 480), makeEvent(1, 1, 0, 960)};
  FlatEventList out;
  Status s = flattenInto(out, events, 3840, 3840, {}, chops, {}, 1);
  CHECK(s.ok());
  sortFlat(out);
  CHECK_EQ(out.size(), 2u);
  CHECK_EQ(out[0].start, 3840);
  CHECK_EQ(out[1].start, 3840 + 960);
  CHECK(out[1].region == (SampleRange{1000, 2000}));
}

CHOP_TEST(flatten_nested_child_replaces_parent_and_stays_in_bounds) {
  const ChopSnapshot chops = makeChops();
  Event parent = makeEvent(1, 1, 960, 960);
  auto child = std::make_shared<NestedPattern>();
  child->windowDuration = 960;
  Event c0 = makeEvent(10, 1, 0, 480);
  c0.region = {0, 500};
  Event c1 = makeEvent(11, 1, 480, 480);
  c1.region = {500, 1000};
  child->events = {c0, c1};
  parent.child = child;
  FlatEventList out;
  CHECK(flattenInto(out, {parent}, 0, 3840, {}, chops, {}, 1).ok());
  CHECK_EQ(out.size(), 2u);
  CHECK_EQ(out[0].start, 960);
  CHECK_EQ(out[1].start, 960 + 480);
  CHECK_EQ(out[0].depth, 1);

  // Parent fallback: deactivating the child plays the original single event.
  parent.childActive = false;
  out.clear();
  CHECK(flattenInto(out, {parent}, 0, 3840, {}, chops, {}, 1).ok());
  CHECK_EQ(out.size(), 1u);

  // A child reading outside its ancestor's range is an error unless explicitly overridden.
  parent.childActive = true;
  auto bad = std::make_shared<NestedPattern>(*child);
  bad->events[0].chop = ChopId{2};
  bad->events[0].region = {1000, 1500};
  parent.child = bad;
  out.clear();
  Status s = flattenInto(out, {parent}, 0, 3840, {}, chops, {}, 1);
  CHECK(!s.ok() && s.error().code == ErrorCode::OutOfRange);
  auto over = std::make_shared<NestedPattern>(*bad);
  over->events[0].sourceOverride = true;
  parent.child = over;
  out.clear();
  CHECK(flattenInto(out, {parent}, 0, 3840, {}, chops, {}, 1).ok());
}

CHOP_TEST(flatten_enforces_limits_and_rejects_malformed_events) {
  const ChopSnapshot chops = makeChops();
  FlattenLimits lim;
  lim.maxEvents = 2;
  std::vector<Event> three{makeEvent(1, 1, 0, 100), makeEvent(2, 1, 100, 100), makeEvent(3, 1, 200, 100)};
  FlatEventList out;
  Status s = flattenInto(out, three, 0, 3840, {}, chops, lim, 1);
  CHECK(!s.ok() && s.error().code == ErrorCode::LimitExceeded && s.error().hint == 2);

  out.clear();
  CHECK(!flattenInto(out, {makeEvent(1, 1, 3800, 100)}, 0, 3840, {}, chops, {}, 1).ok());  // past the window
  CHECK(!flattenInto(out, {makeEvent(1, 1, 0, 0)}, 0, 3840, {}, chops, {}, 1).ok());       // zero length
  CHECK(!flattenInto(out, {makeEvent(1, 99, 0, 10)}, 0, 3840, {}, chops, {}, 1).ok());     // unknown chop
  Event disabled = makeEvent(1, 1, 0, 10);
  disabled.enabled = false;
  out.clear();
  CHECK(flattenInto(out, {disabled}, 0, 3840, {}, chops, {}, 1).ok() && out.empty());
}

CHOP_TEST(probability_is_deterministic_per_seed) {
  const ChopSnapshot chops = makeChops();
  std::vector<Event> events;
  for (std::uint64_t i = 1; i <= 64; ++i) {
    Event e = makeEvent(i, 1, static_cast<Ticks>((i - 1) * 50), 50);
    e.probability = 0.5f;
    events.push_back(e);
  }
  FlatEventList a, b, c;
  CHECK(flattenInto(a, events, 0, 3840, {}, chops, {}, 5).ok());
  CHECK(flattenInto(b, events, 0, 3840, {}, chops, {}, 5).ok());
  CHECK(flattenInto(c, events, 0, 3840, {}, chops, {}, 6).ok());
  CHECK_EQ(a.size(), b.size());
  CHECK(a.size() > 10 && a.size() < 54);
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i].id == b[i].id);
  bool differs = a.size() != c.size();
  for (std::size_t i = 0; !differs && i < a.size(); ++i) differs = !(a[i].id == c[i].id);
  CHECK(differs);  // a different seed selects a different subset
}

CHOP_TEST(event_codec_roundtrips_nested_trees_and_rejects_truncation) {
  Event parent = makeEvent(1, 1, 960, 960);
  parent.tx.pitchSemitones = 3.f;
  parent.tx.reverse = true;
  parent.locked = true;
  auto child = std::make_shared<NestedPattern>();
  child->seed = 77;
  child->windowDuration = 960;
  child->events = {makeEvent(10, 1, 0, 480), makeEvent(11, 2, 480, 480)};
  parent.child = child;
  CHECK(validateEvent(parent).ok());

  std::vector<std::uint8_t> buf;
  bytes::Writer w(buf);
  writeEvent(w, parent);
  bytes::Reader r(buf.data(), buf.size());
  Event back;
  CHECK(readEvent(r, back));
  CHECK(back.id == parent.id && back.locked && back.tx.reverse && back.tx.pitchSemitones == 3.f);
  CHECK(back.child && back.child->seed == 77 && back.child->events.size() == 2);
  if (back.child && back.child->events.size() == 2) CHECK(back.child->events[1].chop == ChopId{2});

  for (std::size_t cut = 0; cut < buf.size(); cut += 7) {
    bytes::Reader tr(buf.data(), cut);
    Event tmp;
    CHECK(!readEvent(tr, tmp));
  }
  Event bad = parent;
  bad.tx.level = 99.f;
  CHECK(!validateEvent(bad).ok());
}

CHOP_TEST(ids_hash_and_compare) {
  std::unordered_set<EventId> set;
  set.insert(EventId{1});
  set.insert(EventId{1});
  set.insert(EventId{2});
  CHECK_EQ(set.size(), 2u);
  CHECK(!EventId{}.valid() && EventId{3}.valid());
  Result<int> ok = 5;
  Result<int> bad = makeError(ErrorCode::Corrupt, "x");
  CHECK(ok.ok() && ok.value() == 5);
  CHECK(!bad.ok() && bad.error().code == ErrorCode::Corrupt);
}
