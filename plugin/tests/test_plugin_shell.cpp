// Headless tests of the real JUCE processor: no host and no window, but the real parameter tree, host-time
// translation, audio path, project-state round trip and file decoding.
#include <functional>
#include <string>
#include <algorithm>
#include <memory>
#include <chopfractal/chop_contracts/rng.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "chop_test.hpp"

namespace cf = chopfractal;

namespace {
// GUI tests need a display. Linux has one only when DISPLAY is set; Windows and macOS always have a desktop session.
// With CHOPFRACTAL_REQUIRE_DISPLAY=1 (set by the CI job that runs under xvfb) a missing display fails instead of
// silently skipping every GUI and fuzz test.
bool haveDisplay() {
#if JUCE_LINUX || JUCE_BSD
  const bool has = std::getenv("DISPLAY") != nullptr;
  if (!has && std::getenv("CHOPFRACTAL_REQUIRE_DISPLAY")) {
    std::fprintf(stderr, "CHOPFRACTAL_REQUIRE_DISPLAY is set but there is no display; GUI tests cannot run\n");
    CHECK(false);
  }
  return has;
#else
  return true;
#endif
}
}  // namespace

namespace {

// A host playhead we control.
struct FakePlayHead : juce::AudioPlayHead {
  bool playing = true;
  bool hasBpm = true;
  bool hasPpq = true;
  double bpm = 120.0;
  double ppq = 0.0;
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setIsPlaying(playing);
    if (hasBpm) info.setBpm(bpm);
    if (hasPpq) info.setPpqPosition(ppq);
    info.setTimeSignature(TimeSignature{4, 4});
    return info;
  }
};

std::shared_ptr<const cf::render::SourceData> drumLoop() {
  auto d = std::make_shared<cf::render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  cf::Rng rng(3);
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 5000; ++i)
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(i) / 900.f) * 0.8f;
  return d;
}

// Sets a parameter the way a host does (through the parameter object, so the project state sees it).
void setParam(ChopFractalProcessor& p, const char* id, float plain) {
  auto* param = dynamic_cast<juce::RangedAudioParameter*>(p.apvts.getParameter(id));
  CHECK(param != nullptr);
  if (param) param->setValueNotifyingHost(param->convertTo0to1(plain));
}

void prepare(ChopFractalProcessor& p, int block = 512) {
  p.setPlayConfigDetails(2, 2, 48000.0, block);
  p.prepareToPlay(48000.0, block);
}

void makeSession(ChopFractalProcessor& p, std::uint64_t seed = 42) {
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.loadSource(drumLoop(), "loop", "loop.wav").ok());
    cf::composition::DetectOptions d;
    d.analysis.sensitivity = 0.8;
    CHECK(s.applyChops(d).ok());
    cf::pattern::Settings settings;
    settings.bars = 2;
    settings.density = 0.7;
    settings.seed = seed;
    CHECK(s.generate(settings).ok());
  });
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
  CHECK(p.acceptsMidi() && !p.producesMidi() && !p.isMidiEffect() && p.getTailLengthSeconds() == 0.0 && p.getLatencySamples() == 0);
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
  makeSession(a);
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
  CHECK_NEAR(q.currentParams().get(cf::host::kFxIntensity), 0.9, 1e-6);
  CHECK(q.currentParams().get(cf::host::kAllowFilter) == 1.0);
}

CHOP_TEST(embed_flag_and_manual_tempo_survive_a_save_and_reload) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  p.embedSource = true;
  p.manualBpm = 97.5;
  juce::MemoryBlock blob;
  p.getStateInformation(blob);
  ChopFractalProcessor q;
  q.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
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
  juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
  juce::Image img = editor->createComponentSnapshot(editor->getLocalBounds());
  CHECK(img.isValid() && img.getWidth() == editor->getWidth());
  if (const char* out = std::getenv("CHOPFRACTAL_SNAPSHOT")) {
    juce::File f(out);
    f.deleteFile();
    juce::FileOutputStream fs(f);
    juce::PNGImageFormat().writeImageToStream(img, fs);
  }
}

namespace {
juce::Button* findButton(juce::Component& root, const juce::String& text) {
  for (auto* c : root.getChildren())
    if (auto* b = dynamic_cast<juce::Button*>(c))
      if (b->getButtonText() == text) return b;
  return nullptr;
}
std::vector<std::uint8_t> patternBytes(ChopFractalProcessor& p) {
  std::vector<std::uint8_t> out;
  p.withSession([&](cf::composition::ProjectSession& s) {
    if (s.pattern()) out = cf::pattern::serialize(*s.pattern());
  });
  return out;
}
}  // namespace

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
  for (auto* c : ed->getChildren())
    if (auto* b = dynamic_cast<juce::Button*>(c)) CHECK(b->getTitle().isNotEmpty() || b->getButtonText().isNotEmpty());
}

namespace {
juce::MouseEvent mouseAt(juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods = {}) {
  auto src = juce::Desktop::getInstance().getMainMouseSource();
  return juce::MouseEvent(src, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), p.toFloat(), juce::Time::getCurrentTime(), 1, false);
}
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
juce::Point<int> hitPoint(const ChopFractalEditor& ed, const cf::ui::EventRect& r, double fraction = 0.5) {
  const auto b = ed.patternBounds();
  const float laneH = static_cast<float>(b.getHeight()) / static_cast<float>(std::max(1, ed.laneCount()));
  return {b.getX() + static_cast<int>((r.x0 + (r.x1 - r.x0) * fraction) * b.getWidth()), b.getY() + static_cast<int>((static_cast<float>(r.chopIndex) + 0.5f) * laneH)};
}
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
  for (int i = 0; i < ed->getNumChildComponents(); ++i)
    if (auto* button = dynamic_cast<juce::ToggleButton*>(ed->getChildComponent(i)))
      if (button->getButtonText() == "Move / resize hits") button->setToggleState(true, juce::dontSendNotification);
  // Direct hit editing with real mouse events.
  ed->doReroll();
  pump();
  const auto before = patternBytes(p);
  const std::size_t hits = hitCount(p);
  const cf::ui::EventRect* target = nullptr;
  for (const auto& r : ed->hitRects())
    if (r.depth == 0 && !r.locked && (r.x1 - r.x0) * ed->patternBounds().getWidth() > 24) {
      target = &r;
      break;
    }
  CHECK(target != nullptr);
  if (!target) return;
  const cf::EventId tid = target->id;
  const auto start = hitPoint(*ed, *target, 0.3);
  ed->mouseDown(mouseAt(*ed, start));
  CHECK(ed->selectedHit() == tid);
  ed->mouseDrag(mouseAt(*ed, start + juce::Point<int>(80, 0)));
  ed->mouseUp(mouseAt(*ed, start + juce::Point<int>(80, 0)));
  pump();
  CHECK(patternBytes(p) != before);
  CHECK(hitCount(p) == hits);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.undo()); });
  juce::MessageManager::getInstance()->runDispatchLoopUntil(250);  // the editor timer runs at 10 Hz
  CHECK(patternBytes(p) == before);

  // A tiny movement is a click, not an edit.
  ed->mouseDown(mouseAt(*ed, start));
  ed->mouseUp(mouseAt(*ed, start + juce::Point<int>(1, 0)));
  CHECK(patternBytes(p) == before);

  // Delete removes the selected hit (one undo step), M mutes it.
  ed->mouseDown(mouseAt(*ed, start));
  ed->mouseUp(mouseAt(*ed, start));
  CHECK(ed->keyPressed(juce::KeyPress('m', juce::ModifierKeys(), 'm')));
  CHECK(patternBytes(p) != before);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.undo()); });
  juce::MessageManager::getInstance()->runDispatchLoopUntil(250);  // the editor timer runs at 10 Hz
  ed->mouseDown(mouseAt(*ed, start));
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
      ed->mouseDown(mouseAt(*ed, pt));
      CHECK(patternBytes(p) == before);
      ed->mouseUp(mouseAt(*ed, pt));
      ed->mouseDown(mouseAt(*ed, pt, juce::ModifierKeys(juce::ModifierKeys::shiftModifier)));
      ed->mouseUp(mouseAt(*ed, pt));
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
  static const char* const kButtons[] = {"Generate", "Mutate", "Undo", "Redo", "Zoom In", "Collapse", "Lock Bar", "Fractal", "Store A", "Recall A", "Store B",
                                         "Recall B", "Keep", "Smart Setup", "Accept Roles", "Use Tempo", "Detect Chops", "Set Role", "Play", "Stop"};
  static const char* const kParams[] = {"density", "swing", "variation_amount", "fx_intensity", "chaos", "dry_mix", "pattern_bars", "allow_reverse",
                                        "allow_pitch", "allow_retrigger", "allow_filter", "allow_glide", "allow_crunch", "effect_enable"};
  FakePlayHead ph;
  p.setPlayHead(&ph);
  for (int i = 0; i < events; ++i) {
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
        ed->mouseDown(mouseAt(*ed, a, juce::ModifierKeys(m)));
        if (drag) ed->mouseDrag(mouseAt(*ed, b, juce::ModifierKeys(m)));
        ed->mouseUp(mouseAt(*ed, drag ? b : a, juce::ModifierKeys(m)));
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

int main() {
  const bool display = haveDisplay();
  std::unique_ptr<juce::ScopedJuceInitialiser_GUI> gui;
  if (display) gui = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
  juce::MessageManager::getInstance();
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

CHOP_TEST(editor_has_visible_green_play_and_red_stop_controls) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  ChopFractalEditor editor(p);
  juce::TextButton* play = nullptr;
  juce::TextButton* stop = nullptr;
  for (int i = 0; i < editor.getNumChildComponents(); ++i) {
    if (auto* b = dynamic_cast<juce::TextButton*>(editor.getChildComponent(i))) {
      if (b->getButtonText() == "Play") play = b;
      if (b->getButtonText() == "Stop") stop = b;
    }
  }
  CHECK(play != nullptr && stop != nullptr);
  if (!play || !stop) return;
  CHECK(play->isVisible() && stop->isVisible());
  CHECK(play->getWidth() >= 90 && stop->getWidth() >= 90);
  CHECK(!play->getBounds().intersects(stop->getBounds()));
  CHECK(play->findColour(juce::TextButton::buttonColourId).getGreen() >
        play->findColour(juce::TextButton::buttonColourId).getRed());
  CHECK(stop->findColour(juce::TextButton::buttonColourId).getRed() >
        stop->findColour(juce::TextButton::buttonColourId).getGreen());
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
  juce::TextEditor* status = nullptr;
  for (int i = 0; i < editor.getNumChildComponents(); ++i)
    if (auto* text = dynamic_cast<juce::TextEditor*>(editor.getChildComponent(i)))
      if (text->getTitle() == "Status messages") status = text;
  CHECK(status != nullptr);
  if (!status) return;
  CHECK(status->isReadOnly());
  CHECK(status->isMultiLine());
  CHECK(status->getText().startsWith(message));
  CHECK(status->getHeight() >= 60);
  CHECK(editor.getLocalBounds().contains(status->getBounds()));
}

CHOP_TEST(drag_selection_selects_multiple_hits_without_changing_the_pattern_and_zoom_works) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  ChopFractalEditor editor(p);
  const auto before = patternBytes(p);
  const auto bounds = editor.patternBounds();
  const auto start = bounds.getTopLeft() + juce::Point<int>(1, 1);
  const auto end = bounds.getBottomRight() - juce::Point<int>(1, 1);
  editor.mouseDown(mouseAt(editor, start));
  editor.mouseDrag(mouseAt(editor, end));
  editor.mouseUp(mouseAt(editor, end));
  CHECK(editor.selectedHits().size() > 1);
  CHECK(patternBytes(p) == before);
  // A single hit can be selected by a simple click, and Zoom creates nested events.
  const auto hits = editor.hitRects();
  if (hits.empty()) return;
  const auto point = hitPoint(editor, hits.front(), 0.5);
  editor.mouseDown(mouseAt(editor, point));
  editor.mouseUp(mouseAt(editor, point));
  CHECK(editor.selectedHit().valid());
  for (int i = 0; i < editor.getNumChildComponents(); ++i)
    if (auto* button = dynamic_cast<juce::TextButton*>(editor.getChildComponent(i)))
      if (button->getButtonText() == "Zoom In") button->onClick();
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
  const auto start = bounds.getTopLeft() + juce::Point<int>(1, bounds.getHeight() / 2);
  const auto end = bounds.getTopRight() + juce::Point<int>(-1, bounds.getHeight() / 2);
  editor.mouseDown(mouseAt(editor, start));
  editor.mouseDrag(mouseAt(editor, end));
  editor.mouseUp(mouseAt(editor, end));
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
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
      if (auto* button = dynamic_cast<juce::TextButton*>(editor.getChildComponent(i)))
        if (button->getButtonText() == "Detect Chops") button->onClick();
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
    auto a = area.getTopLeft() + juce::Point<int>(1, 1);
    auto b = area.getBottomRight() - juce::Point<int>(1, 1);
    const auto unchanged = patternBytes(p);
    editor.mouseDown(mouseAt(editor, a));
    editor.mouseDrag(mouseAt(editor, b));
    editor.mouseUp(mouseAt(editor, b));
    CHECK(editor.selectedHits().size() > 1);
    CHECK(patternBytes(p) == unchanged);
    const auto rects = editor.hitRects();
    CHECK(!rects.empty());
    if (rects.empty()) return;
    a = hitPoint(editor, rects.front(), 0.5);
    editor.mouseDown(mouseAt(editor, a));
    editor.mouseUp(mouseAt(editor, a));
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
      if (auto* button = dynamic_cast<juce::TextButton*>(editor.getChildComponent(i)))
        if (button->getButtonText() == "Zoom In") button->onClick();
    CHECK(patternBytes(p) != unchanged);

    // 4. Saving and restoring preserves the pattern and embedded audio.
    p.embedSource = true;
    juce::MemoryBlock state;
    p.getStateInformation(state);
    ChopFractalProcessor restored;
    prepare(restored);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
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
  for (int i = 0; i < editor.getNumChildComponents(); ++i) {
    auto* component = editor.getChildComponent(i);
    if (!(dynamic_cast<juce::Button*>(component) || dynamic_cast<juce::Slider*>(component) ||
          dynamic_cast<juce::ComboBox*>(component) || dynamic_cast<juce::TextEditor*>(component))) continue;
    auto* tooltip = dynamic_cast<juce::TooltipClient*>(component);
    CHECK(tooltip != nullptr);
    if (tooltip) CHECK(tooltip->getTooltip().length() > 20);
    ++controls;
  }
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

namespace {
namespace capt = chopfractal::capture;

// Records `blocks` blocks of the test sine in Free mode and leaves the engine in Captured.
void recordFree(ChopFractalProcessor& p, FakePlayHead& ph, int blocks) {
  auto& cc = p.captureController();
  CHECK(cc.arm(capt::CaptureMode::Free, 4, 30.0).isEmpty());
  CHECK(cc.engine().status().state == capt::CaptureState::Armed);
  CHECK(cc.start().isEmpty());
  run(p, ph, blocks, 512, 0.5f);
  cc.engine().requestStop();
  run(p, ph, 1, 512, 0.5f);
  CHECK(cc.engine().status().state == capt::CaptureState::Captured);
}

bool waitForStatus(ChopFractalProcessor& p, const char* text) {
  for (int i = 0; i < 100 && !p.statusMessage().contains(text); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
  return p.statusMessage().contains(text);
}
}  // namespace

CHOP_TEST(capture_flow_through_the_real_processor_keeps_input_and_embeds_it) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  recordFree(p, ph, 100);
  CHECK_EQ(p.captureController().engine().status().framesCaptured, 51200);
  p.captureController().keepAsync(true);
  CHECK(waitForStatus(p, "Captured audio is saved"));
  bool has = false, needs = false;
  std::shared_ptr<const cf::render::SourceData> data;
  std::string name;
  p.withSession([&](cf::composition::ProjectSession& s) {
    has = s.hasSource();
    needs = s.sourceNeedsEmbed();
    data = s.sourceData();
    if (has) name = s.chopMap()->info().name;
  });
  CHECK(has && needs && data);
  CHECK(name.rfind("Capture ", 0) == 0);
  CHECK(p.embedSource.load());
  CHECK_EQ(data->frames, 51200);
  CHECK_EQ(data->channels, 2);
  CHECK_NEAR(data->samples[100], 0.5f * std::sin(0.05f * 100.f), 1e-6);  // the input, not the rendered output
  juce::MemoryBlock saved;
  p.getStateInformation(saved);
  ChopFractalProcessor q;
  q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  q.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.hasSource() && !s.sourceMissing());
    CHECK_EQ(s.chopMap()->info().frames, 51200);
  });
}

CHOP_TEST(capture_undo_keep_restores_previous_source) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;
  std::size_t markers = 0;
  p.withSession([&](cf::composition::ProjectSession& s) { markers = s.chopMap()->markers().size(); });
  recordFree(p, ph, 40);
  p.captureController().keepAsync(false);
  CHECK(waitForStatus(p, "Captured audio is saved"));
  CHECK(p.captureController().canUndoKeep());
  CHECK(p.captureController().undoKeep());
  CHECK(!p.captureController().canUndoKeep());
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK_EQ(s.chopMap()->info().frames, 96000);
    CHECK_EQ(s.chopMap()->markers().size(), markers);
    CHECK(!s.sourceNeedsEmbed() || s.chopMap()->info().path.empty());
  });
}

CHOP_TEST(capture_is_refused_offline_and_idle_capture_leaves_audio_untouched) {
  ChopFractalProcessor p;
  prepare(p);
  p.setNonRealtime(true);
  CHECK(p.captureController().arm(capt::CaptureMode::Free, 4, 30.0).contains("offline render"));
  p.setNonRealtime(false);
  CHECK(p.captureController().engine().status().state == capt::CaptureState::Idle);
  ChopFractalProcessor q;
  prepare(q);
  CHECK(q.captureController().engine().status().capacityFrames == 0);  // no memory held while idle
}

CHOP_TEST(capture_panel_meter_shows_level_and_latches_clipping) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  ChopFractalEditor editor(p);
  FakePlayHead ph;
  auto& cc = p.captureController();
  CHECK(cc.arm(capt::CaptureMode::Free, 4, 30.0).isEmpty());
  run(p, ph, 4, 512, 0.5f);  // armed: metering without recording
  CHECK_NEAR(cc.engine().takeRecentPeak(), 0.5f, 0.01);
  CHECK_EQ(cc.engine().status().clippedSamples, 0u);
  CHECK(cc.start().isEmpty());
  run(p, ph, 4, 512, 1.0f);  // full-scale sine reaches the clip threshold
  CHECK(cc.engine().status().clippedSamples > 0);
  juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
  CHECK(editor.getTooltip().isNotEmpty());
}

namespace {
juce::Button* findButtonDeep(juce::Component& root, const juce::String& text) {
  if (auto* b = dynamic_cast<juce::Button*>(&root))
    if (b->getButtonText() == text) return b;
  for (int i = 0; i < root.getNumChildComponents(); ++i)
    if (auto* f = findButtonDeep(*root.getChildComponent(i), text)) return f;
  return nullptr;
}
juce::Slider* findSliderByTitle(juce::Component& root, const juce::String& title) {
  if (auto* s = dynamic_cast<juce::Slider*>(&root))
    if (s->getTitle() == title) return s;
  for (int i = 0; i < root.getNumChildComponents(); ++i)
    if (auto* f = findSliderByTitle(*root.getChildComponent(i), title)) return f;
  return nullptr;
}
}  // namespace

CHOP_TEST(morph_controls_enable_with_both_slots_and_commit_adds_a_node) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.loadSource(drumLoop(), "loop", "loop.wav").ok()); });
  std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
  CHECK(ed != nullptr);
  if (!ed) return;
  ed->setVisible(true);
  auto pump = [](int ms = 120) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };
  auto click = [&](const char* text) {
    juce::Button* b = findButtonDeep(*ed, text);
    CHECK(b != nullptr);
    if (b && b->isEnabled()) b->triggerClick();
    pump();
  };
  auto nodes = [&] {
    std::size_t n = 0;
    p.withSession([&](cf::composition::ProjectSession& s) { n = s.history().size(); });
    return n;
  };
  auto active = [&] {
    bool a = false;
    p.withSession([&](cf::composition::ProjectSession& s) { a = s.morphActive(); });
    return a;
  };
  click("Detect Chops");
  click("Generate");
  click("Store A");
  click("Mutate");
  click("Store B");
  pump(200);
  auto* toggle = dynamic_cast<juce::ToggleButton*>(findButtonDeep(*ed, "Morph"));
  auto* slider = findSliderByTitle(*ed, "Morph amount");
  CHECK(toggle != nullptr && slider != nullptr);
  if (!toggle || !slider) return;
  CHECK(toggle->isEnabled());
  CHECK(!slider->isEnabled());
  toggle->setToggleState(true, juce::sendNotificationSync);
  pump();
  CHECK(active());
  CHECK(p.statusMessage().contains("Morphing 50% toward B"));
  slider->setValue(0.25, juce::sendNotificationSync);
  pump(250);
  double amount = 0;
  p.withSession([&](cf::composition::ProjectSession& s) { amount = s.morphAmount(); });
  CHECK_NEAR(amount, 0.25, 0.005);
  const std::size_t before = nodes();
  const auto pre = patternBytes(p);
  click("Commit Morph");
  CHECK(!active());
  CHECK_EQ(nodes(), before + 1);
  click("Undo");
  CHECK(patternBytes(p) == pre);
  toggle->setToggleState(true, juce::sendNotificationSync);
  pump();
  CHECK(active());
  click("Cancel Morph");
  CHECK(!active());
  CHECK(patternBytes(p) == pre);
  CHECK(p.statusMessage().contains("Morph cancelled; your pattern is unchanged."));
}

namespace {
template <class T>
T* findByTitle(juce::Component& root, const juce::String& title) {
  if (auto* c = dynamic_cast<T*>(&root))
    if (c->getTitle() == title) return c;
  for (int i = 0; i < root.getNumChildComponents(); ++i)
    if (auto* f = findByTitle<T>(*root.getChildComponent(i), title)) return f;
  return nullptr;
}

struct RulesRig {
  ChopFractalProcessor p;
  std::unique_ptr<juce::AudioProcessorEditor> ed;
  RulesPanel* panel = nullptr;
  RulesRig() {
    prepare(p);
    p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.loadSource(drumLoop(), "loop", "loop.wav").ok()); });
    ed.reset(p.createEditor());
    if (!ed) return;
    ed->setVisible(true);
    for (int i = 0; i < ed->getNumChildComponents(); ++i)
      if (auto* r = dynamic_cast<RulesPanel*>(ed->getChildComponent(i))) panel = r;
    pump();
  }
  static void pump(int ms = 100) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
  void click(const char* text, bool deep = false) {
    juce::Button* b = deep ? findButtonDeep(*ed, text) : findButton(*ed, text);
    CHECK(b != nullptr);
    if (b && b->isEnabled()) b->triggerClick();
    pump();
  }
  void detectAndRoles() {
    click("Detect Chops");
    p.withSession([&](cf::composition::ProjectSession& s) {
      const auto& c = s.chops()->chops;
      for (std::size_t i = 0; i < c.size(); ++i) s.assignRole(c[i].id, i == 0 ? "kick" : (i == 1 ? "hat" : "snare"));
    });
  }
};
}  // namespace

CHOP_TEST(rules_panel_template_apply_and_generate) {
  if (!haveDisplay()) return;
  RulesRig r;
  CHECK(r.ed && r.panel);
  if (!r.ed || !r.panel) return;
  r.detectAndRoles();
  r.click("Rules");
  CHECK(r.panel->isVisible());
  findByTitle<juce::ComboBox>(*r.panel, "Template role")->setText("hat", juce::sendNotificationSync);
  findByTitle<juce::TextEditor>(*r.panel, "Template bar from")->setText("1", true);
  findByTitle<juce::TextEditor>(*r.panel, "Template bar to")->setText("2", true);
  r.click("Exclude from bars", true);
  CHECK_EQ(r.panel->model().rows().size(), 1u);
  CHECK(r.panel->hasUnapplied());
  r.click("Apply", true);
  cf::roles::RuleSet rs;
  r.p.withSession([&](cf::composition::ProjectSession& s) { rs = s.rules(); });
  CHECK_EQ(rs.rules.size(), 1u);
  if (rs.rules.size() == 1) {
    CHECK(rs.rules[0].action.kind == cf::roles::ActionKind::Forbid);
    CHECK(rs.rules[0].when.role == "hat");
    CHECK_EQ(rs.rules[0].when.barMin, 0);
    CHECK_EQ(rs.rules[0].when.barMax, 1);
  }
  CHECK(r.p.statusMessage().contains("Rules applied."));
  CHECK(!r.panel->hasUnapplied());
  r.click("Generate");
  r.p.withSession([&](cf::composition::ProjectSession& s) {
    const cf::ChopId hat = s.chops()->chops[1].id;
    for (std::size_t b = 0; b < 2 && b < s.pattern()->bars.size(); ++b)
      for (const auto& bt : s.pattern()->bars[b].beats)
        for (const auto& e : bt.events) CHECK(e.chop != hat);
  });
  r.click("Test Rules", true);
  CHECK(r.p.statusMessage().startsWith("Test"));
  r.click("Close", true);
  CHECK(!r.panel->isVisible());
}

CHOP_TEST(rules_panel_invalid_rule_blocks_apply_and_revert_restores) {
  if (!haveDisplay()) return;
  RulesRig r;
  CHECK(r.ed && r.panel);
  if (!r.ed || !r.panel) return;
  r.detectAndRoles();
  r.click("Rules");
  r.click("Add Rule", true);
  CHECK_EQ(r.panel->model().rows().size(), 1u);
  findByTitle<juce::ComboBox>(*r.panel, "Action")->setSelectedId(5, juce::sendNotificationSync);  // Limit count per bar
  findByTitle<juce::TextEditor>(*r.panel, "Count")->setText("0", true);
  RulesRig::pump();
  bool error = false;
  for (const auto& i : r.panel->model().issues()) error |= i.error;
  CHECK(error);
  CHECK(!findButtonDeep(*r.ed, "Apply")->isEnabled());
  CHECK(findButtonDeep(*r.ed, "Apply")->getTooltip().isNotEmpty());
  r.click("Revert", true);
  CHECK_EQ(r.panel->model().rows().size(), 0u);
  bool empty = false;
  r.p.withSession([&](cf::composition::ProjectSession& s) { empty = s.rules().rules.empty(); });
  CHECK(empty);
}

CHOP_TEST(rules_panel_why_explains_the_selected_hit) {
  if (!haveDisplay()) return;
  RulesRig r;
  CHECK(r.ed && r.panel);
  if (!r.ed || !r.panel) return;
  r.detectAndRoles();
  r.click("Generate");
  r.click("Rules");
  r.click("Why?", true);
  CHECK(r.p.statusMessage().contains("Select a hit first; Why? explains that hit."));
  auto* editor = dynamic_cast<ChopFractalEditor*>(r.ed.get());
  CHECK(editor != nullptr);
  cf::EventId first;
  r.p.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& bt : s.pattern()->bars[0].beats)
      if (!bt.events.empty() && !first.valid()) first = bt.events.front().id;
  });
  CHECK(first.valid());
  editor->setSelectedHits({first});
  r.click("Why?", true);
  CHECK(r.p.statusMessage().contains("rule") || r.p.statusMessage().contains("nothing constrains"));
}

CHOP_TEST(rules_panel_roles_tab_assigns_and_clears_and_controls_are_titled) {
  if (!haveDisplay()) return;
  RulesRig r;
  CHECK(r.ed && r.panel);
  if (!r.ed || !r.panel) return;
  r.click("Detect Chops");
  r.click("Rules");
  r.click("Roles", true);
  auto* list = findByTitle<juce::ListBox>(*r.panel, "Chop roles");
  CHECK(list != nullptr);
  if (!list) return;
  list->selectRow(1);
  RulesRig::pump();
  auto* combo = findByTitle<juce::ComboBox>(*r.panel, "Role for the selected chop");
  CHECK(combo != nullptr);
  if (!combo) return;
  combo->setText("snare", juce::sendNotificationSync);
  RulesRig::pump();
  cf::ChopId chop;
  std::string role;
  r.p.withSession([&](cf::composition::ProjectSession& s) {
    chop = s.chops()->chops[1].id;
    role = s.roleMap().roleOf(chop);
  });
  CHECK(role == "snare");
  list->selectRow(1);
  combo->setText("(none)", juce::sendNotificationSync);
  RulesRig::pump();
  r.p.withSession([&](cf::composition::ProjectSession& s) { role = s.roleMap().roleOf(chop); });
  CHECK(role.empty());
  // every interactive control in the panel has an accessible name
  std::function<void(juce::Component&)> check = [&](juce::Component& c) {
    if (auto* b = dynamic_cast<juce::Button*>(&c)) CHECK(b->getTitle().isNotEmpty() || b->getButtonText().isNotEmpty());
    if (dynamic_cast<juce::ComboBox*>(&c) || dynamic_cast<juce::Slider*>(&c) || dynamic_cast<juce::TextEditor*>(&c)) {
      if (c.getParentComponent() == r.panel) CHECK(c.getTitle().isNotEmpty());
    }
    for (int i = 0; i < c.getNumChildComponents(); ++i) check(*c.getChildComponent(i));
  };
  check(*r.panel);
}

CHOP_TEST(scene_panel_captures_plays_wraps_and_stops_without_touching_the_pattern) {
  if (!haveDisplay()) return;
  RulesRig r;
  CHECK(r.ed != nullptr);
  if (!r.ed) return;
  r.click("Detect Chops");
  r.click("Generate");
  r.click("Scenes");
  r.click("Capture Scene", true);
  r.click("Mutate");
  r.click("Capture Scene", true);
  auto* list = findByTitle<juce::ListBox>(*r.ed, "Scene list");
  CHECK(list != nullptr);
  if (!list) return;
  CHECK_EQ(list->getListBoxModel()->getNumRows(), 2);
  auto* loops = findByTitle<juce::Slider>(*r.ed, "Scene loops");
  CHECK(loops != nullptr);
  for (int row = 0; row < 2; ++row) {
    list->selectRow(row);
    RulesRig::pump();
    loops->setValue(1, juce::sendNotificationSync);
    RulesRig::pump();
  }
  std::vector<cf::scenes::SceneId> ids;
  r.p.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& sc : s.sceneList().scenes) {
      ids.push_back(sc.id);
      CHECK_EQ(sc.loops, 1);
    }
  });
  list->selectRow(0);  // play from the first scene
  RulesRig::pump();
  const auto before = patternBytes(r.p);
  r.click("Play Scenes", true);
  FakePlayHead host;
  host.playing = true;
  r.p.setPlayHead(&host);
  juce::MidiBuffer midi;
  int bars = 4;
  r.p.withSession([&](cf::composition::ProjectSession& s) { bars = s.pattern()->settings.bars; });
  const double loopFrames = bars * 4.0 * 0.5 * 48000.0;
  std::vector<cf::scenes::SceneId> seen;
  host.ppq = 0.0;
  for (double done = 0; done < 3.5 * loopFrames; done += 512) {
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    r.p.processBlock(buf, midi);
    host.ppq += 512.0 * 120.0 / (60.0 * 48000.0);
    if (static_cast<int>(done / 512) % 8 == 0) {
      r.p.pollAudioFlags();
      r.p.withSession([&](cf::composition::ProjectSession& s) {
        const auto id = s.sceneStatus().playing;
        if (seen.empty() || seen.back() != id) seen.push_back(id);
      });
    }
  }
  r.p.setPlayHead(nullptr);
  CHECK(seen.size() >= 3);
  if (seen.size() >= 3) {
    CHECK_EQ(seen[0], ids[0]);
    CHECK_EQ(seen[1], ids[1]);
    CHECK_EQ(seen[2], ids[0]);  // wrap
  }
  CHECK(patternBytes(r.p) == before);
  r.click("Stop Scenes", true);
  bool running = true;
  r.p.withSession([&](cf::composition::ProjectSession& s) { running = s.sceneStatus().running; });
  CHECK(!running);
  CHECK(patternBytes(r.p) == before);
  r.p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.clearScenes().ok()); });
  RulesRig::pump(300);
  CHECK_EQ(list->getListBoxModel()->getNumRows(), 0);
}

namespace {
// Runs `blocks` blocks of 512 with a host that is playing or not, injecting `midi` into the first block, and
// returns the left channel. The playhead position continues from `ph.ppq` (unlike run(), which restarts at zero).
std::vector<float> runMidi(ChopFractalProcessor& p, FakePlayHead& ph, int blocks, const juce::MidiBuffer& first = {}) {
  p.setPlayHead(&ph);
  std::vector<float> out;
  for (int b = 0; b < blocks; ++b) {
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    juce::MidiBuffer midi;
    if (b == 0) midi = first;
    p.processBlock(buf, midi);
    for (int i = 0; i < 512; ++i) out.push_back(buf.getSample(0, i));
    if (ph.playing) ph.ppq += 512.0 * ph.bpm / (60.0 * 48000.0);
  }
  p.setPlayHead(nullptr);
  return out;
}
}  // namespace

CHOP_TEST(midi_note_on_triggers_the_mapped_chop_at_the_event_sample) {
  for (bool hostPlaying : {false, true}) {
    ChopFractalProcessor p;
    prepare(p);
    makeSession(p);
    FakePlayHead ph;
    ph.playing = hostPlaying;
    ph.ppq = 7.0;  // late in the loop: the pattern itself is quiet here, the pad is what we measure
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 100);
    const auto silent = runMidi(p, ph, 1);
    ph.ppq = 7.0;
    const auto out = runMidi(p, ph, 4, midi);
    if (!hostPlaying) {
      CHECK(peak(silent) < 1e-6f);
      for (int i = 0; i <= 100; ++i) CHECK_NEAR(out[static_cast<std::size_t>(i)], 0.0, 1e-9);
      CHECK(peak(std::vector<float>(out.begin() + 100, out.begin() + 2000)) > 0.05f);
    } else {
      CHECK(peak(out) > 0.0f);
    }
  }
}

CHOP_TEST(midi_channel_filter_gate_all_notes_off_and_bypass) {
  auto make = [] {
    auto p = std::make_unique<ChopFractalProcessor>();
    prepare(*p);
    makeSession(*p);
    return p;
  };
  FakePlayHead ph;
  ph.playing = false;
  {  // channel filter
    auto p = make();
    p->withSession([](cf::composition::ProjectSession& s) {
      cf::composition::PadSettings ps;
      ps.midiChannel = 2;
      CHECK(s.setPadSettings(ps).ok());
    });
    juce::MidiBuffer ch1, ch2;
    ch1.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 10);
    ch2.addEvent(juce::MidiMessage::noteOn(2, 36, static_cast<juce::uint8>(100)), 10);
    CHECK(peak(runMidi(*p, ph, 3, ch1)) < 1e-6f);
    CHECK(peak(runMidi(*p, ph, 3, ch2)) > 0.05f);
  }
  {  // Gate: note off stops the voice
    auto p = make();
    setParam(*p, "perf_mode", 2.f);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
    runMidi(*p, ph, 1, on);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
    const auto tail = runMidi(*p, ph, 3, off);
    CHECK(peak(std::vector<float>(tail.begin() + 200, tail.end())) < 1e-4f);
  }
  {  // CC 123 silences pad voices
    auto p = make();
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
    runMidi(*p, ph, 1, on);
    juce::MidiBuffer all;
    all.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    const auto tail = runMidi(*p, ph, 3, all);
    CHECK(peak(std::vector<float>(tail.begin() + 200, tail.end())) < 1e-4f);
  }
  {  // effect off: MIDI ignored
    auto p = make();
    setParam(*p, "effect_enable", 0.f);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
    CHECK(peak(runMidi(*p, ph, 3, on)) < 1e-6f);
  }
  {  // on-screen presses reach the audio one block later
    auto p = make();
    p->padTrigger(36, 100, true);
    CHECK(peak(runMidi(*p, ph, 3)) > 0.05f);
  }
  {  // more events than a block can carry: dropped and counted, never fatal
    auto p = make();
    juce::MidiBuffer many;
    for (int i = 0; i < 300; ++i) many.addEvent(juce::MidiMessage::noteOn(1, 36 + (i % 8), static_cast<juce::uint8>(90)), i);
    runMidi(*p, ph, 2, many);
    CHECK(p->pads().droppedEvents() > 0u);
  }
}

CHOP_TEST(recording_a_take_adds_quantized_events_at_the_midpoint_and_flushes_on_disarm) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;
  ph.playing = true;
  ph.ppq = 0.0;
  p.setRecordArmed(true);
  CHECK(p.statusMessage().contains("Recording armed"));
  std::size_t before = 0;
  p.withSession([&](cf::composition::ProjectSession& s) { before = cf::pattern::eventCount(*s.pattern()); });
  p.setPlayHead(&ph);
  // Find a free slot (no event of that chop at that tick) so the merge cannot be skipped as a duplicate.
  int freeChop = -1;
  int freeStart = 0;
  p.withSession([&](cf::composition::ProjectSession& s) {
    for (int chop = 0; chop < 8 && freeChop < 0; ++chop)
      for (int slot = 2; slot < 12 && freeChop < 0; ++slot) {
        bool taken = false;
        for (const auto& bt : s.pattern()->bars[0].beats)
          for (const auto& e : bt.events) taken = taken || (e.chop == s.chops()->chops[static_cast<std::size_t>(chop)].id && e.start == slot * 240);
        if (!taken) {
          freeChop = chop;
          freeStart = slot * 240;
        }
      }
  });
  CHECK(freeChop >= 0);
  const double targetPpq = freeStart / 960.0;
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 36 + freeChop, static_cast<juce::uint8>(100)), 0);
  bool played = false;
  for (int b = 0; b < 400; ++b) {  // ppq 0 .. 8.5 (the loop is 8 quarters, the midpoint is ppq 4)
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    juce::MidiBuffer m;
    if (!played && ph.ppq >= targetPpq) {
      m = midi;
      played = true;
    }
    p.processBlock(buf, m);
    ph.ppq += 512.0 * ph.bpm / (60.0 * 48000.0);
    if (b % 4 == 0) p.pollAudioFlags();
  }
  p.setPlayHead(nullptr);
  CHECK(played);
  CHECK(p.statusMessage().contains("Recorded 1 hit"));
  bool found = false;
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK_EQ(cf::pattern::eventCount(*s.pattern()), before + 1);
    for (const auto& bt : s.pattern()->bars[0].beats)
      for (const auto& e : bt.events) found = found || (e.userOwned && e.chop == s.chops()->chops[static_cast<std::size_t>(freeChop)].id && e.start == freeStart);
  });
  CHECK(found);
  // Disarming flushes whatever is waiting, and a take with nothing in it changes nothing.
  p.padTrigger(37, 90, true);
  p.setRecordArmed(true);
  runMidi(p, ph, 1);
  p.setRecordArmed(false);
  p.setRecordArmed(false);
}

CHOP_TEST(pad_panel_pads_trigger_and_expose_settings) {
  if (!haveDisplay()) return;
  RulesRig r;
  CHECK(r.ed != nullptr);
  if (!r.ed) return;
  r.click("Detect Chops");
  r.click("Generate");
  r.click("Pads");
  auto* pad = dynamic_cast<juce::Button*>(findButtonDeep(*r.ed, "Pad 1"));
  CHECK(pad != nullptr);
  if (!pad) return;
  CHECK(pad->isEnabled());
  pad->setState(juce::Button::buttonDown);
  RulesRig::pump();
  FakePlayHead ph;
  ph.playing = false;
  CHECK(peak(runMidi(r.p, ph, 4)) > 0.05f);
  pad->setState(juce::Button::buttonNormal);
  RulesRig::pump();
  CHECK(findByTitle<juce::ComboBox>(*r.ed, "Pad mode") != nullptr);
  CHECK(findByTitle<juce::ComboBox>(*r.ed, "Velocity curve") != nullptr);
  auto* base = findByTitle<juce::Slider>(*r.ed, "Base note");
  CHECK(base != nullptr);
  if (base) {
    base->setValue(48, juce::sendNotificationSync);
    RulesRig::pump();
    int note = 0;
    r.p.withSession([&](cf::composition::ProjectSession& s) { note = s.padSettings().baseNote; });
    CHECK_EQ(note, 48);
  }
  r.click("Close Pads", true);
}
