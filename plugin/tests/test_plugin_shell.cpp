// Headless tests of the real JUCE processor: no host and no window, but the real parameter tree, host-time
// translation, audio path, project-state round trip and file decoding.
#include <algorithm>
#include <memory>
#include <chopfractal/chop_contracts/rng.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "BuildInfo.h"
#include <typeinfo>

// Newer JUCE releases deprecate AudioFormat::createWriterFor(...); the pinned release still needs it.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "CdlTheme.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "chop_test.hpp"
#include "shell_test_support.hpp"

namespace cf = chopfractal;

using namespace shelltest;

namespace {

// Sets a parameter the way a host does (through the parameter object, so the project state sees it).
void setParam(ChopFractalProcessor& p, const char* id, float plain) {
  auto* param = dynamic_cast<juce::RangedAudioParameter*>(p.apvts.getParameter(id));
  CHECK(param != nullptr);
  if (param) param->setValueNotifyingHost(param->convertTo0to1(plain));
}

// Runs `blocks` host blocks from ppq 0 and returns the left channel.
std::vector<float> run(ChopFractalProcessor& p, FakePlayHead& ph, int blocks, int block = 512, float inputLevel = 0.f) {
  p.setPlayHead(&ph);
  std::vector<float> out;
  juce::MidiBuffer midi;
  ph.ppq = 0.0;
  for (int b = 0; b < blocks; ++b) {
    juce::AudioBuffer<float> buf(2, block);
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < block; ++i) buf.setSample(c, i, inputLevel * std::sin(0.05f * static_cast<float>(i + b * block)));
    p.processBlock(buf, midi);
    for (int i = 0; i < block; ++i) out.push_back(buf.getSample(0, i));
    ph.ppq += static_cast<double>(block) * ph.bpm / (60.0 * 48000.0);
  }
  p.setPlayHead(nullptr);
  return out;
}

float peak(const std::vector<float>& v) {
  float m = 0.f;
  for (float x : v) {
    if (!std::isfinite(x)) return 1e9f;
    m = std::max(m, std::fabs(x));
  }
  return m;
}

}  // namespace

CHOP_TEST(parameter_tree_matches_the_manifest_ids_ranges_and_versions) {
  ChopFractalProcessor p;
  const auto& manifest = cf::host::parameterManifest();
  CHECK_EQ(p.getParameters().size(), static_cast<int>(manifest.size()));
  for (const cf::host::ParamDef& d : manifest) {
    auto* param = dynamic_cast<juce::RangedAudioParameter*>(p.apvts.getParameter(d.id));
    CHECK(param != nullptr);
    if (!param) continue;
    CHECK_EQ(param->getVersionHint(), d.sinceVersion);
    CHECK_NEAR(param->getNormalisableRange().start, d.minValue, 1e-6);
    CHECK_NEAR(param->getNormalisableRange().end, d.maxValue, 1e-6);
    CHECK_NEAR(param->convertFrom0to1(param->getDefaultValue()), d.defaultValue, 1e-5);
  }
  CHECK(p.apvts.getParameter("seed") == nullptr);
  const cf::host::ParamValues v = p.currentParams();
  CHECK_NEAR(v.get(cf::host::kDensity), 0.5, 1e-6);
  CHECK_NEAR(v.get(cf::host::kEffectEnable), 1.0, 0.0);
}

CHOP_TEST(only_matching_mono_and_stereo_bus_layouts_are_supported) {
  ChopFractalProcessor p;
  auto layout = [](juce::AudioChannelSet in, juce::AudioChannelSet out) {
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(in);
    l.outputBuses.add(out);
    return l;
  };
  CHECK(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
  CHECK(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
  CHECK(!p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())));
  CHECK(!p.isBusesLayoutSupported(layout(juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
  CHECK(p.acceptsMidi() && !p.producesMidi() && !p.isMidiEffect() && p.getTailLengthSeconds() == 0.0);
  CHECK(p.getNumPrograms() >= 1 && p.getProgramName(0).isNotEmpty());  // the VST3 validator rejects unnamed programs
}

CHOP_TEST(input_passes_through_unchanged_before_a_source_is_loaded) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  const auto out = run(p, ph, 4, 512, 0.5f);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const float expected = 0.5f * std::sin(0.05f * static_cast<float>(i % 512 + (i / 512) * 512));
    CHECK_NEAR(out[i], expected, 1e-6);
  }
}

CHOP_TEST(generated_pattern_plays_through_the_real_processor_and_stops_with_the_transport) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;
  const auto playing = run(p, ph, 200);
  CHECK(peak(playing) > 0.05f && peak(playing) < 4.f);

  ph.playing = false;
  const auto stopped = run(p, ph, 40);
  CHECK(peak(std::vector<float>(stopped.end() - 4096, stopped.end())) < 1e-6f);  // stopped transport: silence (dry mix is 0)

  // Effect disabled: the input is simply passed through, source or not.
  ph.playing = true;
  setParam(p, "effect_enable", 0.f);
  const auto bypassed = run(p, ph, 4, 512, 0.25f);
  CHECK_NEAR(bypassed[100], 0.25f * std::sin(0.05f * 100.f), 1e-6);
}

CHOP_TEST(missing_host_tempo_falls_back_visibly_and_stays_valid) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;
  ph.hasBpm = false;
  ph.hasPpq = false;  // a host that reports neither tempo nor position
  p.manualBpm = 140.0;
  const auto out = run(p, ph, 100);
  CHECK(p.usedFallbackTempo.load() && p.hostPlaying.load());
  CHECK(peak(out) > 0.05f && peak(out) < 4.f);
  ph.hasBpm = true;
  ph.hasPpq = true;
  run(p, ph, 2);
  CHECK(!p.usedFallbackTempo.load());
}

CHOP_TEST(project_state_round_trips_parameters_pattern_history_and_audio) {
  ChopFractalProcessor a;
  prepare(a);
  makeSession(a, 42, "/nonexistent/loop.wav");  // file-backed: the light save below references the file instead of embedding it
  setParam(a, "density", 0.9f);
  setParam(a, "allow_reverse", 1.f);
  a.embedSource = true;
  FakePlayHead ph;
  const auto reference = run(a, ph, 150);
  juce::MemoryBlock saved;
  a.getStateInformation(saved);
  CHECK(saved.getSize() > 100000);  // the embedded loop is inside

  ChopFractalProcessor b;
  prepare(b);
  b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  waitForRestore(b);
  CHECK_NEAR(*b.apvts.getRawParameterValue("density"), 0.9f, 1e-5);
  CHECK(*b.apvts.getRawParameterValue("allow_reverse") > 0.5f);
  bool restored = false;
  b.withSession([&](cf::composition::ProjectSession& s) { restored = s.pattern() != nullptr && !s.sourceMissing() && s.history().size() == 1; });
  CHECK(restored);
  CHECK(run(b, ph, 150) == reference);  // identical audio after a save and reload
  CHECK(b.statusMessage().isEmpty());

  // Reference-only projects are much smaller.
  a.embedSource = false;
  juce::MemoryBlock light;
  a.getStateInformation(light);
  CHECK(light.getSize() < saved.getSize() / 10);
}

CHOP_TEST(malformed_or_foreign_state_never_crashes_and_leaves_the_plugin_working) {
  ChopFractalProcessor a;
  prepare(a);
  makeSession(a);
  a.embedSource = true;
  juce::MemoryBlock good;
  a.getStateInformation(good);

  ChopFractalProcessor victim;
  prepare(victim);
  victim.setStateInformation(nullptr, 0);
  const char junk[] = "this is definitely not a ChopFractal project";
  victim.setStateInformation(junk, sizeof(junk));
  for (std::size_t cut : {std::size_t{0}, std::size_t{3}, std::size_t{9}, good.getSize() / 3, good.getSize() - 1}) victim.setStateInformation(good.getData(), static_cast<int>(cut));
  cf::Rng rng(11);
  for (int i = 0; i < 60; ++i) {  // corrupt random bytes of the real state
    juce::MemoryBlock bad = good;
    for (int k = 0; k < 3; ++k) static_cast<unsigned char*>(bad.getData())[rng.uniform(bad.getSize())] ^= static_cast<unsigned char>(1 + rng.uniform(255));
    victim.setStateInformation(bad.getData(), static_cast<int>(bad.getSize()));
  }
  FakePlayHead ph;
  CHECK(peak(run(victim, ph, 20, 512, 0.3f)) < 4.f);  // still processes audio (pass-through or a valid session)
}

CHOP_TEST(file_decoding_enforces_limits_and_reports_errors) {
  const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("chopfractal-shell-test");
  dir.createDirectory();
  const juce::File wav = dir.getChildFile("loop.wav");
  {
    wav.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> stream = wav.createOutputStream();
    CHECK(stream != nullptr);
    auto writer = fmt.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(2).withBitsPerSample(16));
    CHECK(writer != nullptr);
    if (!writer) return;
    juce::AudioBuffer<float> b(2, 44100);
    for (int i = 0; i < 44100; ++i) {
      b.setSample(0, i, 0.5f * std::sin(0.02f * static_cast<float>(i)));
      b.setSample(1, i, 0.25f);
    }
    writer->writeFromAudioSampleBuffer(b, 0, 44100);
  }
  juce::String error;
  auto data = ChopFractalProcessor::decodeFile(wav, error);
  CHECK(data != nullptr && error.isEmpty());
  if (data) {
    CHECK(data->channels == 2 && data->sampleRate == 44100 && data->frames == 44100);
    CHECK_NEAR(data->channel(1)[10], 0.25f, 1e-3);
  }
  const juce::File text = dir.getChildFile("notaudio.wav");
  text.replaceWithText("hello");
  CHECK(ChopFractalProcessor::decodeFile(text, error) == nullptr && error.isNotEmpty());
  CHECK(ChopFractalProcessor::decodeFile(dir.getChildFile("missing.wav"), error) == nullptr);

  // Loading through the async path ends with the source in the session.
  ChopFractalProcessor p;
  p.loadFileAsync(wav);
  bool loaded = false;
  for (int i = 0; i < 100 && !loaded; ++i) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    p.withSession([&](cf::composition::ProjectSession& s) { loaded = s.hasSource(); });
  }
  CHECK(loaded);
  dir.deleteRecursively();
}

CHOP_TEST(evolve_steps_the_pattern_at_the_loop_midpoint_without_touching_the_audio_thread) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  std::vector<std::uint8_t> before;
  p.withSession([&](cf::composition::ProjectSession& s) {
    before = cf::pattern::serialize(*s.pattern());
    cf::evolve::Settings es;
    es.enabled = true;
    es.everyLoops = 1;
    es.amount = 0.8;
    es.startSeed = 5;
    CHECK(s.setEvolve(es).ok());
  });
  FakePlayHead ph;
  // The pattern is 2 bars = 8 quarters; 4.5 quarters at 120 bpm / 48 kHz is about 211 blocks of 512.
  const auto first = run(p, ph, 211);
  CHECK(peak(first) > 0.05f);
  CHECK(p.playheadQuarters() >= 0.0 && p.playheadQuarters() < 8.0);
  p.pollAudioFlags();  // the message thread's timer would do this
  std::vector<std::uint8_t> after;
  p.withSession([&](cf::composition::ProjectSession& s) { after = cf::pattern::serialize(*s.pattern()); });
  CHECK(after != before);                                  // the midpoint step landed
  const auto second = run(p, ph, 100);                     // and the new pattern still plays
  CHECK(peak(second) > 0.05f);
  ph.playing = false;
  run(p, ph, 4);
  CHECK(p.playheadQuarters() < 0.0);                       // no playhead while stopped
}

CHOP_TEST(effect_parameters_reach_generation_and_the_state_round_trips) {
  ChopFractalProcessor p;
  prepare(p);
  setParam(p, "allow_filter", 1.f);
  setParam(p, "allow_glide", 1.f);
  setParam(p, "fx_intensity", 0.9f);
  const cf::host::ParamValues v = p.currentParams();
  CHECK(v.get(cf::host::kAllowFilter) == 1.0 && v.get(cf::host::kAllowGlide) == 1.0 && v.get(cf::host::kAllowCrunch) == 0.0);
  CHECK_NEAR(v.get(cf::host::kFxIntensity), 0.9, 1e-6);
  const cf::pattern::Settings s = cf::composition::settingsFromParams(v, cf::pattern::Settings{});
  CHECK(s.allowFilter && s.allowGlide && !s.allowCrunch);
  juce::MemoryBlock blob;
  p.getStateInformation(blob);
  ChopFractalProcessor q;
  q.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
  waitForRestore(q);
  CHECK_NEAR(q.currentParams().get(cf::host::kFxIntensity), 0.9, 1e-6);
  CHECK(q.currentParams().get(cf::host::kAllowFilter) == 1.0);
}

CHOP_TEST(embed_flag_and_manual_tempo_survive_a_save_and_reload) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p, 42, "/nonexistent/loop.wav");
  p.embedSource = true;
  p.manualBpm = 97.5;
  juce::MemoryBlock blob;
  p.getStateInformation(blob);
  ChopFractalProcessor q;
  q.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
  waitForRestore(q);
  CHECK(q.embedSource.load());                                  // a re-save keeps the audio inside the project
  CHECK_NEAR(q.manualBpm.load(), 97.5, 1e-9);
  juce::MemoryBlock again;
  q.getStateInformation(again);
  CHECK(again.getSize() > 90000);                               // the second save still embeds the audio
  p.embedSource = false;
  juce::MemoryBlock ref;
  p.getStateInformation(ref);
  ChopFractalProcessor r;
  r.setStateInformation(ref.getData(), static_cast<int>(ref.getSize()));
  waitForRestore(r);
  CHECK(!r.embedSource.load());
}

CHOP_TEST(editor_constructs_draws_and_reflects_the_session_when_a_display_exists) {
  if (!haveDisplay()) return;  // CI without a display skips the GUI smoke test
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->setVisible(true);
  // CHOPFRACTAL_SNAPSHOT_SIZE=880x560 and CHOPFRACTAL_THEME=concrete render the other sizes and the daylight theme;
  // CHOPFRACTAL_SNAPSHOT_DRAWER=tools shows a drawer.
  if (const char* size = std::getenv("CHOPFRACTAL_SNAPSHOT_SIZE")) {
    int w = 0, h = 0;
    if (std::sscanf(size, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) editor->setSize(w, h);
  }
  if (auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get())) {
    if (const char* theme = std::getenv("CHOPFRACTAL_THEME")) ed->setTheme(std::string(theme) == "concrete" ? cdl::ThemeId::Concrete : cdl::ThemeId::Night);
    if (const char* drawer = std::getenv("CHOPFRACTAL_SNAPSHOT_DRAWER")) ed->openDrawer(drawer);
    if (std::getenv("CHOPFRACTAL_SNAPSHOT_ADVANCED")) ed->setAdvanced(true);
  }
  juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
  juce::Image img = editor->createComponentSnapshot(editor->getLocalBounds());
  CHECK(img.isValid() && img.getWidth() == editor->getWidth());
  cdl::settingsFile().deleteFile();  // a snapshot at another size or theme must not leak into later tests
  cdl::setCurrentTheme(cdl::ThemeId::Night);
  if (const char* out = std::getenv("CHOPFRACTAL_SNAPSHOT")) {
    juce::File f(out);
    f.deleteFile();
    juce::FileOutputStream fs(f);
    juce::PNGImageFormat().writeImageToStream(img, fs);
  }
}

// Drives the real editor's buttons the way a user would and checks the session reacts.
CHOP_TEST(editor_buttons_drive_the_session) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.loadSource(drumLoop(), "loop", "").ok()); });
  std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
  CHECK(ed != nullptr);
  if (!ed) return;
  ed->setVisible(true);
  auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };
  auto click = [&](const char* text) {
    juce::Button* b = findButton(*ed, text);
    CHECK(b != nullptr);
    if (b && b->isEnabled()) b->triggerClick();
    pump();
    return b != nullptr;
  };
  auto hasPattern = [&] { return !patternBytes(p).empty(); };

  click("Detect Chops");
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.chops() && s.chops()->chops.size() >= 1); });
  click("Mutate");                                   // refused without a pattern: no crash, no pattern
  CHECK(!hasPattern());
  click("Generate");
  CHECK(hasPattern());
  const auto g = patternBytes(p);
  click("Mutate");
  const auto m = patternBytes(p);
  CHECK(m != g);
  click("Undo");
  CHECK(patternBytes(p) == g);
  click("Redo");
  CHECK(patternBytes(p) == m);
  click("Store A");
  click("Fractal");
  const auto f = patternBytes(p);
  CHECK(f != m);
  click("Recall A");
  CHECK(patternBytes(p) == m);
  click("Lock Bar");  // nothing selected: refused, no bar is locked
  p.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& b : s.pattern()->bars) CHECK(!b.locked);
  });
  CHECK(p.statusMessage().contains("Select a hit"));
  click("Keep");
  click("Smart Setup");
  if (auto* e = dynamic_cast<juce::Button*>(findButton(*ed, "Evolve"))) {
    e->setToggleState(true, juce::sendNotificationSync);
    pump();
    bool running = false;
    p.withSession([&](cf::composition::ProjectSession& s) { running = s.evolveRunning(); });
    CHECK(running);
    e->setToggleState(false, juce::sendNotificationSync);
    pump();
    p.withSession([&](cf::composition::ProjectSession& s) { running = s.evolveRunning(); });
    CHECK(!running);
  } else {
    CHECK(false);
  }
  // Selecting a hit then Zoom In / Collapse (no selection: refused politely, nothing breaks).
  click("Zoom In");
  click("Collapse");
  click("Set Role");
  CHECK(hasPattern());
  // Every visible button must have a title for accessibility.
  forEachControl(*ed, [](juce::Component& c) {
    if (auto* b = dynamic_cast<juce::Button*>(&c)) CHECK(b->getTitle().isNotEmpty() || b->getButtonText().isNotEmpty());
  });
}

namespace {
std::size_t hitCount(ChopFractalProcessor& p) {
  std::size_t n = 0;
  p.withSession([&](cf::composition::ProjectSession& s) {
    if (s.pattern() && s.chops()) {
      auto f = cf::pattern::flatten(*s.pattern(), *s.chops());
      if (f.ok()) n = f.value().size();
    }
  });
  return n;
}
juce::Point<int> hitPoint(ChopFractalEditor& ed, const cf::ui::EventRect& r, double fraction = 0.5) { return hitPointOf(ed, r, fraction); }
}  // namespace

// Pass 3: chaos reroll, presets, drag-out files, and direct hit editing through real mouse events.
CHOP_TEST(pass3_chaos_presets_dragout_and_hit_editing_work_through_the_editor) {
  if (!haveDisplay()) return;
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_pass3_" + juce::String(juce::Random::getSystemRandom().nextInt()));
  PresetManager::setFolderOverride(tmp.getChildFile("presets"));
  dragout::setFolderOverride(tmp.getChildFile("drag"));
  ChopFractalProcessor p;
  prepare(p);
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.loadSource(drumLoop(), "loop", "").ok());
    cf::composition::DetectOptions d;
    d.analysis.sensitivity = 0.8;
    CHECK(s.applyChops(d).ok());
  });
  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto* ed = dynamic_cast<ChopFractalEditor*>(base.get());
  CHECK(ed != nullptr);
  if (!ed) return;
  ed->setVisible(true);
  auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };
  pump();

  // Reroll generates a pattern and moves the seed.
  ed->doReroll();
  CHECK(!patternBytes(p).empty());
  const auto first = patternBytes(p);
  ed->doReroll();
  CHECK(patternBytes(p) != first);

  // Presets: factory loads set the controls; a saved user preset round-trips and can be deleted.
  const auto list = PresetManager::list();
  CHECK(list.size() >= 6);
  ed->applyPreset(list[1].preset);
  CHECK(std::fabs(p.currentParams().get(cf::host::kDensity) - list[1].preset.density) < 0.01);
  setParam(p, "chaos", 0.8f);
  ed->savePresetNamed("Test One", false);
  CHECK(PresetManager::list().size() == list.size() + 1);
  ed->savePresetNamed("Test One", false);  // refused: exists
  CHECK(p.statusMessage().contains("already exists"));
  ed->savePresetNamed("Test One", true);
  CHECK(PresetManager::list().size() == list.size() + 1);
  setParam(p, "chaos", 0.0f);
  const auto after = PresetManager::list();
  ed->applyPreset(after.back().preset);
  CHECK(std::fabs(p.currentParams().get(cf::host::kChaos) - 0.8) < 0.01);
  // A corrupt file in the folder is skipped, not fatal.
  CHECK(tmp.getChildFile("presets").getChildFile("bad.cfpreset").replaceWithText("garbage"));
  int skipped = 0;
  CHECK(PresetManager::list(&skipped).size() == after.size());
  CHECK(skipped == 1);
  CHECK(PresetManager::remove(after.back().file).ok());
  CHECK(!PresetManager::remove(tmp.getChildFile("outside.cfpreset")).ok());

  // Drag-out files are real MIDI / WAV.
  const juce::File mid = ed->makeDragFile(false);
  CHECK(mid.existsAsFile());
  juce::MemoryBlock mb;
  mid.loadFileAsData(mb);
  CHECK(mb.getSize() > 14 && std::memcmp(mb.getData(), "MThd", 4) == 0);
  const juce::File wav = ed->makeDragFile(true);
  mb.reset();
  wav.loadFileAsData(mb);
  CHECK(mb.getSize() > 44 && std::memcmp(mb.getData(), "RIFF", 4) == 0);

  // Direct hit editing uses the explicit move/resize mode; plain drags select sections.
  if (auto* button = findButton(*ed, "Move / resize hits")) button->setToggleState(true, juce::sendNotificationSync);
  else CHECK(false);
  MouseSim sim(*ed);
  // Direct hit editing with real mouse events.
  ed->doReroll();
  pump();
  const auto before = patternBytes(p);
  const std::size_t hits = hitCount(p);
  const cf::ui::EventRect* target = nullptr;
  for (const auto& r : ed->hitRects())
    if (r.depth == 0 && !r.locked && (r.x1 - r.x0) * ed->patternBounds().getWidth() > 24) {
      // Skip hits that another rectangle of the same lane overlaps at the click point: the topmost one would be hit.
      bool covered = false;
      const double px = r.x0 + (r.x1 - r.x0) * 0.3;
      for (const auto& o : ed->hitRects())
        if (!(o.id == r.id) && o.chopIndex == r.chopIndex && o.x0 <= px + 0.002 && o.x1 >= px - 0.002) covered = true;
      if (covered) continue;
      target = &r;
      break;
    }
  CHECK(target != nullptr);
  if (!target) return;
  const cf::EventId tid = target->id;
  const auto start = hitPoint(*ed, *target, 0.3);
  sim.down(start);
  CHECK(ed->selectedHit() == tid);
  sim.drag(start + juce::Point<int>(80, 0));
  sim.up(start + juce::Point<int>(80, 0));
  pump();
  CHECK(patternBytes(p) != before);
  CHECK(hitCount(p) == hits);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.undo()); });
  juce::MessageManager::getInstance()->runDispatchLoopUntil(250);  // the editor timer runs at 10 Hz
  CHECK(patternBytes(p) == before);

  // A tiny movement is a click, not an edit.
  sim.down(start);
  sim.up(start + juce::Point<int>(1, 0));
  CHECK(patternBytes(p) == before);

  // Delete removes the selected hit (one undo step), M mutes it.
  sim.click(start);
  CHECK(ed->keyPressed(juce::KeyPress('m', juce::ModifierKeys(), 'm')));
  CHECK(patternBytes(p) != before);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.undo()); });
  juce::MessageManager::getInstance()->runDispatchLoopUntil(250);  // the editor timer runs at 10 Hz
  sim.down(start);
  CHECK(ed->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
  CHECK(patternBytes(p) != before);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.undo()); });
  juce::MessageManager::getInstance()->runDispatchLoopUntil(250);  // the editor timer runs at 10 Hz
  CHECK(patternBytes(p) == before);

  // Shift-click on an empty cell adds a hit; a plain click there selects nothing and changes nothing.
  const auto b = ed->patternBounds();
  bool added = false;
  for (int lane = 0; lane < ed->laneCount() && !added; ++lane)
    for (int px = 4; px < b.getWidth() && !added; px += 16) {
      const double fx = static_cast<double>(px) / b.getWidth();
      bool covered = false;
      for (const auto& r : ed->hitRects()) covered = covered || (r.chopIndex == lane && fx >= r.x0 - 0.02 && fx <= r.x1 + 0.02);
      if (covered) continue;
      const juce::Point<int> pt(b.getX() + px, b.getY() + static_cast<int>((static_cast<float>(lane) + 0.5f) * b.getHeight() / static_cast<float>(ed->laneCount())));
      sim.down(pt);
      CHECK(patternBytes(p) == before);
      sim.up(pt);
      sim.down(pt, juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
      sim.up(pt);
      pump();
      added = hitCount(p) == hits + 1;
    }
  CHECK(added);
  tmp.deleteRecursively();
  PresetManager::setFolderOverride(juce::File());
  dragout::setFolderOverride(juce::File());
}

// ---------------------------------------------------------------------------------------------------------
// Randomized QA: a fast, clumsy user (random clicks, drags, modifiers, keys, buttons, knobs) while audio runs.
// Fixed seeds keep it reproducible; CF_FUZZ_SEED / CF_FUZZ_EVENTS override them for exploratory runs.
// Right-clicks are left out on purpose: they open native popup menus, which need a window manager.
// ---------------------------------------------------------------------------------------------------------
namespace {

struct FuzzResult {
  std::vector<std::uint8_t> pattern;
  std::size_t historyNodes = 0;
  std::size_t audioChecks = 0;
};

FuzzResult fuzzEditor(std::uint64_t seed, int events, bool* ok) {
  FuzzResult out;
  PresetManager::setFolderOverride(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_fuzz_presets"));
  dragout::setFolderOverride(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_fuzz_drag"));
  cdl::settingsFile().deleteFile();  // the editor saves its size, theme and mode on close: every replay must start from the defaults
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto* ed = dynamic_cast<ChopFractalEditor*>(base.get());
  if (!ed) {
    *ok = false;
    return out;
  }
  ed->setVisible(true);
  cf::Rng rng(seed);
  auto pick = [&](int n) { return std::min(n - 1, static_cast<int>(rng.uniform01() * static_cast<double>(n))); };
  const int w = ed->getWidth(), h = ed->getHeight();
  // "Reroll" and "New Seed" draw system randomness, so they stay out of the reproducible run.
  // Detect Chops opens an asynchronous call-out box (covered by test_editing_gui.cpp), which would make replays timing dependent.
  static const char* const kButtons[] = {"Generate", "Mutate", "Undo", "Redo", "Zoom In", "Collapse", "Lock Bar", "Fractal", "Store A", "Recall A", "Store B",
                                         "Recall B", "Keep", "Smart Setup", "Accept Roles", "Use Tempo", "Generate", "Set Role", "Play", "Stop"};
  static const char* const kParams[] = {"density", "swing", "variation_amount", "fx_intensity", "chaos", "dry_mix", "pattern_bars", "allow_reverse",
                                        "allow_pitch", "allow_retrigger", "allow_filter", "allow_glide", "allow_crunch", "effect_enable"};
  FakePlayHead ph;
  p.setPlayHead(&ph);
  for (int i = 0; i < events; ++i) {
    ed->model().refresh(false, ed->waveformBounds().getWidth());  // the view must not depend on how many timer ticks the dispatch loop happened to run
    switch (pick(6)) {
      case 0:
      case 1:
      case 2: {
        const juce::Point<int> a(pick(w), pick(h));
        const juce::Point<int> b(juce::jlimit(0, w - 1, a.x + pick(401) - 200), juce::jlimit(0, h - 1, a.y + pick(161) - 80));
        int m = 0;
        if (rng.uniform01() < 0.25) m |= juce::ModifierKeys::shiftModifier;
        if (rng.uniform01() < 0.15) m |= juce::ModifierKeys::altModifier;
        const bool drag = rng.uniform01() < 0.5;
        MouseSim sim(*ed);
        sim.down(a, juce::ModifierKeys(m));
        if (drag) sim.drag(b, juce::ModifierKeys(m));
        sim.up(drag ? b : a, juce::ModifierKeys(m));
        break;
      }
      case 3: {
        const juce::KeyPress keys[] = {juce::KeyPress(juce::KeyPress::deleteKey), juce::KeyPress(juce::KeyPress::leftKey), juce::KeyPress(juce::KeyPress::rightKey),
                                       juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 0),
                                       juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 0),
                                       juce::KeyPress('m', juce::ModifierKeys(), 'm'), juce::KeyPress('d', juce::ModifierKeys(), 'd'), juce::KeyPress(juce::KeyPress::backspaceKey)};
        ed->keyPressed(keys[pick(8)]);
        break;
      }
      case 4:
        if (auto* b = findButton(*ed, kButtons[pick(18)]))
          if (b->isEnabled()) b->triggerClick();
        break;
      default:
        setParam(p, kParams[pick(14)], static_cast<float>(rng.uniform01()));
        break;
    }
    if (i % 25 == 0) {
      juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
      ph.playing = rng.uniform01() < 0.8;
      ph.hasPpq = rng.uniform01() < 0.8;
      ph.hasBpm = rng.uniform01() < 0.9;
      const auto audio = run(p, ph, 4, 512, 0.1f);
      if (!(peak(audio) < 20.f)) {  // finite and sane
        *ok = false;
        std::fprintf(stderr, "  audio not finite/sane at event %d (peak %g)\n", i, static_cast<double>(peak(audio)));
      }
      ++out.audioChecks;
    }
    if (i % 100 == 0) {
      cf::Status st;
      p.withSession([&](cf::composition::ProjectSession& s) { st = s.pattern() ? cf::pattern::validate(*s.pattern()) : cf::Status{}; });
      if (!st.ok()) {
        *ok = false;
        std::fprintf(stderr, "  pattern invalid at event %d: %s\n", i, st.error().message.c_str());
      }
    }
  }
  p.setPlayHead(nullptr);
  juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
  out.pattern = patternBytes(p);
  p.withSession([&](cf::composition::ProjectSession& s) { out.historyNodes = s.history().size(); });

  // Extra check 1 - saving after abuse still works and restores the identical project.
  juce::MemoryBlock mb;
  p.getStateInformation(mb);
  ChopFractalProcessor q;
  prepare(q);
  q.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
  waitForRestore(q);
  if (patternBytes(q) != out.pattern) {
    *ok = false;
    std::fprintf(stderr, "  state round trip changed the pattern\n");
  }

  // Extra check 2 - the undo stack is intact: from the newest version, unwinding everything and replaying it lands
  // on the same pattern (the random user may have ended mid-stack, so first move to the tip).
  int undone = 0, redone = 0;
  std::vector<std::uint8_t> tip;
  p.withSession([&](cf::composition::ProjectSession& s) {
    for (int n = 0; n < 500 && s.redo(); ++n) {
    }
    tip = s.pattern() ? cf::pattern::serialize(*s.pattern()) : std::vector<std::uint8_t>();
    while (undone < 500 && s.undo()) ++undone;
    while (redone < 500 && s.redo()) ++redone;
  });
  if (undone != redone || patternBytes(p) != tip) {
    *ok = false;
    std::fprintf(stderr, "  undo/redo unwind mismatch: undone %d redone %d same=%d\n", undone, redone, patternBytes(p) == tip);
  }

  PresetManager::setFolderOverride(juce::File());
  dragout::setFolderOverride(juce::File());
  return out;
}

int fuzzEvents(int fallback) { return std::getenv("CF_FUZZ_EVENTS") ? std::max(100, std::atoi(std::getenv("CF_FUZZ_EVENTS"))) : fallback; }

}  // namespace

CHOP_TEST(fuzz_random_editor_and_audio_input_never_breaks_the_plugin) {
  if (!haveDisplay()) return;
  std::vector<std::uint64_t> seeds = {1, 2, 3, 4, 5, 6};
  if (const char* s = std::getenv("CF_FUZZ_SEED")) seeds = {static_cast<std::uint64_t>(std::atoll(s))};
  for (std::uint64_t seed : seeds) {
    bool ok = true;
    const FuzzResult r = fuzzEditor(seed, fuzzEvents(5000), &ok);
    if (!ok) std::fprintf(stderr, "fuzz seed %llu failed\n", static_cast<unsigned long long>(seed));
    CHECK(ok);
    CHECK(r.audioChecks > 50);
    CHECK(r.historyNodes <= 64);  // the family tree respects its cap however much the user abuses it
  }
}

// Extra check 3 - the same random session replays to byte-identical results (no hidden time or address dependence).
CHOP_TEST(fuzz_session_replays_deterministically) {
  if (!haveDisplay()) return;
  for (std::uint64_t seed : {11ull, 12ull}) {
    bool okA = true, okB = true;
    const FuzzResult a = fuzzEditor(seed, fuzzEvents(2500), &okA);
    const FuzzResult b = fuzzEditor(seed, fuzzEvents(2500), &okB);
    CHECK(okA && okB);
    CHECK(a.pattern == b.pattern);
    CHECK(a.historyNodes == b.historyNodes);
  }
}

// Extra check 4 - hostile host timing: odd block sizes, absurd or missing tempo, non-monotonic or NaN positions.
CHOP_TEST(audio_stays_finite_under_hostile_host_timing_and_block_sizes) {
  ChopFractalProcessor p;
  prepare(p, 512);
  makeSession(p, 77);
  setParam(p, "allow_reverse", 1.f);
  setParam(p, "allow_glide", 1.f);
  setParam(p, "allow_crunch", 1.f);
  setParam(p, "allow_filter", 1.f);
  cf::Rng rng(99);
  FakePlayHead ph;
  p.setPlayHead(&ph);
  const double bpms[] = {120.0, 0.0, -5.0, 1e-9, 20.0, 999.0, 5000.0, 1e12, std::nan("")};
  const double ppqs[] = {0.0, -100.0, 1e9, 3.5, std::nan(""), 7.0, 0.0001};
  const int blocks[] = {1, 2, 7, 64, 511, 512, 1000, 4096, 9001};
  bool finite = true;
  for (int i = 0; i < 400; ++i) {
    ph.playing = rng.uniform01() < 0.85;
    ph.hasBpm = rng.uniform01() < 0.8;
    ph.hasPpq = rng.uniform01() < 0.8;
    ph.bpm = bpms[std::min(8, static_cast<int>(rng.uniform01() * 9.0))];
    ph.ppq = ppqs[std::min(6, static_cast<int>(rng.uniform01() * 7.0))];
    const int block = blocks[std::min(8, static_cast<int>(rng.uniform01() * 9.0))];
    if (i % 40 == 0) setParam(p, "pattern_bars", static_cast<float>(i % 4));
    juce::AudioBuffer<float> buf(2, block);
    for (int c = 0; c < 2; ++c)
      for (int n = 0; n < block; ++n) buf.setSample(c, n, 0.2f * std::sin(0.03f * static_cast<float>(n)));
    juce::MidiBuffer midi;
    p.setPlayHead(&ph);
    p.processBlock(buf, midi);
    for (int c = 0; c < 2; ++c)
      for (int n = 0; n < block; ++n) {
        const float v = buf.getSample(c, n);
        if (!std::isfinite(v) || std::fabs(v) > 20.f) finite = false;
      }
    if (i % 50 == 0) p.pollAudioFlags();  // loop/midpoint bookkeeping must cope with the same garbage
  }
  p.setPlayHead(nullptr);
  CHECK(finite);
}

CHOP_TEST(version_stamp_matches_VERSION_file) {
  std::ifstream in(CHOPFRACTAL_VERSION_FILE);
  CHECK(in.good());
  std::string line;
  std::getline(in, line);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  CHECK(!line.empty());
  CHECK(line == cfbuild::kVersionFull);
  CHECK(line.rfind(cfbuild::kVersionNum, 0) == 0);  // numeric part is the prefix of the full version
}

int main() {
  // The editor remembers theme and size per user; keep the tests away from the real profile.
  cdl::setSettingsFileOverride(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("chopfractal_shell_tests_ui.json"));
  const bool display = haveDisplay();
  std::unique_ptr<juce::ScopedJuceInitialiser_GUI> gui;
  if (display) gui = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
  juce::MessageManager::getInstance();
  if (const char* only = std::getenv("CHOP_TEST_FILTER")) {  // run just the cases whose name contains the text
    auto& cases = chop_test::registry();
    cases.erase(std::remove_if(cases.begin(), cases.end(), [only](const chop_test::Case& c) { return std::string(c.name).find(only) == std::string::npos; }), cases.end());
  }
  const int rc = chop_test::runAll();
  gui.reset();
  juce::DeletedAtShutdown::deleteAll();
  juce::MessageManager::deleteInstance();
  return rc;
}

CHOP_TEST(local_transport_plays_without_a_host_and_restarts_at_zero) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  auto render = [&](int blocks) {
    std::vector<float> audio;
    juce::MidiBuffer midi;
    for (int n = 0; n < blocks; ++n) {
      juce::AudioBuffer<float> b(2, 512);
      b.clear();
      p.processBlock(b, midi);
      audio.insert(audio.end(), b.getReadPointer(0), b.getReadPointer(0) + 512);
    }
    return audio;
  };
  CHECK(peak(render(8)) < 1e-6f);
  CHECK(p.startPlayback());
  const auto first = render(100);
  CHECK(peak(first) > 0.05f);
  CHECK(p.hostPlaying.load());
  p.stopPlayback();
  CHECK(peak(render(8)) < 1e-6f);
  CHECK(!p.hostPlaying.load());
  CHECK(p.playheadQuarters() < 0.0);
  CHECK(p.startPlayback());
  CHECK(render(100) == first);
}

CHOP_TEST(local_transport_reports_missing_source_and_overrides_a_stopped_host) {
  ChopFractalProcessor p;
  prepare(p);
  CHECK(!p.startPlayback());
  CHECK(p.statusMessage().containsIgnoreCase("load"));
  makeSession(p);
  FakePlayHead ph;
  ph.playing = false;
  p.setPlayHead(&ph);
  CHECK(p.startPlayback());
  const auto audio = run(p, ph, 100);
  CHECK(peak(audio) > 0.05f);
  p.stopPlayback();
  ph.playing = true;
  CHECK(peak(run(p, ph, 8)) < 1e-6f);
}

CHOP_TEST(editor_has_visible_play_and_stop_controls_in_the_header) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  ChopFractalEditor editor(p);
  auto* play = dynamic_cast<juce::TextButton*>(findButton(editor, "Play"));
  auto* stop = dynamic_cast<juce::TextButton*>(findButton(editor, "Stop"));
  CHECK(play != nullptr && stop != nullptr);
  if (!play || !stop) return;
  CHECK(play->isVisible() && stop->isVisible());
  // The transport lives in the header strip: both buttons are sized to be hit, apart from each other, and inside the strip.
  CHECK(play->getWidth() >= 44 && stop->getWidth() >= 44 && play->getHeight() >= 24);
  CHECK(!editor.getLocalArea(play->getParentComponent(), play->getBounds()).intersects(editor.getLocalArea(stop->getParentComponent(), stop->getBounds())));
  CHECK(play->getParentComponent() == stop->getParentComponent() && play->getParentComponent()->getComponentID() == "TopBar");
  CHECK(play->getParentComponent()->getLocalBounds().contains(play->getBounds()) && play->getParentComponent()->getLocalBounds().contains(stop->getBounds()));
  CHECK(!play->getToggleState());
  play->onClick();
  juce::AudioBuffer<float> b(2, 512);
  juce::MidiBuffer midi;
  b.clear();
  p.processBlock(b, midi);
  CHECK(p.hostPlaying.load());
  stop->onClick();
  b.clear();
  p.processBlock(b, midi);
  CHECK(!p.hostPlaying.load());
}

CHOP_TEST(status_area_preserves_long_messages_in_a_scrollable_text_control) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  const juce::String message = juce::String::repeatedString("A long status message. ", 200);
  p.setStatusMessage(message);
  ChopFractalEditor editor(p);
  juce::TextEditor* status = findTextEditor(editor, "Status messages");
  CHECK(status != nullptr);
  if (!status) return;
  CHECK(status->isReadOnly());
  CHECK(status->isMultiLine());
  CHECK(status->getText().startsWith(message));
  CHECK(status->getHeight() >= 20);
  CHECK(editor.getLocalBounds().contains(editor.getLocalArea(status, status->getLocalBounds())));
}

CHOP_TEST(drag_selection_selects_multiple_hits_without_changing_the_pattern_and_zoom_works) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  ChopFractalEditor editor(p);
  const auto before = patternBytes(p);
  const auto bounds = editor.patternBounds();
  const auto end = bounds.getTopLeft() + juce::Point<int>(1, 1);
  MouseSim sim(editor);
  const auto start = sim.emptyPatternSpot(editor);  // the press must land on empty grid, otherwise it drags a hit
  sim.dragFromTo(start, end);
  CHECK(editor.selectedHits().size() > 1);
  CHECK(patternBytes(p) == before);
  // A single hit can be selected by a simple click, and Zoom creates nested events.
  const auto hits = editor.hitRects();
  if (hits.empty()) return;
  const auto point = hitPoint(editor, hits.front(), 0.5);
  sim.click(point);
  CHECK(editor.selectedHit().valid());
  if (auto* button = findButton(editor, "Zoom In")) button->onClick();
  CHECK(patternBytes(p) != before);
  CHECK(std::any_of(editor.hitRects().begin(), editor.hitRects().end(), [](const auto& hit) { return hit.depth > 0; }));
}

CHOP_TEST(waveform_drag_selects_multiple_source_sections) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  ChopFractalEditor editor(p);
  const auto before = patternBytes(p);
  const auto bounds = editor.waveformBounds();
  const auto start = bounds.getTopLeft() + juce::Point<int>(20, bounds.getHeight() / 2);  // clear of the marker at frame 0
  const auto end = bounds.getTopRight() + juce::Point<int>(-1, bounds.getHeight() / 2);
  MouseSim(editor).dragFromTo(start, end);
  CHECK(editor.selectedChops().size() > 1);
  CHECK(editor.selectedHits().size() > 1);
  CHECK(patternBytes(p) == before);
}

CHOP_TEST(editor_accepts_dropped_audio_and_rejects_unrelated_files) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  ChopFractalEditor editor(p);
  CHECK(editor.isInterestedInFileDrag(juce::StringArray{"/tmp/loop.wav"}));
  CHECK(editor.isInterestedInFileDrag(juce::StringArray{"/tmp/loop.AIFF"}));
  CHECK(!editor.isInterestedInFileDrag(juce::StringArray{"/tmp/readme.txt"}));
  editor.filesDropped(juce::StringArray{"/tmp/readme.txt"}, 0, 0);
  CHECK(p.statusMessage().containsIgnoreCase("drop an audio"));
}

CHOP_TEST(dropped_wav_is_loaded_by_the_async_source_loader) {
  if (!haveDisplay()) return;
  const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getNonexistentChildFile("cf_drop", ".wav");
  juce::WavAudioFormat format;
  auto stream = file.createOutputStream();
  CHECK(stream != nullptr);
  if (!stream) return;
  std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 48000, 1, 16, {}, 0));
  CHECK(writer != nullptr);
  if (!writer) return;
  stream.release();
  juce::AudioBuffer<float> samples(1, 4800);
  for (int i = 0; i < samples.getNumSamples(); ++i) samples.setSample(0, i, 0.4f * std::sin(0.05f * i));
  CHECK(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()));
  writer.reset();
  {
    ChopFractalProcessor p;
    ChopFractalEditor editor(p);
    editor.filesDropped(juce::StringArray{file.getFullPathName()}, 10, 10);
    bool loaded = false;
    for (int i = 0; i < 100 && !loaded; ++i) {
      juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
      p.withSession([&](cf::composition::ProjectSession& s) {
        loaded = s.playback() && s.playback()->source && s.playback()->source->frames == 4800;
      });
    }
    CHECK(loaded);
  }
  file.deleteFile();
}

CHOP_TEST(four_essential_workflows_then_35_seconds_of_randomized_qa) {
  if (!haveDisplay()) return;
  const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getNonexistentChildFile("cf_essential_qa", ".wav");
  const auto source = drumLoop();
  {
    juce::WavAudioFormat format;
    auto stream = file.createOutputStream();
    CHECK(stream != nullptr);
    if (!stream) return;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 48000, 1, 16, {}, 0));
    CHECK(writer != nullptr);
    if (!writer) return;
    stream.release();
    juce::AudioBuffer<float> audio(1, static_cast<int>(source->frames));
    audio.copyFrom(0, 0, source->channel(0), audio.getNumSamples());
    CHECK(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()));
  }
  {
    ChopFractalProcessor p;
    prepare(p);
    ChopFractalEditor editor(p);
    // 1. A real file dropped into the editor loads through the asynchronous decoder.
    editor.filesDropped(juce::StringArray{file.getFullPathName()}, 0, 0);
    bool loaded = false;
    for (int i = 0; i < 150 && !loaded; ++i) {
      juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
      p.withSession([&](cf::composition::ProjectSession& s) {
        loaded = s.playback() && s.playback()->source && s.playback()->source->frames == source->frames;
      });
    }
    CHECK(loaded);
    if (!loaded) return;
    if (auto* button = findButton(editor, "Detect Chops")) button->onClick();
    editor.doReroll();

    // 2. Standalone Play produces audio and Stop produces silence without a DAW.
    auto render = [&](int blocks) {
      std::vector<float> out;
      juce::MidiBuffer midi;
      for (int i = 0; i < blocks; ++i) {
        juce::AudioBuffer<float> audio(2, 512);
        audio.clear();
        p.processBlock(audio, midi);
        out.insert(out.end(), audio.getReadPointer(0), audio.getReadPointer(0) + 512);
      }
      return out;
    };
    CHECK(p.startPlayback());
    CHECK(peak(render(400)) > 0.01f);
    p.stopPlayback();
    CHECK(peak(render(10)) < 1e-6f);

    // 3. Drag selects multiple sections without editing; Zoom subdivides a selected hit.
    auto area = editor.patternBounds();
    auto b = area.getTopLeft() + juce::Point<int>(1, 1);
    const auto unchanged = patternBytes(p);
    MouseSim sim(editor);
    auto a = sim.emptyPatternSpot(editor);
    sim.dragFromTo(a, b);
    CHECK(editor.selectedHits().size() > 1);
    CHECK(patternBytes(p) == unchanged);
    const auto rects = editor.hitRects();
    CHECK(!rects.empty());
    if (rects.empty()) return;
    // Not every hit can be subdivided (short ones are refused with a status message): zoom the first one that can.
    bool zoomed = false;
    for (const auto& r : rects) {
      sim.click(hitPoint(editor, r, 0.5));
      if (!editor.selectedHit().valid()) continue;
      if (auto* button = findButton(editor, "Zoom In")) button->onClick();
      if (patternBytes(p) != unchanged) {
        zoomed = true;
        break;
      }
    }
    CHECK(zoomed);

    // 4. Saving and restoring preserves the pattern and embedded audio.
    p.embedSource = true;
    juce::MemoryBlock state;
    p.getStateInformation(state);
    ChopFractalProcessor restored;
    prepare(restored);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    waitForRestore(restored);
    CHECK(patternBytes(restored) == patternBytes(p));
    CHECK(restored.startPlayback());
    FakePlayHead stoppedHost;
    stoppedHost.playing = false;
    CHECK(peak(run(restored, stoppedHost, 400)) > 0.01f);
  }
  file.deleteFile();

  // Real time, not 35 seconds of simulated audio: random edits/controls run concurrently
  // with audio processing. Existing fuzz checks cover finite audio, valid patterns,
  // bounded history, state round trips and undo/redo. Seeds are printed for reproduction.
  const double start = juce::Time::getMillisecondCounterHiRes();
  std::uint64_t seed = 35000;
  std::size_t sessions = 0, audioChecks = 0;
  do {
    bool ok = true;
    const auto result = fuzzEditor(seed, 200, &ok);
    CHECK(ok);
    audioChecks += result.audioChecks;
    ++sessions;
    ++seed;
  } while (juce::Time::getMillisecondCounterHiRes() - start < 35000.0);
  CHECK(sessions > 0 && audioChecks > 0);
  std::fprintf(stdout, "35-second QA: %.2f s, %zu sessions, %zu audio checks, seeds 35000..%llu\n",
      (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, sessions, audioChecks,
      static_cast<unsigned long long>(seed - 1));
}

CHOP_TEST(non_obvious_editor_controls_have_hover_help) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  ChopFractalEditor editor(p);
  int controls = 0;
  forEachControl(editor, [&](juce::Component& component) {
    if (!(dynamic_cast<juce::Button*>(&component) || dynamic_cast<juce::Slider*>(&component) ||
          dynamic_cast<juce::ComboBox*>(&component) || dynamic_cast<juce::TextEditor*>(&component))) return;
    auto* tooltip = dynamic_cast<juce::TooltipClient*>(&component);
    CHECK(tooltip != nullptr);
    const std::size_t need = component.isEnabled() ? 20 : 8;  // a disabled control may carry just the short reason it is off
    if (tooltip && tooltip->getTooltip().length() <= need) std::fprintf(stderr, "  no hover help: %s '%s' %s in %s tip='%s'\n", component.getTitle().toRawUTF8(), component.getComponentID().toRawUTF8(), typeid(component).name(),
                                                                     component.getParentComponent() ? component.getParentComponent()->getComponentID().toRawUTF8() : "-", tooltip->getTooltip().toRawUTF8());
    if (tooltip) CHECK(tooltip->getTooltip().length() > need);
    ++controls;
  });
  CHECK(controls > 35);
  CHECK(editor.getTooltip().length() > 20);
}

CHOP_TEST(local_preview_can_return_to_the_daw_transport) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead host;
  host.playing = false;
  CHECK(p.startPlayback());
  CHECK(peak(run(p, host, 100)) > 0.05f);
  p.stopPlayback();
  host.playing = true;
  CHECK(peak(run(p, host, 10)) < 1e-6f);
  p.followHostTransport();
  CHECK(peak(run(p, host, 100)) > 0.05f);
  CHECK(!p.usedFallbackTempo.load());
  host.playing = false;
  const auto stopped = run(p, host, 40);
  CHECK(peak(std::vector<float>(stopped.end() - 4096, stopped.end())) < 1e-6f);
}
