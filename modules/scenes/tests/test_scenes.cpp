#include <memory>
#include <chopfractal/scenes/scenes.hpp>

#include <initializer_list>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::scenes;

namespace {
Scene scene(const char* name, int loops = 2, Next next = Next::Advance, SceneId jump = 0) {
  Scene s;
  s.name = name;
  s.loops = loops;
  s.next = next;
  s.jumpTo = jump;
  s.pattern = {1, 2, 3};
  return s;
}
SceneList listOf(std::initializer_list<Scene> scenes, bool wrap = true) {
  SceneList l;
  l.wrap = wrap;
  for (const Scene& s : scenes) CHECK(add(l, s).ok());
  return l;
}
}  // namespace

CHOP_TEST(add_remove_move_keep_stable_ids_and_the_32_scene_cap) {
  SceneList l;
  for (std::size_t i = 0; i < kMaxScenes; ++i) CHECK(add(l, scene("s")).ok());
  CHECK_EQ(l.scenes.back().id, 32u);
  auto full = add(l, scene("x"));
  CHECK(!full.ok() && full.error().code == ErrorCode::LimitExceeded);
  CHECK(full.error().message == "The scene list is full (32). Delete a scene to capture more.");
  CHECK(remove(l, 5).ok());
  CHECK(!remove(l, 5).ok());
  CHECK_EQ(add(l, scene("y")).value(), 33u);  // ids are never reused
  CHECK(move(l, 33, 0).ok());
  CHECK_EQ(l.scenes.front().id, 33u);
  CHECK_EQ(indexOf(l, 33), 0);
  CHECK(validate(l).ok());
  CHECK(!move(l, 999, 0).ok());
}

CHOP_TEST(removing_a_jump_target_turns_the_jump_into_advance) {
  SceneList l = listOf({scene("a"), scene("b"), scene("c")});
  l.scenes[0].next = Next::Jump;
  l.scenes[0].jumpTo = 3;
  CHECK(validate(l).ok());
  CHECK(remove(l, 3).ok());
  CHECK(l.scenes[0].next == Next::Advance && l.scenes[0].jumpTo == 0);
}

CHOP_TEST(validate_rejects_bad_loops_names_dangling_jumps_and_oversized_patterns) {
  CHECK(!add(*std::make_unique<SceneList>(), scene("a", 0)).ok());
  CHECK(!add(*std::make_unique<SceneList>(), scene("a", 65)).ok());
  CHECK(!add(*std::make_unique<SceneList>(), scene("")).ok());
  Scene big = scene("big");
  big.pattern.assign(kMaxPatternBytes + 1, 0);
  SceneList l;
  CHECK(!add(l, big).ok());
  SceneList j = listOf({scene("a", 2, Next::Jump, 9)});
  CHECK(!validate(j).ok());
  SceneList dup = listOf({scene("a"), scene("b")});
  dup.scenes[1].id = dup.scenes[0].id;
  CHECK(!validate(dup).ok());
  Scene longName = scene(std::string(40, 'n').c_str());
  SceneList t;
  CHECK(add(t, longName).ok());
  CHECK_EQ(t.scenes[0].name.size(), kMaxNameLength);
}

CHOP_TEST(sequencer_advances_arms_one_loop_ahead_and_finishes_at_stop) {
  SceneList l = listOf({scene("a", 2), scene("b", 2), scene("c", 2, Next::Stop)});
  Sequencer q;
  Advance a = q.start(l, 1);
  CHECK(q.running() && q.current() == 1 && a.arm == 0);
  a = q.onBoundary(l);  // loop 1 of a: next is armed (loopIndex == loops - 1)
  CHECK(!a.switched && a.arm == 2);
  a = q.onBoundary(l);  // boundary after a's second loop: b plays
  CHECK(a.switched && a.playing == 2 && a.arm == 0);
  a = q.onBoundary(l);
  CHECK(a.arm == 3);
  a = q.onBoundary(l);
  CHECK(a.switched && a.playing == 3);
  a = q.onBoundary(l);  // c is the last (Stop): nothing is armed, finishing
  CHECK(!a.finished && a.arm == 0 && q.running());
  a = q.onBoundary(l);
  CHECK(a.finished && !q.running());
}

CHOP_TEST(single_loop_scenes_arm_immediately_and_wrap_repeat_and_jump_work) {
  SceneList l = listOf({scene("a", 1), scene("b", 1)});
  Sequencer q;
  Advance a = q.start(l, 1);
  CHECK_EQ(a.arm, 2u);  // loops == 1 arms the following scene as soon as the current one starts
  a = q.onBoundary(l);
  CHECK(a.switched && a.playing == 2 && a.arm == 1);  // wraps back to the first
  a = q.onBoundary(l);
  CHECK(a.switched && a.playing == 1);
  // wrap off: stops after the last
  SceneList nw = listOf({scene("a", 1), scene("b", 1)}, false);
  Sequencer q2;
  q2.start(nw, 2);
  a = q2.onBoundary(nw);
  CHECK(a.finished);
  // repeat: loops forever without arming
  SceneList rp = listOf({scene("a", 2, Next::Repeat), scene("b")});
  Sequencer q3;
  q3.start(rp, 1);
  for (int i = 0; i < 9; ++i) {
    a = q3.onBoundary(rp);
    CHECK(!a.switched && a.arm == 0 && q3.current() == 1 && q3.running());
  }
  // jump
  SceneList jp = listOf({scene("a", 1, Next::Jump, 3), scene("b"), scene("c")});
  Sequencer q4;
  a = q4.start(jp, 1);
  CHECK_EQ(a.arm, 3u);
  a = q4.onBoundary(jp);
  CHECK(a.switched && a.playing == 3);
}

CHOP_TEST(transport_stop_resets_the_loop_count_and_start_rearms_single_loop_scenes) {
  SceneList l = listOf({scene("a", 2), scene("b", 1)});
  Sequencer q;
  q.start(l, 1);
  q.onBoundary(l);  // loop index 1, b armed
  CHECK_EQ(q.armed(), 2u);
  q.onTransportStopped();
  CHECK_EQ(q.armed(), 0u);
  CHECK_EQ(q.loopIndex(), 0);
  Advance a = q.onTransportStarted(l);
  CHECK_EQ(a.arm, 0u);  // a has two loops: nothing to arm yet
  SceneList one = listOf({scene("a", 1), scene("b", 1)});
  Sequencer q2;
  q2.start(one, 1);
  q2.onTransportStopped();
  CHECK_EQ(q2.onTransportStarted(one).arm, 2u);
  q2.stop();
  CHECK(!q2.running());
  CHECK_EQ(q2.onBoundary(one).arm, 0u);
}

CHOP_TEST(codec_roundtrips_and_hostile_input_never_crashes) {
  SceneList l = listOf({scene("a", 3, Next::Jump, 2), scene("b", 5, Next::Repeat)}, false);
  l.scenes[0].seed = 77;
  l.scenes[1].evolveStep = 4;
  const auto bytes = serialize(l);
  auto back = deserialize(bytes.data(), bytes.size());
  CHECK(back.ok());
  CHECK(serialize(back.value()) == bytes);
  CHECK(!back.value().wrap && back.value().scenes[0].jumpTo == 2);
  for (std::size_t cut = 0; cut < bytes.size(); ++cut) CHECK(!deserialize(bytes.data(), cut).ok());
  auto trailing = bytes;
  trailing.push_back(0);
  CHECK(!deserialize(trailing.data(), trailing.size()).ok());
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    auto mutated = bytes;
    mutated[i] ^= 0xFF;
    (void)deserialize(mutated.data(), mutated.size());  // must not crash or over-allocate
  }
}
