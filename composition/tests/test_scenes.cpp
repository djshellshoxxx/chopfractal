// F09: the session-level Evolve scenes (list, recording, armed playback with a real Renderer, state, export).
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <set>

#include "session_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

bool hasNotice(ProjectSession& s, const std::string& text) {
  bool found = false;
  for (const Notice& n : s.takeNotices()) found = found || n.text == text;
  return found;
}

std::vector<std::uint8_t> patternBytes(const ProjectSession& s) { return pattern::serialize(*s.pattern()); }
std::vector<std::uint8_t> displayBytes(const ProjectSession& s) { return pattern::serialize(*s.displayPattern()); }

// Captures `n` distinct patterns (seeds 11, 12, ...) and returns their ids; the live pattern ends as the last one.
std::vector<scenes::SceneId> captureSeeds(ProjectSession& s, int n, int bars = 2) {
  std::vector<scenes::SceneId> ids;
  for (int i = 0; i < n; ++i) {
    CHECK(s.generate(fixture::settings(11 + static_cast<std::uint64_t>(i), bars)).ok());
    auto r = s.captureScene();
    CHECK(r.ok());
    if (r.ok()) ids.push_back(r.value());
  }
  return ids;
}

void setLoops(ProjectSession& s, scenes::SceneId id, int loops, scenes::Next next = scenes::Next::Advance, scenes::SceneId jump = 0) {
  CHECK(s.editScene(id, [&](scenes::Scene& sc) {
          sc.loops = loops;
          sc.next = next;
          sc.jumpTo = jump;
        }).ok());
}

// A session wired to a real Renderer, driven like the plugin: blocks of audio, then the 30 Hz poll after a loop boundary.
struct Rig {
  ProjectSession s;
  render::Renderer r;
  std::int64_t pos = 0;
  Rig() {
    render::Config c;
    c.maxBlock = 512;
    r.prepare(c);
    s.attachMailbox(&r.mailbox());
    s.attachArmMailbox(&r.armMailbox());
  }
  Ticks loopTicks() const { return s.playback() ? s.playback()->lengthTicks : 0; }
  // Plays `frames` frames at 120 BPM / 48 kHz (0.04 ticks per frame); `polls` = call the session hooks like pollAudioFlags.
  int run(std::int64_t frames, bool polls = true) {
    int boundaries = 0;
    std::vector<float> x(512, 0.f), oL(512), oR(512);
    const float* in[2] = {x.data(), x.data()};
    float* out[2] = {oL.data(), oR.data()};
    render::RenderParams p;
    for (std::int64_t done = 0; done < frames;) {
      const int n = static_cast<int>(std::min<std::int64_t>(512, frames - done));
      render::TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.bpm = 120.0;
      tb.ppq = static_cast<double>(pos) * 0.04 / 960.0;
      const Ticks len = loopTicks();
      r.process(tb, p, in, out, 2, n);
      if (polls && len > 0) {
        const double before = static_cast<double>(pos) * 0.04, after = static_cast<double>(pos + n) * 0.04;
        if (std::floor(after / static_cast<double>(len) + 1e-9) > std::floor(before / static_cast<double>(len) + 1e-9)) {
          ++boundaries;
          s.onLoopBoundary();
          s.onSceneBoundary(r.appliedSwitchToken());
        }
      }
      pos += n;
      done += n;
    }
    return boundaries;
  }
  std::int64_t loopFrames() const { return static_cast<std::int64_t>(std::llround(static_cast<double>(loopTicks()) / 0.04)); }
};

bool ready(Rig& rig, bool play = true) {
  CHECK(fixture::ready(rig.s, true, 5, 2));
  if (play) rig.s.onTransportChanged(true);
  return rig.s.pattern() != nullptr;
}

}  // namespace

CHOP_TEST(capture_stores_pattern_seed_and_dedupes_identical_captures) {
  ProjectSession s;
  CHECK(!s.captureScene().ok());  // no pattern
  CHECK(fixture::ready(s, true, 31, 2));
  auto a = s.captureScene();
  CHECK(a.ok());
  const scenes::Scene* sc = scenes::find(s.sceneList(), a.value());
  CHECK(sc != nullptr);
  CHECK(sc->name == "Scene 1" && sc->loops == 4 && sc->seed == 31 && sc->bars == 2 && sc->evolveStep == scenes::Scene::kManual);
  CHECK(sc->pattern == patternBytes(s));
  CHECK(sc->eventCount == pattern::eventCount(*s.pattern()));
  auto again = s.captureScene();
  CHECK(again.ok() && again.value() == a.value() && s.sceneList().scenes.size() == 1);  // identical to the last scene: no new scene
  CHECK(s.generate(fixture::settings(32, 2)).ok());
  auto b = s.captureScene("Fill");
  CHECK(b.ok() && b.value() != a.value() && s.sceneList().scenes.size() == 2);
  CHECK(scenes::find(s.sceneList(), b.value())->name == "Fill");
  CHECK(hasNotice(s, "Captured Scene 2."));
  CHECK(s.section(kTagScenes) != nullptr);
}

CHOP_TEST(capture_fails_at_32_with_the_exact_message) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 1, 1));
  for (int i = 0; i < 32; ++i) {
    CHECK(s.generate(fixture::settings(100 + static_cast<std::uint64_t>(i), 1)).ok());
    CHECK(s.captureScene().ok());
  }
  CHECK_EQ(s.sceneList().scenes.size(), 32u);
  CHECK(s.generate(fixture::settings(999, 1)).ok());
  auto over = s.captureScene();
  CHECK(!over.ok());
  CHECK(over.error().code == ErrorCode::LimitExceeded);
  CHECK(over.error().message == "The scene list is full (32). Delete a scene to capture more.");
  CHECK_EQ(s.sceneList().scenes.size(), 32u);
}

CHOP_TEST(edit_scene_changes_only_user_fields_and_validates) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 31, 2));
  auto ids = captureSeeds(s, 2);
  const auto bytes = scenes::find(s.sceneList(), ids[0])->pattern;
  CHECK(s.editScene(ids[0], [&](scenes::Scene& sc) {
          sc.name = "Intro";
          sc.loops = 8;
          sc.next = scenes::Next::Jump;
          sc.jumpTo = ids[1];
          sc.pattern.clear();  // ignored
          sc.seed = 12345;     // ignored
        }).ok());
  const scenes::Scene* sc = scenes::find(s.sceneList(), ids[0]);
  CHECK(sc->name == "Intro" && sc->loops == 8 && sc->next == scenes::Next::Jump && sc->jumpTo == ids[1] && sc->pattern == bytes && sc->seed == 11);
  CHECK(!s.editScene(ids[0], [](scenes::Scene& x) { x.loops = 0; }).ok());
  CHECK(!s.editScene(ids[0], [](scenes::Scene& x) { x.name.clear(); }).ok());
  CHECK(!s.editScene(ids[0], [](scenes::Scene& x) { x.jumpTo = 9999; }).ok());
  CHECK(!s.editScene(9999, nullptr).ok());
  CHECK(scenes::find(s.sceneList(), ids[0])->loops == 8);  // failed edits changed nothing
  CHECK(s.setSceneWrap(false).ok() && !s.sceneList().wrap);
  CHECK(s.moveScene(ids[1], 0).ok() && s.sceneList().scenes[0].id == ids[1]);
  CHECK(s.removeScene(ids[1]).ok());
  CHECK(scenes::find(s.sceneList(), ids[0])->next == scenes::Next::Advance);  // the dangling jump was cleared
}

CHOP_TEST(evolve_recording_captures_start_and_each_step_and_stops_when_full) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  evolve::Settings es;
  es.enabled = true;
  es.everyLoops = 1;
  es.amount = 0.6;
  es.startSeed = 9;
  CHECK(s.setEvolve(es).ok());
  CHECK(s.setRecordScenes(true).ok() && s.recordingScenes());
  CHECK_EQ(s.sceneList().scenes.size(), 1u);
  CHECK(s.sceneList().scenes[0].name == "Scene 1 (start)");
  int stepped = 0;
  for (int i = 0; i < 30 && stepped < 3; ++i) {
    auto r = s.evolveStep();
    CHECK(r.ok());
    if (r.ok() && r.value()) ++stepped;
  }
  CHECK_EQ(stepped, 3);
  CHECK_EQ(s.sceneList().scenes.size(), 4u);
  for (std::size_t i = 1; i < 4; ++i) {
    const scenes::Scene& sc = s.sceneList().scenes[i];
    CHECK(sc.evolveStep != scenes::Scene::kManual);
    CHECK(sc.name == "Evolve step " + std::to_string(sc.evolveStep + 1));
    CHECK(sc.loops == 1);
  }
  CHECK(s.sceneList().scenes[3].pattern == patternBytes(s));
  s.takeNotices();
  // Run until the list is full: recording turns itself off with the message, Evolve goes on.
  for (int i = 0; i < 400 && s.recordingScenes(); ++i) s.evolveStep();
  CHECK(!s.recordingScenes());
  CHECK_EQ(s.sceneList().scenes.size(), 32u);
  CHECK(hasNotice(s, "Scene recording stopped: the list is full (32)."));
  CHECK(s.evolveRunning());
}

CHOP_TEST(recorded_scenes_replay_the_exact_patterns) {
  std::vector<std::uint8_t> state;
  std::vector<std::vector<std::uint8_t>> stored;
  {
    ProjectSession s;
    CHECK(fixture::ready(s, true, 5, 2));
    evolve::Settings es;
    es.enabled = true;
    es.everyLoops = 1;
    es.amount = 0.5;
    CHECK(s.setEvolve(es).ok());
    CHECK(s.setRecordScenes(true).ok());
    for (int i = 0; i < 4; ++i) s.evolveStep();
    s.setRecordScenes(false);
    s.setEvolve(evolve::Settings{});
    for (const auto& sc : s.sceneList().scenes) stored.push_back(sc.pattern);
    auto saved = s.saveState(true);
    CHECK(saved.ok());
    state = saved.value();
  }
  ProjectSession t;  // no audio side: every boundary installs the scene directly
  CHECK(t.loadState(state.data(), state.size(), {}).ok());
  CHECK_EQ(t.sceneList().scenes.size(), stored.size());
  for (std::size_t i = 0; i < stored.size(); ++i) CHECK(t.sceneList().scenes[i].pattern == stored[i]);
  CHECK(t.playScenes(0, Quantize::Immediate).ok());
  const auto real = patternBytes(t);
  for (std::size_t i = 0; i < stored.size(); ++i) {
    CHECK(displayBytes(t) == pattern::serialize(pattern::sanitize(pattern::deserialize(stored[i].data(), stored[i].size()).value(), *t.chops())));
    CHECK(patternBytes(t) == real);
    for (int k = 0; k < t.sceneList().scenes[i].loops; ++k) t.onSceneBoundary(0);
  }
}

CHOP_TEST(playing_scenes_arms_the_next_scene_and_adopts_it_when_the_renderer_reports_the_switch) {
  Rig rig;
  CHECK(ready(rig));
  auto ids = captureSeeds(rig.s, 3);
  setLoops(rig.s, ids[0], 1);
  setLoops(rig.s, ids[1], 2);
  setLoops(rig.s, ids[2], 1);
  const auto real = patternBytes(rig.s);
  const std::size_t nodes = rig.s.history().listAll().size();
  rig.s.takeNotices();
  rig.run(1000, false);
  CHECK(rig.s.playScenes(ids[0]).ok());
  CHECK(hasNotice(rig.s, "Playing scenes from Scene 1."));
  SceneStatus st = rig.s.sceneStatus();
  CHECK(st.running && st.playing == 0 && st.armed == ids[0] && !st.waitingForTransport);
  const std::int64_t loop = rig.loopFrames();
  CHECK(loop > 0);
  std::vector<scenes::SceneId> seen;
  std::vector<std::uint64_t> tokens;
  for (int i = 0; i < 4; ++i) {
    rig.run(loop);  // crosses exactly one boundary
    seen.push_back(rig.s.sceneStatus().playing);
    tokens.push_back(rig.r.appliedSwitchToken());
    CHECK(patternBytes(rig.s) == real);
    CHECK(rig.s.history().listAll().size() == nodes);
  }
  CHECK(seen[0] == ids[0] && seen[1] == ids[1] && seen[2] == ids[1] && seen[3] == ids[2]);
  CHECK(tokens[0] == 1 && tokens[1] == 2 && tokens[2] == 2 && tokens[3] == 3);  // the renderer applied every switch itself
  CHECK(displayBytes(rig.s) == pattern::serialize(*rig.s.displayPattern()));
  CHECK(rig.s.displayPattern() != rig.s.pattern());
  CHECK(rig.s.sceneStatus().armed == ids[0]);  // C has one loop and wraps to A: armed as soon as it started
  CHECK(!rig.s.stopScenes().ok() == false);
  CHECK(!rig.s.sceneStatus().running && rig.s.displayPattern() == rig.s.pattern());
}

CHOP_TEST(late_arm_falls_back_to_immediate_install_and_cancels_the_arm) {
  Rig rig;
  CHECK(ready(rig));
  auto ids = captureSeeds(rig.s, 2);
  setLoops(rig.s, ids[0], 1);
  setLoops(rig.s, ids[1], 1);
  rig.run(2000, false);
  CHECK(rig.s.playScenes(ids[0], Quantize::Immediate).ok());
  CHECK(rig.s.sceneStatus().playing == ids[0] && rig.s.sceneStatus().armed == ids[1]);
  // The boundary is observed before the renderer ever saw the arm (no audio ran): install now.
  rig.s.onSceneBoundary(rig.r.appliedSwitchToken());
  CHECK(rig.s.sceneStatus().playing == ids[1]);
  CHECK_EQ(rig.r.appliedSwitchToken(), 0u);
  // Tokens so far: 1 = the arm of B (never seen by the renderer), 2 = its cancel, 3 = the next arm (A again).
  // The renderer must apply only the newest one: the cancelled arm cannot come back.
  CHECK(rig.s.sceneStatus().armed == ids[0]);
  rig.pos = 0;
  rig.run(rig.loopFrames() * 2 + 1000, false);
  CHECK_EQ(rig.r.appliedSwitchToken(), 3u);
}

CHOP_TEST(transport_stop_cancels_the_arm_and_start_resumes) {
  Rig rig;
  CHECK(ready(rig));
  auto ids = captureSeeds(rig.s, 2);
  setLoops(rig.s, ids[0], 2);
  setLoops(rig.s, ids[1], 2);
  rig.run(1000, false);
  CHECK(rig.s.playScenes(ids[0]).ok());
  rig.run(rig.loopFrames());  // A begins
  CHECK(rig.s.sceneStatus().playing == ids[0] && rig.s.sceneStatus().loopIndex == 0);
  rig.run(rig.loopFrames());  // one loop of A done: B is armed
  CHECK(rig.s.sceneStatus().loopIndex == 1 && rig.s.sceneStatus().armed == ids[1]);
  rig.s.onTransportChanged(false);
  SceneStatus st = rig.s.sceneStatus();
  CHECK(st.running && st.waitingForTransport && st.armed == 0 && st.loopIndex == 0 && st.playing == ids[0]);
  rig.s.onTransportChanged(true);
  CHECK(!rig.s.sceneStatus().waitingForTransport);
  rig.run(rig.loopFrames() * 2 + 100);
  CHECK(rig.s.sceneStatus().playing == ids[1]);  // A counted its two loops again, then B began
}

CHOP_TEST(any_pattern_change_stops_scenes_with_the_notice) {
  const char* const kText = "Scene playback stopped because you changed the pattern.";
  struct Case {
    const char* name;
    std::function<void(ProjectSession&)> act;
  };
  const std::vector<Case> cases = {
      {"generate", [](ProjectSession& s) { s.generate(fixture::settings(77, 2)); }},
      {"mutate", [](ProjectSession& s) { s.mutate(pattern::MutateOptions{}); }},
      {"edit", [](ProjectSession& s) { s.edit([](const pattern::Pattern& p, const ChopSnapshot&) -> Result<pattern::Pattern> { return pattern::clearLocks(p); }); }},
      {"undo", [](ProjectSession& s) { s.undo(); }},
      {"recall", [](ProjectSession& s) { s.recallSnapshot(0); }},
      {"activate", [](ProjectSession& s) { s.activate(s.history().listAll().front().id, Quantize::Immediate); }},
  };
  for (const Case& c : cases) {
    ProjectSession s;
    CHECK(fixture::ready(s, true, 5, 2));
    CHECK(s.storeSnapshot(0).ok());
    auto ids = captureSeeds(s, 2);
    CHECK(s.playScenes(ids[0], Quantize::Immediate).ok());
    s.takeNotices();
    CHECK(s.sceneStatus().running);
    c.act(s);
    if (s.sceneStatus().running) std::printf("scenes still running after %s\n", c.name);
    CHECK(!s.sceneStatus().running);
    CHECK(s.displayPattern() == s.pattern());
    CHECK(hasNotice(s, kText));
  }
}

CHOP_TEST(evolve_and_scenes_are_mutually_exclusive) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  auto ids = captureSeeds(s, 2);
  evolve::Settings es;
  es.enabled = true;
  es.everyLoops = 2;
  CHECK(s.setEvolve(es).ok() && s.evolveRunning());
  s.takeNotices();
  CHECK(s.playScenes(0, Quantize::Immediate).ok());
  CHECK(!s.evolveRunning());
  CHECK(hasNotice(s, "Evolve was switched off: scenes and Evolve cannot run together."));
  CHECK(s.sceneStatus().running);
  CHECK(s.setEvolve(es).ok());
  CHECK(s.evolveRunning() && !s.sceneStatus().running);
  CHECK(hasNotice(s, "Scene playback stopped: Evolve was switched on."));
  (void)ids;
}

CHOP_TEST(morph_and_scenes_are_mutually_exclusive) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 21, 2));
  CHECK(s.storeSnapshot(0).ok());
  CHECK(s.generate(fixture::settings(22, 2)).ok());
  CHECK(s.storeSnapshot(1).ok());
  auto ids = captureSeeds(s, 1);
  CHECK(s.beginMorph(0.5).ok());
  auto r = s.playScenes(0, Quantize::Immediate);
  CHECK(!r.ok() && r.error().message == "Cancel the morph before playing scenes.");
  s.cancelMorph();
  CHECK(s.playScenes(0, Quantize::Immediate).ok());
  CHECK(!s.beginMorph(0.5).ok());
  (void)ids;
}

CHOP_TEST(play_refusals_use_the_exact_messages) {
  ProjectSession s;
  auto none = s.playScenes();
  CHECK(!none.ok() && none.error().message == "Scenes need a pattern and a source.");
  CHECK(fixture::ready(s, true, 5, 2));
  auto empty = s.playScenes();
  CHECK(!empty.ok() && empty.error().message == "Capture at least one scene first.");
}

CHOP_TEST(scene_that_no_longer_fits_is_skipped_with_a_notice) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  // Find two patterns where the second uses a chop the first does not.
  ChopId victim;
  scenes::SceneId first = 0, second = 0;
  for (std::uint64_t seed = 11; seed < 60 && !victim.valid(); ++seed) {
    ProjectSession probe;
    CHECK(fixture::ready(probe, true, 5, 2));
    CHECK(probe.generate(fixture::settings(seed, 2)).ok());
    CHECK(probe.generate(fixture::settings(seed + 1, 2)).ok());
    auto chopsOf = [&](const pattern::Pattern& p) {
      std::set<std::uint64_t> out;
      auto flat = pattern::flatten(p, *probe.chops());
      if (flat.ok())
        for (const FlatEvent& e : flat.value()) out.insert(e.chop.value);
      return out;
    };
    ProjectSession a;
    CHECK(fixture::ready(a, true, 5, 2));
    CHECK(a.generate(fixture::settings(seed, 2)).ok());
    const auto setA = chopsOf(*a.pattern());
    CHECK(a.generate(fixture::settings(seed + 1, 2)).ok());
    const auto setB = chopsOf(*a.pattern());
    for (std::uint64_t id : setB)
      if (!setA.count(id) && !victim.valid()) {
        victim = ChopId{id};
        CHECK(s.generate(fixture::settings(seed, 2)).ok());
        auto r1 = s.captureScene();
        CHECK(s.generate(fixture::settings(seed + 1, 2)).ok());
        auto r2 = s.captureScene();
        CHECK(r1.ok() && r2.ok());
        first = r1.value();
        second = r2.value();
      }
  }
  CHECK(victim.valid());
  if (!victim.valid()) return;
  CHECK(s.sceneFits(first) && s.sceneFits(second));
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(victim); }).ok());
  CHECK(s.sceneFits(first) && !s.sceneFits(second));
  s.takeNotices();
  CHECK(s.playScenes(0, Quantize::Immediate).ok());
  CHECK(hasNotice(s, "Scene 2 no longer fits the chops and was skipped."));
  CHECK(s.sceneStatus().playing == first);
  // Only scene 1 remains in the sequence.
  s.onSceneBoundary(0);
  s.onSceneBoundary(0);
  s.onSceneBoundary(0);
  s.onSceneBoundary(0);
  CHECK(s.sceneStatus().playing == first);
  // When nothing fits, playback is refused with the message.
  CHECK(s.stopScenes().ok());
  CHECK(s.removeScene(first).ok());
  auto r = s.playScenes(0, Quantize::Immediate);
  CHECK(!r.ok() && r.error().message == "No scene fits the current chops; scene playback stopped.");
}

CHOP_TEST(saved_state_holds_the_real_pattern_while_scenes_play) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  auto ids = captureSeeds(s, 2);
  const auto before = s.saveState(true);
  CHECK(before.ok());
  CHECK(s.playScenes(ids[0], Quantize::Immediate).ok());
  CHECK(displayBytes(s) != patternBytes(s));
  const auto during = s.saveState(true);
  CHECK(during.ok() && during.value() == before.value());
  ProjectSession t;
  CHECK(t.loadState(during.value().data(), during.value().size(), {}).ok());
  CHECK(patternBytes(t) == patternBytes(s));
  CHECK(!t.sceneStatus().running && t.displayPattern() == t.pattern());
  CHECK(t.sceneList().scenes.size() == 2);
}

CHOP_TEST(scenes_section_roundtrips_is_absent_when_empty_and_hostile_sweep_is_safe) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  const auto plain = s.saveState(true);
  CHECK(plain.ok());
  CHECK(s.section(kTagScenes) == nullptr);
  auto ids = captureSeeds(s, 3);
  setLoops(s, ids[1], 7, scenes::Next::Repeat);
  s.setSceneWrap(false);
  const auto saved = s.saveState(true);
  CHECK(saved.ok() && saved.value() != plain.value());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), {}).ok());
  CHECK_EQ(t.sceneList().scenes.size(), 3u);
  CHECK(t.sceneList().scenes[1].loops == 7 && t.sceneList().scenes[1].next == scenes::Next::Repeat && !t.sceneList().wrap);
  const auto again = t.saveState(true);
  CHECK(again.ok() && again.value() == saved.value());
  // Removing every scene returns to the exact old bytes.
  CHECK(t.clearScenes().ok());
  CHECK(t.section(kTagScenes) == nullptr);
  // A damaged section drops only the scenes: one warning, the project loads.
  StateSection bad;
  bad.tag = kTagScenes;
  bad.schema = 1;
  bad.bytes = {1, 2, 3};
  ProjectSession u;
  CHECK(fixture::ready(u, true, 5, 2));
  CHECK(u.setSection(bad).ok());
  const auto withBad = u.saveState(true);
  CHECK(withBad.ok());
  ProjectSession v;
  CHECK(v.loadState(withBad.value().data(), withBad.value().size(), {}).ok());
  CHECK(v.sceneList().scenes.empty() && v.section(kTagScenes) == nullptr && v.pattern() != nullptr);
  CHECK(hasNotice(v, "Some scene settings could not be restored."));
  // Hostile sweep: byte flips and truncations of the saved scenes section never crash or leave a broken list.
  const StateSection* sec = s.section(kTagScenes);
  CHECK(sec != nullptr);
  if (!sec) return;
  std::uint64_t rng = 0x9E3779B97F4A7C15ull;
  auto next = [&] {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
  };
  for (int i = 0; i < 300; ++i) {
    StateSection hostile = *sec;
    if (i % 3 == 0) hostile.bytes.resize(next() % hostile.bytes.size());
    else hostile.bytes[next() % hostile.bytes.size()] ^= static_cast<std::uint8_t>(1u << (next() % 8));
    ProjectSession w;
    CHECK(fixture::ready(w, true, 5, 2));
    CHECK(w.setSection(hostile).ok());
    auto bytes = w.saveState(true);
    CHECK(bytes.ok());
    ProjectSession x;
    CHECK(x.loadState(bytes.value().data(), bytes.value().size(), {}).ok());
    CHECK(scenes::validate(x.sceneList()).ok());
    CHECK(x.pattern() != nullptr);
  }
}

CHOP_TEST(keep_scene_adds_a_child_of_the_active_node_and_one_undo_step) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  auto ids = captureSeeds(s, 2);
  const auto active = s.history().active();
  const std::size_t nodes = s.history().listAll().size();
  CHECK(s.playScenes(ids[0], Quantize::Immediate).ok());
  s.takeNotices();
  CHECK(s.keepScene(ids[0]).ok());
  CHECK(!s.sceneStatus().running);
  CHECK_EQ(s.history().listAll().size(), nodes + 1);
  CHECK(s.history().active() != active);
  CHECK(patternBytes(s) == pattern::serialize(pattern::sanitize(pattern::deserialize(scenes::find(s.sceneList(), ids[0])->pattern.data(), scenes::find(s.sceneList(), ids[0])->pattern.size()).value(), *s.chops())));
  CHECK(hasNotice(s, "Kept Scene 1 as a new variation."));
  CHECK(s.undo());  // one undo step returns to the pattern before
  CHECK(patternBytes(s) == scenes::find(s.sceneList(), ids[1])->pattern);
  // Deleting tree nodes never touches scenes.
  const auto count = s.sceneList().scenes.size();
  for (const auto& n : s.history().listAll()) s.deleteBranch(n.id, true);
  CHECK_EQ(s.sceneList().scenes.size(), count);
}

CHOP_TEST(delete_and_move_are_refused_while_playing) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  auto ids = captureSeeds(s, 3);
  CHECK(s.playScenes(ids[0], Quantize::Immediate).ok());
  auto del = s.removeScene(ids[1]);
  CHECK(!del.ok() && del.error().message == "Stop scene playback first.");
  CHECK(!s.moveScene(ids[1], 0).ok());
  CHECK(!s.clearScenes().ok());
  CHECK_EQ(s.sceneList().scenes.size(), 3u);
  CHECK(s.stopScenes().ok());
  CHECK(s.removeScene(ids[1]).ok());
}

CHOP_TEST(sequence_end_holds_the_last_scene_and_stop_returns_to_the_real_pattern) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  auto ids = captureSeeds(s, 2);
  setLoops(s, ids[0], 1);
  setLoops(s, ids[1], 1, scenes::Next::Stop);
  CHECK(s.playScenes(ids[0], Quantize::Immediate).ok());
  s.takeNotices();
  s.onSceneBoundary(0);  // B starts
  CHECK(s.sceneStatus().playing == ids[1]);
  s.onSceneBoundary(0);  // B's single loop is over: finished
  CHECK(!s.sceneStatus().running && s.sceneStatus().holding);
  CHECK(hasNotice(s, "Scene sequence finished at Scene 2; it keeps looping."));
  CHECK(s.displayPattern() != s.pattern());
  CHECK(s.stopScenes().ok());
  CHECK(!s.sceneStatus().holding && s.displayPattern() == s.pattern());
}

CHOP_TEST(export_scenes_writes_tsv_midi_and_wav_deterministically) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  auto ids = captureSeeds(s, 3);
  setLoops(s, ids[0], 2);
  CHECK(s.editScene(ids[1], [&](scenes::Scene& x) {
          x.name = "Tab\tName";
          x.next = scenes::Next::Jump;
          x.jumpTo = ids[0];
        }).ok());
  SceneExportOptions o;
  o.wav = true;
  o.wavOptions.sampleRate = 44100;
  o.wavOptions.format = wav::Format::Pcm16;
  o.wavOptions.dither = false;
  const std::string dir = fixture::tmpDir("cf_scenes", "export");
  auto rep = s.exportScenes(dir, o);
  CHECK(rep.ok());
  if (!rep.ok()) return;
  CHECK_EQ(rep.value().files.size(), 5u);  // tsv, 3 midi, wav
  const auto tsv = fixture::readFile(dir + "/scenes.tsv");
  const std::string text(tsv.begin(), tsv.end());
  CHECK(text.rfind("number\tname\tloops\tnext\tjump_to\tseed\tevolve_step\tevents\tbars\n", 0) == 0);
  CHECK(text.find("2\tTab Name\t4\tjump\t1\t12\t-\t") != std::string::npos);
  CHECK(text.find("1\tScene 1\t2\tnext\t\t11\t-\t") != std::string::npos);
  CHECK(text.back() == '\n');
  const std::vector<std::string> expected = {"scenes.tsv", "scene_01_scene_1.mid", "scene_02_tab_name.mid", "scene_03_scene_3.mid", "scenes.wav"};
  for (std::size_t k = 0; k < expected.size(); ++k) {
    const std::string& f = rep.value().files[k];
    CHECK(f.size() >= expected[k].size() && f.compare(f.size() - expected[k].size(), expected[k].size(), expected[k]) == 0);
  }
  // Same inputs, same bytes: a second export to a fresh folder is identical file by file.
  const std::string dir2 = fixture::tmpDir("cf_scenes", "export2");
  auto rep2 = s.exportScenes(dir2, o);
  CHECK(rep2.ok());
  if (!rep2.ok()) return;
  CHECK_EQ(rep2.value().files.size(), rep.value().files.size());
  for (std::size_t i = 0; i < rep.value().files.size(); ++i) {
    const auto a = fixture::readFile(rep.value().files[i]);
    const auto b = fixture::readFile(rep2.value().files[i]);
    CHECK(!a.empty() && a == b);
  }
  // Without overwrite a second export into the same folder fails and leaves the files alone.
  const auto before = fixture::readFile(dir + "/scenes.tsv");
  auto clash = s.exportScenes(dir, o);
  CHECK(!clash.ok() && clash.error().code == ErrorCode::Conflict);
  CHECK(fixture::readFile(dir + "/scenes.tsv") == before);
  o.overwrite = true;
  CHECK(s.exportScenes(dir, o).ok());
  CHECK(!s.exportScenes("", o).ok());
  ProjectSession empty;
  CHECK(!empty.exportScenes(dir, o).ok());
}

CHOP_TEST(export_wav_places_scenes_at_cumulative_offsets_and_overlaps_tails) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 1));
  auto ids = captureSeeds(s, 2, 1);
  setLoops(s, ids[0], 1);
  setLoops(s, ids[1], 2);
  SceneExportOptions o;
  o.list = false;
  o.midi = false;
  o.wav = true;
  o.bpm = 120.0;
  o.wavOptions.sampleRate = 48000;
  o.wavOptions.format = wav::Format::Float32;
  o.wavOptions.dither = false;
  const std::string dir = fixture::tmpDir("cf_scenes", "wav");
  auto rep = s.exportScenes(dir, o);
  CHECK(rep.ok());
  if (!rep.ok()) return;
  // One-bar scenes at 120 BPM: 96000 frames per loop. Total = 1 loop + 2 loops + the 0.5 s tail.
  CHECK_EQ(rep.value().frames, static_cast<std::uint64_t>(3 * 96000 + 24000));
  CHECK_EQ(rep.value().files.size(), 1u);
  // The second scene is the live pattern: rendered alone it equals the mix from frame 96000 (the first scene's tail has ended by then).
  render::OfflineSettings rs;
  rs.sampleRate = 48000;
  rs.bpm = 120.0;
  rs.cycles = 2;
  rs.tailSeconds = 0.5;
  rs.params = s.renderParams();
  rs.params.enabled = true;
  rs.params.dryMix = 0.f;
  auto alone = s.renderOffline(rs);
  CHECK(alone.ok());
  const auto bytes = fixture::readFile(rep.value().files[0]);
  auto dec = wav::decode(bytes.data(), bytes.size());
  CHECK(dec.ok());
  if (!alone.ok() || !dec.ok()) return;
  CHECK_EQ(dec.value().planar[0].size(), static_cast<std::size_t>(3 * 96000 + 24000));
  double worst = 0.0, worstTail = 0.0, peakTail = 0.0;
  for (std::size_t k = 24000; k < 192000; ++k) worst = std::max(worst, std::fabs(static_cast<double>(dec.value().planar[0][96000 + k]) - static_cast<double>(alone.value()[0][k])));
  CHECK(worst < 1e-6);
  for (std::size_t k = 0; k < 24000; ++k) {  // the first scene's tail overlaps the start of the second: they sum
    worstTail = std::max(worstTail, std::fabs(static_cast<double>(dec.value().planar[0][96000 + k]) - static_cast<double>(alone.value()[0][k])));
    peakTail = std::max(peakTail, std::fabs(static_cast<double>(dec.value().planar[0][96000 + k])));
  }
  CHECK(peakTail > 0.0);
  (void)worstTail;
}

CHOP_TEST(a_stale_arm_never_overwrites_an_edit) {
  Rig rig;
  CHECK(ready(rig));
  auto ids = captureSeeds(rig.s, 2);
  rig.run(2000, false);
  CHECK(rig.s.playScenes(ids[0]).ok());  // scene 1 is armed for the next loop boundary
  CHECK(rig.s.sceneStatus().armed == ids[0]);
  CHECK(rig.s.generate(fixture::settings(500, 2)).ok());  // the edit must win
  CHECK(!rig.s.sceneStatus().running);
  const auto edited = patternBytes(rig.s);
  rig.pos = 2000;
  rig.run(rig.loopFrames() * 2);
  CHECK_EQ(rig.r.appliedSwitchToken(), 0u);
  CHECK(patternBytes(rig.s) == edited && rig.s.displayPattern() == rig.s.pattern());
}

CHOP_TEST(stopping_the_transport_while_the_first_scene_waits_starts_it_at_once) {
  Rig rig;
  CHECK(ready(rig));
  auto ids = captureSeeds(rig.s, 2);
  rig.run(2000, false);
  CHECK(rig.s.playScenes(ids[1]).ok());
  CHECK(rig.s.sceneStatus().playing == 0 && rig.s.sceneStatus().armed == ids[1]);
  rig.s.onTransportChanged(false);
  const SceneStatus st = rig.s.sceneStatus();
  CHECK(st.running && st.playing == ids[1] && st.waitingForTransport);
  CHECK(rig.s.displayPattern() != rig.s.pattern());
}

CHOP_TEST(a_stopped_transport_installs_the_first_scene_immediately_and_resumes_counting_on_play) {
  Rig rig;
  CHECK(ready(rig, false));  // the host is stopped
  auto ids = captureSeeds(rig.s, 2);
  setLoops(rig.s, ids[0], 1);
  CHECK(rig.s.playScenes(ids[0]).ok());
  SceneStatus st = rig.s.sceneStatus();
  CHECK(st.running && st.playing == ids[0] && st.waitingForTransport);
  rig.s.onTransportChanged(true);
  CHECK(!rig.s.sceneStatus().waitingForTransport && rig.s.sceneStatus().armed == ids[1]);  // a one-loop scene arms its follower at once
}
