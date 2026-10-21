#include <chopfractal/scenes/scenes.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::scenes;

namespace {
Scene make(const char* name, int loops = 4, Next next = Next::Advance, SceneId jump = 0) {
  Scene s;
  s.name = name;
  s.loops = loops;
  s.next = next;
  s.jumpTo = jump;
  s.seed = 7;
  s.eventCount = 3;
  s.bars = 2;
  s.pattern = {1, 2, 3, 4};
  return s;
}
SceneId must(Result<SceneId> r) {
  CHECK(r.ok());
  return r.ok() ? r.value() : 0;
}
struct Abc {
  SceneList list;
  SceneId a, b, c;
};
Abc abc(bool wrap = false) {
  Abc x;
  x.list.wrap = wrap;
  x.a = must(add(x.list, make("A", 2)));
  x.b = must(add(x.list, make("B", 1)));
  x.c = must(add(x.list, make("C", 3, Next::Stop)));
  return x;
}
}  // namespace

CHOP_TEST(add_remove_move_keep_stable_ids_and_the_32_scene_cap) {
  SceneList l;
  std::vector<SceneId> ids;
  for (int i = 0; i < 32; ++i) ids.push_back(must(add(l, make("S"))));
  for (std::size_t i = 1; i < ids.size(); ++i) CHECK(ids[i] == ids[i - 1] + 1);
  auto over = add(l, make("S"));
  CHECK(!over.ok());
  CHECK(over.error().code == ErrorCode::LimitExceeded);
  CHECK(l.scenes.size() == 32);
  CHECK(remove(l, ids[5]).ok());
  const SceneId fresh = must(add(l, make("new")));
  CHECK(fresh == 33);  // ids are never reused
  CHECK(!remove(l, 9999).ok());
  CHECK(move(l, ids[0], 10).ok());
  CHECK(indexOf(l, ids[0]) == 10);
  CHECK(l.scenes[0].id == ids[1]);
  CHECK(!move(l, ids[0], 40).ok());
  CHECK(!move(l, ids[0], -1).ok());
  CHECK(!move(l, 9999, 0).ok());
  CHECK(validate(l).ok());
  CHECK(find(l, fresh) != nullptr);
  CHECK(find(l, 9999) == nullptr);
}

CHOP_TEST(validate_rejects_bad_loops_names_dangling_jumps_and_oversized_patterns) {
  SceneList l;
  CHECK(!add(l, make("")).ok());
  CHECK(!add(l, make("0123456789012345678901234567890123")).ok());
  CHECK(add(l, make("01234567890123456789012345678901")).ok());
  CHECK(!add(l, make("x", 0)).ok());
  CHECK(!add(l, make("x", 65)).ok());
  CHECK(add(l, make("x", 64)).ok());
  CHECK(!add(l, make("x", 4, Next::Jump, 777)).ok());
  Scene big = make("big");
  big.pattern.assign(kMaxPatternBytes + 1, 0);
  auto r = add(l, big);
  CHECK(!r.ok());
  CHECK(r.error().code == ErrorCode::LimitExceeded);
  Scene empty = make("empty");
  empty.pattern.clear();
  CHECK(!add(l, empty).ok());
  CHECK(validate(l).ok());
  l.scenes[0].next = Next::Jump;
  l.scenes[0].jumpTo = 424242;
  CHECK(!validate(l).ok());
  l.scenes[0].next = Next::Advance;
  l.scenes[1].id = l.scenes[0].id;
  CHECK(!validate(l).ok());
}

CHOP_TEST(remove_clears_dangling_jump_targets) {
  SceneList l;
  const SceneId a = must(add(l, make("A")));
  const SceneId b = must(add(l, make("B")));
  Scene j = make("J", 2, Next::Jump, b);
  const SceneId jid = must(add(l, j));
  CHECK(remove(l, b).ok());
  const Scene* js = find(l, jid);
  CHECK(js && js->next == Next::Advance && js->jumpTo == 0);
  CHECK(validate(l).ok());
  CHECK(find(l, a) != nullptr);
}

CHOP_TEST(serialize_roundtrips_and_rejects_truncation_and_bitflips) {
  Abc x = abc(true);
  Scene j = make("Jumper", 5, Next::Jump, x.a);
  j.evolveStep = 3;
  j.pattern.assign(300, 9);
  must(add(x.list, j));
  const auto bytes = serialize(x.list);
  CHECK(serialize(x.list) == bytes);
  auto back = deserialize(bytes.data(), bytes.size());
  CHECK(back.ok());
  if (back.ok()) {
    CHECK(serialize(back.value()) == bytes);
    CHECK(back.value().scenes.size() == 4);
    CHECK(back.value().wrap);
    CHECK(back.value().scenes[3].evolveStep == 3);
  }
  for (std::size_t n = 0; n < bytes.size(); ++n) {
    auto r = deserialize(bytes.data(), n);
    CHECK(!r.ok());
    if (!r.ok()) CHECK(r.error().code == ErrorCode::Corrupt);
  }
  std::uint64_t s = 88172645463325252ull;
  auto rnd = [&] {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  };
  for (int i = 0; i < 2000; ++i) {
    auto copy = bytes;
    copy[rnd() % copy.size()] ^= static_cast<std::uint8_t>(1u << (rnd() % 8));
    auto r = deserialize(copy.data(), copy.size());
    if (r.ok()) CHECK(validate(r.value()).ok());
    else CHECK(r.error().code == ErrorCode::Corrupt || r.error().code == ErrorCode::UnsupportedVersion);
  }
  const std::vector<std::uint8_t> trailing = [&] {
    auto t = bytes;
    t.push_back(0);
    return t;
  }();
  CHECK(!deserialize(trailing.data(), trailing.size()).ok());
}

CHOP_TEST(sequencer_trace_matches_the_table) {
  Abc x = abc(false);
  Sequencer q;
  Advance a = q.start(x.list, x.a);
  CHECK(a.arm == 0 && a.playing == x.a && q.running());
  a = q.onBoundary(x.list);  // 1
  CHECK(a.arm == x.b && !a.switched && q.armed() == x.b);
  a = q.onBoundary(x.list);  // 2
  CHECK(a.switched && a.playing == x.b && a.arm == x.c);
  a = q.onBoundary(x.list);  // 3
  CHECK(a.switched && a.playing == x.c && a.arm == 0);
  a = q.onBoundary(x.list);  // 4
  CHECK(!a.switched && a.arm == 0 && !a.finished);
  a = q.onBoundary(x.list);  // 5
  CHECK(!a.switched && a.arm == 0 && !a.finished && q.running());
  a = q.onBoundary(x.list);  // 6
  CHECK(a.finished && !q.running());
  CHECK(q.current() == x.c);
  a = q.onBoundary(x.list);  // after finish: inert
  CHECK(!a.switched && !a.finished && a.arm == 0);
}

CHOP_TEST(single_loop_scenes_arm_immediately) {
  SceneList l;
  const SceneId a = must(add(l, make("A", 1)));
  const SceneId b = must(add(l, make("B", 1)));
  Sequencer q;
  Advance s = q.start(l, a);
  CHECK(s.arm == b);
  Advance x = q.onBoundary(l);
  CHECK(x.switched && x.playing == b && x.arm == a);  // wrap defaults on
  x = q.onBoundary(l);
  CHECK(x.switched && x.playing == a && x.arm == b);
}

CHOP_TEST(wrap_repeat_and_jump) {
  SceneList l;
  const SceneId a = must(add(l, make("A", 2)));
  const SceneId b = must(add(l, make("B", 2, Next::Repeat)));
  const SceneId c = must(add(l, make("C", 1, Next::Jump, a)));
  l.scenes[0].next = Next::Jump;
  l.scenes[0].jumpTo = c;  // A jumps over B
  Sequencer q;
  q.start(l, a);
  Advance x = q.onBoundary(l);
  CHECK(x.arm == c);
  x = q.onBoundary(l);
  CHECK(x.switched && x.playing == c && x.arm == a);  // C has 1 loop: arms its jump target at once
  x = q.onBoundary(l);
  CHECK(x.switched && x.playing == a);
  // Repeat never arms and never finishes.
  Sequencer r;
  r.start(l, b);
  for (int i = 0; i < 20; ++i) {
    x = r.onBoundary(l);
    CHECK(!x.switched && x.arm == 0 && !x.finished && r.current() == b);
  }
  CHECK(r.loopIndex() >= 0 && r.loopIndex() < 2);
  // wrap off: the last Advance stops; wrap on: it goes to the first.
  SceneList w;
  const SceneId p = must(add(w, make("P", 1)));
  const SceneId q2 = must(add(w, make("Q", 1)));
  w.wrap = false;
  Sequencer s;
  Advance y = s.start(w, q2);
  CHECK(y.arm == 0);
  y = s.onBoundary(w);
  CHECK(y.finished && !s.running());
  w.wrap = true;
  y = s.start(w, q2);
  CHECK(y.arm == p);
  // start on a missing scene does nothing
  y = s.start(w, 9999);
  CHECK(!s.running() && y.playing == 0);
}

CHOP_TEST(transport_stop_resets_loop_index_and_clears_arm) {
  Abc x = abc(false);
  Sequencer q;
  q.start(x.list, x.a);
  q.onBoundary(x.list);
  CHECK(q.armed() == x.b && q.loopIndex() == 1);
  q.onTransportStopped();
  CHECK(q.armed() == 0 && q.loopIndex() == 0 && q.running() && q.current() == x.a);
  // Restart re-counts the scene from its first loop.
  Advance a = q.onTransportStarted(x.list);
  CHECK(a.arm == 0);
  a = q.onBoundary(x.list);
  CHECK(a.arm == x.b);
  // A one-loop scene re-arms as soon as the transport starts.
  Sequencer r;
  r.start(x.list, x.b);
  r.onTransportStopped();
  a = r.onTransportStarted(x.list);
  CHECK(a.arm == x.c);
  r.stop();
  CHECK(!r.running());
  a = r.onTransportStarted(x.list);
  CHECK(a.arm == 0);
}

CHOP_TEST(sequencer_is_deterministic_for_a_boundary_count) {
  SceneList l;
  l.wrap = true;
  const SceneId a = must(add(l, make("A", 3)));
  must(add(l, make("B", 2)));
  must(add(l, make("C", 1)));
  auto trace = [&](int n) {
    Sequencer q;
    q.start(l, a);
    std::vector<SceneId> t;
    for (int i = 0; i < n; ++i) {
      q.onBoundary(l);
      t.push_back(q.current() * 100 + static_cast<SceneId>(q.loopIndex()));
    }
    return t;
  };
  CHECK(trace(40) == trace(40));
  const auto full = trace(40);
  const auto part = trace(17);
  CHECK(std::equal(part.begin(), part.end(), full.begin()));
}

CHOP_TEST(a_scene_removed_while_running_stops_the_sequencer_cleanly) {
  Abc x = abc(true);
  Sequencer q;
  q.start(x.list, x.a);
  remove(x.list, x.a);
  const Advance a = q.onBoundary(x.list);
  CHECK(a.finished && !q.running());
}
