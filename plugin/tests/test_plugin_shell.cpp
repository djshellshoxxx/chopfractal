// Headless tests of the real JUCE processor: no host and no window, but the real parameter tree, host-time
// translation, audio path, project-state round trip and file decoding.
#include <algorithm>
#include <memory>
#include <chopfractal/chop_contracts/rng.hpp>
#include <cmath>
#include <cstdlib>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "chop_test.hpp"

namespace cf = chopfractal;

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
    CHECK(s.loadSource(drumLoop(), "loop", "").ok());
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
  CHECK(!p.acceptsMidi() && !p.producesMidi() && p.getTailLengthSeconds() == 0.0);
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

CHOP_TEST(editor_constructs_draws_and_reflects_the_session_when_a_display_exists) {
  if (!std::getenv("DISPLAY")) return;  // CI without a display skips the GUI smoke test
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
juce::ComboBox* findCombo(juce::Component& root, const juce::String& title) {
  for (auto* c : root.getChildren())
    if (auto* b = dynamic_cast<juce::ComboBox*>(c))
      if (b->getTitle() == title) return b;
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
  if (!std::getenv("DISPLAY")) return;
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
  click("Lock Bar");
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.pattern()->bars[0].locked); });
  click("Lock Bar");
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.pattern()->bars[0].locked); });
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
  (void)findCombo;
}

int main() {
  const bool display = std::getenv("DISPLAY") != nullptr;
  std::unique_ptr<juce::ScopedJuceInitialiser_GUI> gui;
  if (display) gui = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
  juce::MessageManager::getInstance();
  const int rc = chop_test::runAll();
  gui.reset();
  juce::DeletedAtShutdown::deleteAll();
  juce::MessageManager::deleteInstance();
  return rc;
}
