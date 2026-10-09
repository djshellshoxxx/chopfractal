#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/recursive_hit_zoom/zoom.hpp>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::zoom;

namespace {

ChopSnapshot makeChops() {
  ChopSnapshot s;
  s.sourceFrames = 40000;
  s.sampleRate = 48000;
  for (int i = 0; i < 4; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 10000ll, (i + 1) * 10000ll}, 1, 0, 0});
  return s;
}

Event makeParent(Ticks duration = 960) {
  Event e;
  e.id = EventId{5};
  e.chop = ChopId{2};
  e.start = 960;
  e.duration = duration;
  e.tx.level = 0.8f;
  e.tx.pan = 0.25f;
  e.tx.pitchSemitones = 2.f;
  return e;
}

Context makeCtx(const ChopSnapshot& c) {
  Context ctx;
  ctx.sourceBounds = c.chops[1].range;
  ctx.compatible = c.chops;
  return ctx;
}

Event withChild(Event parent, std::shared_ptr<const NestedPattern> child) {
  parent.child = std::move(child);
  return parent;
}

std::vector<std::uint8_t> bytesOf(const NestedPattern& n) { return serialize(n); }

}  // namespace

CHOP_TEST(child_window_equals_parent_duration_and_stays_inside_it) {
  const ChopSnapshot c = makeChops();
  for (int n : {2, 3, 4, 5, 6, 8})
    for (Ticks dur : {960, 961, 1000, 777}) {
      Settings s;
      s.subdivisions = n;
      s.density = Density::Dense;
      s.shuffleTicks = 40;
      auto r = createChildPattern(makeParent(dur), makeCtx(c), s, 17);
      CHECK(r.ok());
      if (!r.ok()) continue;
      CHECK_EQ(r.value()->windowDuration, dur);
      CHECK_EQ(r.value()->events.size(), static_cast<std::size_t>(n));
      Ticks covered = 0;
      for (const Event& e : r.value()->events) {
        CHECK(e.start >= 0 && e.start + e.duration <= dur && e.duration > 0);
        covered += e.duration;
      }
      CHECK_EQ(covered, dur);  // cells tile the window exactly, with no rounding loss
    }
}

CHOP_TEST(source_reads_stay_within_ancestor_bounds_and_flatten_validates_it) {
  const ChopSnapshot c = makeChops();
  for (Operation op : {Operation::RepeatParent, Operation::SelectSubregion, Operation::CreateRests}) {
    Settings s;
    s.operation = op;
    s.subdivisions = 5;
    s.density = Density::Dense;
    const Event parent = makeParent();
    auto r = createChildPattern(parent, makeCtx(c), s, 3);
    CHECK(r.ok());
    if (!r.ok()) continue;
    for (const Event& e : r.value()->events) CHECK(c.chops[1].range.contains(e.region));
    auto flat = flatten(withChild(parent, r.value()), 0, c);
    CHECK(flat.ok());
    if (flat.ok())
      for (const FlatEvent& f : flat.value()) CHECK(c.chops[1].range.contains(f.region) && f.depth == 1);
  }
}

CHOP_TEST(select_subregion_partitions_the_parent_region_exactly) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.operation = Operation::SelectSubregion;
  s.subdivisions = 3;
  s.density = Density::Dense;
  auto r = createChildPattern(makeParent(), makeCtx(c), s, 1);
  CHECK(r.ok());
  if (!r.ok()) return;
  std::int64_t cursor = c.chops[1].range.start;
  for (const Event& e : r.value()->events) {
    CHECK_EQ(e.region.start, cursor);
    cursor = e.region.end;
  }
  CHECK_EQ(cursor, c.chops[1].range.end);
}

CHOP_TEST(alternate_chops_replace_the_source_explicitly_and_rests_leave_gaps) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.operation = Operation::AlternateChops;
  s.subdivisions = 8;
  s.density = Density::Dense;
  auto alt = createChildPattern(makeParent(), makeCtx(c), s, 4);
  CHECK(alt.ok());
  if (alt.ok()) {
    std::set<std::uint64_t> used;
    for (const Event& e : alt.value()->events) {
      used.insert(e.chop.value);
      CHECK(e.sourceOverride == (e.chop != ChopId{2}));
    }
    CHECK(used.size() > 1);
    CHECK(flatten(withChild(makeParent(), alt.value()), 0, c).ok());
  }
  Context noOthers = makeCtx(c);
  noOthers.compatible.clear();
  auto fallback = createChildPattern(makeParent(), noOthers, s, 4);
  CHECK(fallback.ok());
  if (fallback.ok())
    for (const Event& e : fallback.value()->events) CHECK(e.chop == ChopId{2} && !e.sourceOverride);

  s.operation = Operation::CreateRests;
  auto rests = createChildPattern(makeParent(), makeCtx(c), s, 4);
  CHECK(rests.ok() && rests.value()->events.size() < 8 && !rests.value()->events.empty());
}

CHOP_TEST(identical_inputs_reproduce_identical_trees_and_ids_are_stable) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.density = Density::Sparse;
  s.shuffleTicks = 30;
  auto a = createChildPattern(makeParent(), makeCtx(c), s, 99);
  auto b = createChildPattern(makeParent(), makeCtx(c), s, 99);
  auto other = createChildPattern(makeParent(), makeCtx(c), s, 100);
  CHECK(a.ok() && b.ok() && other.ok());
  if (!(a.ok() && b.ok() && other.ok())) return;
  CHECK(bytesOf(*a.value()) == bytesOf(*b.value()));
  // Different seeds may activate different cells, but a cell's id depends only on parent and index.
  std::set<std::uint64_t> ids;
  for (std::uint64_t seed = 1; seed < 30; ++seed) {
    auto r = createChildPattern(makeParent(), makeCtx(c), s, seed);
    CHECK(r.ok());
    if (r.ok())
      for (const Event& e : r.value()->events) {
        CHECK(e.id.valid() && (e.id.value >> 63) == 1);
        ids.insert(e.id.value);
      }
  }
  CHECK(ids.size() <= 4);  // four cells, so at most four distinct ids across every seed
}

CHOP_TEST(depth_and_event_budget_limits_are_enforced_with_hints) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.density = Density::Dense;
  Context ctx = makeCtx(c);
  ctx.parentDepth = 2;  // default max depth is 2, so a third level is refused
  auto deep = createChildPattern(makeParent(), ctx, s, 1);
  CHECK(!deep.ok() && deep.error().code == ErrorCode::LimitExceeded);
  s.maxDepth = 3;
  CHECK(createChildPattern(makeParent(), ctx, s, 1).ok());
  s.maxDepth = 4;
  CHECK(!createChildPattern(makeParent(), ctx, s, 1).ok());  // beyond the hard maximum

  s = Settings{};
  s.subdivisions = 8;
  s.density = Density::Dense;
  ctx = makeCtx(c);
  ctx.eventBudget = 5;
  auto over = createChildPattern(makeParent(), ctx, s, 1);
  CHECK(!over.ok() && over.error().code == ErrorCode::LimitExceeded && over.error().hint == 5);
  ctx.eventBudget = 1;
  auto tiny = createChildPattern(makeParent(), ctx, s, 1);
  CHECK(!tiny.ok() && tiny.error().hint == 0);
  CHECK_EQ(nearestPermittedSubdivision(7), 6);
  CHECK_EQ(nearestPermittedSubdivision(100), 8);
}

CHOP_TEST(parent_is_never_altered_and_collapse_restores_it) {
  const ChopSnapshot c = makeChops();
  const Event parent = makeParent();
  std::vector<std::uint8_t> before, after;
  bytes::Writer wb(before), wa(after);
  writeEvent(wb, parent);
  Settings s;
  auto r = createChildPattern(parent, makeCtx(c), s, 8);
  CHECK(r.ok());
  writeEvent(wa, parent);
  CHECK(before == after);
  const Event zoomed = withChild(parent, r.value());
  const Event back = collapse(zoomed);
  std::vector<std::uint8_t> restored;
  bytes::Writer wr(restored);
  writeEvent(wr, back);
  CHECK(restored == before);
  CHECK(!back.child && back.childActive);
}

CHOP_TEST(transform_inheritance_modes) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.density = Density::Dense;
  s.inherit = Inherit::Inherit;
  auto inherit = createChildPattern(makeParent(), makeCtx(c), s, 1);
  s.inherit = Inherit::Reset;
  auto reset = createChildPattern(makeParent(), makeCtx(c), s, 1);
  s.inherit = Inherit::Override;
  s.overrideTx.level = 0.3f;
  s.overrideTx.reverse = true;
  s.overrideMask = kLevel | kReverse;
  auto over = createChildPattern(makeParent(), makeCtx(c), s, 1);
  CHECK(inherit.ok() && reset.ok() && over.ok());
  if (!(inherit.ok() && reset.ok() && over.ok())) return;
  for (const Event& e : inherit.value()->events) CHECK(e.tx == makeParent().tx);
  for (const Event& e : reset.value()->events) CHECK(e.tx == EventTransform{});
  for (const Event& e : over.value()->events) {
    CHECK(e.tx.level == 0.3f && e.tx.reverse);
    CHECK(e.tx.pan == 0.25f && e.tx.pitchSemitones == 2.f);  // untouched fields still inherit
  }
  s.overrideTx.level = 50.f;  // out of range
  CHECK(!createChildPattern(makeParent(), makeCtx(c), s, 1).ok());
}

CHOP_TEST(shuffle_never_leaves_the_parent_window) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.subdivisions = 6;
  s.density = Density::Dense;
  s.shuffleTicks = 100000;
  for (std::uint64_t seed = 1; seed < 40; ++seed) {
    auto r = createChildPattern(makeParent(), makeCtx(c), s, seed);
    CHECK(r.ok());
    if (r.ok())
      for (const Event& e : r.value()->events) CHECK(e.start >= 0 && e.start + e.duration <= 960);
  }
  s.shuffleTicks = 0;
  auto still = createChildPattern(makeParent(), makeCtx(c), s, 1);
  CHECK(still.ok() && still.value()->events[1].start == 960 / 6);
}

CHOP_TEST(invalid_input_returns_errors_not_malformed_events) {
  const ChopSnapshot c = makeChops();
  Settings s;
  auto bad = [&](auto fn) {
    Settings x = s;
    Event p = makeParent();
    Context ctx = makeCtx(c);
    fn(x, p, ctx);
    CHECK(!createChildPattern(p, ctx, x, 1).ok());
  };
  bad([](Settings& x, Event&, Context&) { x.subdivisions = 7; });
  bad([](Settings& x, Event&, Context&) { x.densityValue = 1.5; });
  bad([](Settings& x, Event&, Context&) { x.shuffleTicks = -1; });
  bad([](Settings&, Event& p, Context&) { p.duration = 0; });
  bad([](Settings&, Event& p, Context&) { p.enabled = false; });
  bad([](Settings&, Event& p, Context&) { p.id = EventId{}; });
  bad([](Settings&, Event& p, Context&) { p.duration = 20; });  // too short for 4 children
  bad([](Settings&, Event&, Context& ctx) { ctx.sourceBounds = {500, 500}; });
  bad([](Settings& x, Event&, Context& ctx) {
    x.operation = Operation::SelectSubregion;
    ctx.sourceBounds = {100, 102};  // two frames cannot be split four ways
  });
}

CHOP_TEST(locked_children_survive_mutation_and_locked_patterns_refuse) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.subdivisions = 4;
  s.density = Density::Dense;
  const Event parent = makeParent();
  auto first = createChildPattern(parent, makeCtx(c), s, 1);
  CHECK(first.ok());
  if (!first.ok()) return;
  auto edited = std::make_shared<NestedPattern>(*first.value());
  edited->events[2].locked = true;
  edited->events[2].tx.level = 0.123f;
  s.operation = Operation::SelectSubregion;
  auto mutated = mutateChildren(withChild(parent, edited), makeCtx(c), s, 2);
  CHECK(mutated.ok());
  if (mutated.ok()) {
    bool found = false;
    for (const Event& e : mutated.value()->events)
      if (e.id == edited->events[2].id) {
        found = true;
        CHECK(e.locked && e.tx.level == 0.123f);
      }
    CHECK(found);
  }
  auto lockedAll = std::make_shared<NestedPattern>(*first.value());
  lockedAll->locked = true;
  auto refused = mutateChildren(withChild(parent, lockedAll), makeCtx(c), s, 2);
  CHECK(!refused.ok() && refused.error().code == ErrorCode::Blocked);
  CHECK(!mutateChildren(parent, makeCtx(c), s, 2).ok());  // nothing to mutate
}

CHOP_TEST(state_roundtrips_and_malformed_state_is_rejected) {
  const ChopSnapshot c = makeChops();
  Settings s;
  s.density = Density::Dense;
  auto r = createChildPattern(makeParent(), makeCtx(c), s, 6);
  CHECK(r.ok());
  if (!r.ok()) return;
  const auto blob = serialize(*r.value());
  auto back = deserialize(blob.data(), blob.size());
  CHECK(back.ok() && serialize(*back.value()) == blob);
  for (std::size_t cut = 0; cut < blob.size(); ++cut) CHECK(!deserialize(blob.data(), cut).ok());
  for (std::size_t i = 0; i < blob.size(); ++i) {
    auto bad = blob;
    bad[i] ^= 0x5A;
    auto d = deserialize(bad.data(), bad.size());
    if (d.ok())
      for (const Event& e : d.value()->events) CHECK(validateEvent(e).ok() && e.start + e.duration <= d.value()->windowDuration);
  }
}
