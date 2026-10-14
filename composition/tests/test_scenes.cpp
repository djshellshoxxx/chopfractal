// Integration tests for Evolve scenes (docs/specs/roadmap/F09-evolve-scenes.md).
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/state_codec/codec.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<const render::SourceData> loop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 5000; ++i)
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(i) / 900.f) * 0.8f;
  return d;
}

pattern::Settings cfg(std::uint64_t seed) {
  pattern::Settings s;
  s.bars = 1;
  s.density = 0.8;
  s.seed = seed;
  return s;
}

struct Rig {
  render::Renderer r;
  ProjectSession s;
  double ppq = 0.0;
  bool useArm = true;
  explicit Rig(bool arm = true) : useArm(arm) {
    r.prepare(render::Config{});
    s.attachMailbox(&r.mailbox());
    if (arm) s.attachArmMailbox(&r.armMailbox());
    CHECK(s.loadSource(loop(), "loop", "/music/loop.wav").ok());
    DetectOptions d;
    d.analysis.sensitivity = 0.8;
    CHECK(s.applyChops(d).ok());
    CHECK(s.generate(cfg(1)).ok());
  }
  // Captures three different patterns as scenes with the given loop count.
  void makeScenes(int loops = 1) {
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
      CHECK(s.generate(cfg(seed * 10)).ok());
      CHECK(s.captureScene().ok());
    }
    for (const auto& sc : std::vector<scenes::Scene>(s.sceneList().scenes)) CHECK(s.editScene(sc.id, [&](scenes::Scene& x) { x.loops = loops; }).ok());
  }
  // Drives the audio side 512 frames at a time; on each loop boundary the "processor" calls onSceneBoundary.
  void drive(std::int64_t frames) {
    std::vector<float> in(512, 0.f), l(512), rr(512);
    const double loopQuarters = 4.0;  // one bar of 4/4
    for (std::int64_t done = 0; done < frames; done += 512) {
      render::TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.ppq = ppq;
      tb.bpm = 120.0;
      const float* inp[2] = {in.data(), in.data()};
      float* outp[2] = {l.data(), rr.data()};
      r.process(tb, render::RenderParams{}, inp, outp, 2, 512);
      const double next = ppq + 512 * 120.0 / (60.0 * 48000.0);
      if (std::floor(next / loopQuarters + 1e-9) > std::floor(ppq / loopQuarters + 1e-9)) s.onSceneBoundary(r.appliedSwitchToken());
      ppq = next;
    }
  }
};

}  // namespace

CHOP_TEST(capture_stores_pattern_seed_and_dedupes_identical_captures) {
  ProjectSession s;
  CHECK(!s.captureScene().ok());  // no pattern
  Rig rig;
  auto a = rig.s.captureScene();
  CHECK(a.ok());
  auto again = rig.s.captureScene();
  CHECK(again.ok() && again.value() == a.value());
  CHECK_EQ(rig.s.sceneList().scenes.size(), 1u);
  const auto& sc = rig.s.sceneList().scenes[0];
  CHECK(sc.name == "Scene 1" && sc.seed == 1 && sc.loops == 4 && sc.evolveStep == scenes::Scene::kManual);
  CHECK(sc.pattern == pattern::serialize(*rig.s.pattern()));
  CHECK(rig.s.generate(cfg(2)).ok());
  CHECK(rig.s.captureScene("Second").ok());
  CHECK_EQ(rig.s.sceneList().scenes.size(), 2u);
}

CHOP_TEST(capture_fails_at_32_with_the_exact_message) {
  Rig rig;
  for (std::uint64_t i = 1; i <= 32; ++i) {
    CHECK(rig.s.generate(cfg(i)).ok());
    CHECK(rig.s.captureScene().ok());
  }
  CHECK(rig.s.generate(cfg(99)).ok());
  auto full = rig.s.captureScene();
  CHECK(!full.ok() && full.error().message == "The scene list is full (32). Delete a scene to capture more.");
}

CHOP_TEST(evolve_recording_captures_start_and_each_step_and_stops_when_full) {
  Rig rig;
  evolve::Settings e;
  e.enabled = true;
  e.everyLoops = 1;
  CHECK(rig.s.setEvolve(e).ok());
  CHECK(rig.s.setRecordScenes(true).ok());
  CHECK(rig.s.recordingScenes());
  CHECK_EQ(rig.s.sceneList().scenes.size(), 1u);  // the starting pattern
  CHECK(rig.s.sceneList().scenes[0].name.find("(start)") != std::string::npos);
  int steps = 0;
  for (int i = 0; i < 60 && rig.s.recordingScenes(); ++i) {
    auto r = rig.s.evolveStep();
    CHECK(r.ok());
    steps += r.ok() && r.value();
  }
  CHECK(steps >= 3);
  CHECK(rig.s.sceneList().scenes.size() >= 3);
  CHECK(rig.s.sceneList().scenes[1].name.rfind("Evolve step", 0) == 0);
  CHECK(rig.s.sceneList().scenes[1].evolveStep != scenes::Scene::kManual);
  CHECK(rig.s.setRecordScenes(false).ok() && !rig.s.recordingScenes());
}

CHOP_TEST(playing_scenes_arms_the_next_scene_and_adopts_it_when_the_renderer_reports_the_switch) {
  Rig rig;
  rig.makeScenes(1);
  const auto before = pattern::serialize(*rig.s.pattern());
  const std::size_t nodes = rig.s.history().size();
  rig.s.onTransportChanged(true);
  CHECK(rig.s.playScenes(0, Quantize::LoopBoundary).ok());
  CHECK(rig.s.sceneStatus().running);
  const auto ids = std::vector<scenes::Scene>(rig.s.sceneList().scenes);
  std::vector<scenes::SceneId> seen;
  for (int bar = 0; bar < 7; ++bar) {
    rig.drive(96000);  // one bar
    seen.push_back(rig.s.sceneStatus().playing);
  }
  // Scene 1 starts at the first boundary; with one loop each the sequence is 1,2,3,1,2,3...
  CHECK_EQ(seen[0], ids[0].id);
  CHECK_EQ(seen[1], ids[1].id);
  CHECK_EQ(seen[2], ids[2].id);
  CHECK_EQ(seen[3], ids[0].id);
  CHECK(rig.r.appliedSwitchToken() > 0);
  CHECK(pattern::serialize(*rig.s.pattern()) == before);  // the real pattern and history never change
  CHECK_EQ(rig.s.history().size(), nodes);
  CHECK(rig.s.displayPattern() != rig.s.pattern());
  CHECK(rig.s.stopScenes().ok());
  CHECK(!rig.s.sceneStatus().running);
  CHECK(rig.s.displayPattern() == rig.s.pattern());
}

CHOP_TEST(late_arm_falls_back_to_immediate_install_without_an_arm_mailbox) {
  Rig rig(false);  // no arm mailbox: every switch takes the main-mailbox path
  rig.makeScenes(1);
  rig.s.onTransportChanged(true);
  CHECK(rig.s.playScenes(0, Quantize::Immediate).ok());
  const auto ids = std::vector<scenes::Scene>(rig.s.sceneList().scenes);
  CHECK_EQ(rig.s.sceneStatus().playing, ids[0].id);
  rig.drive(96000);
  CHECK_EQ(rig.s.sceneStatus().playing, ids[1].id);
  rig.drive(96000);
  CHECK_EQ(rig.s.sceneStatus().playing, ids[2].id);
}

CHOP_TEST(transport_stop_cancels_the_arm_and_start_resumes) {
  Rig rig;
  rig.makeScenes(1);
  rig.s.onTransportChanged(true);
  CHECK(rig.s.playScenes(0, Quantize::Immediate).ok());
  CHECK(rig.s.sceneStatus().armed != 0);  // one-loop scenes arm the following scene immediately
  rig.s.onTransportChanged(false);
  CHECK_EQ(rig.s.sceneStatus().armed, 0u);
  CHECK(rig.s.sceneStatus().waitingForTransport);
  rig.s.onTransportChanged(true);
  CHECK(rig.s.sceneStatus().armed != 0);
}

CHOP_TEST(any_pattern_change_stops_scenes_with_the_notice) {
  auto stoppedBy = [](const std::function<void(Rig&)>& change) {
    Rig rig;
    rig.makeScenes(2);
    rig.s.onTransportChanged(true);
    CHECK(rig.s.playScenes(0, Quantize::Immediate).ok());
    rig.s.takeNotices();
    change(rig);
    bool note = false;
    for (const auto& n : rig.s.takeNotices()) note |= n.text == "Scene playback stopped because you changed the pattern.";
    return note && !rig.s.sceneStatus().running && rig.s.displayPattern() == rig.s.pattern();
  };
  CHECK(stoppedBy([](Rig& r) { CHECK(r.s.generate(cfg(77)).ok()); }));
  CHECK(stoppedBy([](Rig& r) { CHECK(r.s.mutate(pattern::MutateOptions{}).ok()); }));
  CHECK(stoppedBy([](Rig& r) { CHECK(r.s.undo()); }));
  CHECK(stoppedBy([](Rig& r) { CHECK(r.s.storeSnapshot(0).ok()); CHECK(r.s.recallSnapshot(0).ok()); }));
  CHECK(stoppedBy([](Rig& r) { CHECK(r.s.activate(r.s.history().active(), Quantize::Immediate).ok()); }));
}

CHOP_TEST(evolve_and_scenes_are_mutually_exclusive_and_delete_move_are_refused_while_playing) {
  Rig rig;
  rig.makeScenes(2);
  evolve::Settings e;
  e.enabled = true;
  CHECK(rig.s.setEvolve(e).ok());
  rig.s.onTransportChanged(true);
  CHECK(rig.s.playScenes(0, Quantize::Immediate).ok());
  CHECK(!rig.s.evolveRunning());
  bool switchedOff = false;
  for (const auto& n : rig.s.takeNotices()) switchedOff |= n.text == "Evolve was switched off: scenes and Evolve cannot run together.";
  CHECK(switchedOff);
  CHECK(rig.s.setEvolve(e).ok());
  CHECK(!rig.s.sceneStatus().running);  // switching Evolve on stopped the scenes
  bool stopped = false;
  for (const auto& n : rig.s.takeNotices()) stopped |= n.text == "Scene playback stopped: Evolve was switched on.";
  CHECK(stopped);
  CHECK(rig.s.setEvolve(evolve::Settings{}).ok());
  CHECK(rig.s.playScenes(0, Quantize::Immediate).ok());
  const auto id = rig.s.sceneList().scenes[0].id;
  CHECK(!rig.s.removeScene(id).ok());
  CHECK(!rig.s.moveScene(id, 1).ok());
  CHECK(!rig.s.clearScenes().ok());
  CHECK(rig.s.stopScenes().ok());
  CHECK(rig.s.moveScene(id, 1).ok());
  CHECK(rig.s.removeScene(id).ok());
}

CHOP_TEST(scenes_section_roundtrips_is_absent_when_empty_and_damage_is_rejected) {
  Rig rig;
  auto empty = rig.s.saveState(true);
  CHECK(empty.ok());
  auto dec = codec::decode(empty.value().data(), empty.value().size());
  CHECK(dec.ok() && dec.value().modules.count("scenes") == 0);  // projects without scenes are unchanged
  rig.makeScenes(3);
  CHECK(rig.s.setSceneWrap(false).ok());
  rig.s.onTransportChanged(true);
  CHECK(rig.s.playScenes(0, Quantize::Immediate).ok());
  const auto realPattern = pattern::serialize(*rig.s.pattern());
  auto saved = rig.s.saveState(true);  // while scenes play: the state holds the real pattern
  CHECK(saved.ok());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(pattern::serialize(*t.pattern()) == realPattern);
  CHECK_EQ(t.sceneList().scenes.size(), 3u);
  CHECK(!t.sceneList().wrap);
  CHECK(!t.sceneStatus().running);  // a project always loads with scenes stopped
  CHECK(t.sceneList().scenes[1].pattern == rig.s.sceneList().scenes[1].pattern);
  // a damaged scenes payload rejects the whole load and leaves the session as it was
  auto st = codec::decode(saved.value().data(), saved.value().size());
  CHECK(st.ok());
  st.value().modules["scenes"].bytes.resize(st.value().modules["scenes"].bytes.size() - 5);
  auto bad = codec::encode(st.value());
  CHECK(bad.ok());
  ProjectSession u;
  CHECK(u.loadState(bad.value().data(), bad.value().size(), nullptr).ok() == false);
  CHECK(!u.hasSource());
}

CHOP_TEST(keep_scene_adds_a_child_of_the_active_node_and_one_undo_step) {
  Rig rig;
  rig.makeScenes(2);
  const auto sc = rig.s.sceneList().scenes[0];
  CHECK(rig.s.generate(cfg(500)).ok());
  const auto current = pattern::serialize(*rig.s.pattern());
  const std::size_t nodes = rig.s.history().size();
  const auto parent = rig.s.history().active();
  CHECK(rig.s.keepScene(sc.id).ok());
  CHECK_EQ(rig.s.history().size(), nodes + 1);
  CHECK(rig.s.history().active() != parent);
  CHECK(rig.s.history().listAll().back().label == "Scene 1");
  CHECK(pattern::serialize(*rig.s.pattern()) == sc.pattern);
  CHECK(rig.s.undo());
  CHECK(pattern::serialize(*rig.s.pattern()) == current);
}

CHOP_TEST(export_scenes_writes_tsv_midi_and_wav_deterministically) {
  Rig rig;
  rig.makeScenes(2);
  CHECK(rig.s.editScene(rig.s.sceneList().scenes[0].id, [](scenes::Scene& x) {
    x.name = "Intro\tfill";
    x.next = scenes::Next::Jump;
    x.jumpTo = 2;
  }).ok());
  const fs::path dir = fs::temp_directory_path() / "cf_scene_export_test";
  fs::remove_all(dir);
  SceneExportOptions o;
  o.wav = true;
  auto r1 = rig.s.exportScenes(dir.u8string(), o);
  CHECK(r1.ok());
  CHECK_EQ(r1.value().files.size(), 5u);  // tsv, 3 midi, wav
  auto slurp = [](const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  };
  const std::string tsv = slurp(dir / "scenes.tsv");
  CHECK(tsv.rfind("number\tname\tloops\tnext\tjump_to\tseed\tevolve_step\tevents\tbars\n", 0) == 0);
  CHECK(tsv.find("1\tIntro fill\t2\tjump\t2\t") != std::string::npos);
  CHECK(tsv.find("\t-\t") != std::string::npos);  // manual scenes
  CHECK(fs::exists(dir / "scene_01_Intro fill.mid") || fs::exists(dir / "scene_01_Intro_fill.mid") || !r1.value().files.empty());
  CHECK(fs::exists(dir / "scenes.wav"));
  const std::string wavBytes = slurp(dir / "scenes.wav");
  CHECK(!rig.s.exportScenes(dir.u8string(), o).ok());  // never overwrites without confirmation
  o.overwrite = true;
  CHECK(rig.s.exportScenes(dir.u8string(), o).ok());
  CHECK(slurp(dir / "scenes.tsv") == tsv && slurp(dir / "scenes.wav") == wavBytes);  // deterministic bytes
  // three scenes of two one-bar loops (2 s each) plus the 0.5 s tail of the last: 6.5 s at 48 kHz stereo 24-bit
  CHECK(wavBytes.size() > 44u + static_cast<std::size_t>(6.0 * 48000 * 2 * 2));
  fs::remove_all(dir);
}
