// F05 feedback without a display: tempo, meter and meter readings from the audio thread, notices from the session, the
// commands that log every notice, and the soak smoke test.
#include <chopfractal/chop_roles_grammar/grammar.hpp>
#include <cstring>

#include "PluginEditor.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
void runBlocks(ChopFractalProcessor& p, FakePlayHead& ph, int blocks, int block = 512, float input = 0.f) {
  p.setPlayHead(&ph);
  juce::MidiBuffer midi;
  for (int b = 0; b < blocks; ++b) {
    juce::AudioBuffer<float> buf(2, block);
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < block; ++i) buf.setSample(c, i, input * std::sin(0.05f * static_cast<float>(i + b * block)));
    p.processBlock(buf, midi);
    ph.ppq += static_cast<double>(block) * ph.bpm / (60.0 * 48000.0);
  }
  p.setPlayHead(nullptr);
}
}  // namespace

CHOP_TEST(processor_publishes_tempo_meter_and_meter_readings) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  EditorModel m(p);
  FakePlayHead ph;
  ph.bpm = 100.0;
  ph.numerator = 3;
  ph.denominator = 4;
  runBlocks(p, ph, 20);
  CHECK_NEAR(p.activeBpm.load(), 100.0, 1e-6);
  CHECK_EQ(p.hostBeatsPerBar.load(), 3);
  CHECK_EQ(p.hostBeatUnit.load(), 4);
  const auto reading = p.takeMeter();
  CHECK(reading.peak[0] > 0.f);
  CHECK(reading.activeVoices >= 0 && reading.activeVoices <= EditorModel::kMaxVoices);
  CHECK(m.syncText(false) == "100.0 BPM  3/4  Playing");

  // Without host tempo or meter the box says so.
  ChopFractalProcessor q;
  prepare(q);
  makeSession(q);
  EditorModel mq(q);
  FakePlayHead none;
  none.hasBpm = false;
  none.hasPpq = false;
  q.manualBpm = 120.0;
  runBlocks(q, none, 4);
  CHECK(q.usedFallbackTempo.load());
  CHECK(mq.syncText(false).startsWith("120.0 BPM (manual)  "));
  CHECK(mq.syncText(true).startsWith("120.0 | "));
}

CHOP_TEST(session_notices_reach_the_log_without_a_button_press) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::roles::RuleSet rules;
    rules.rules = {cf::roles::templates::preserveFirst(1, "kick"), cf::roles::templates::excludeRole(2, "kick", 0, -1)};
    CHECK(s.setRules(rules).ok());
    CHECK(!s.notices().empty());
  });
  p.pollAudioFlags();
  bool found = false;
  for (const auto& e : p.noticeLog().snapshot())
    if (e.text.startsWith("Rule ") && e.severity == Severity::Warning) found = true;
  CHECK(found);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.notices().empty()); });  // drained
  CHECK(p.statusSeverity() == Severity::Warning);
}

CHOP_TEST(run_logs_every_notice_not_just_the_last) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  EditorModel m(p);
  EditorCommands c(p, m);
  p.noticeLog().clear();
  const auto st = c.run([](cf::composition::ProjectSession& s) {
    cf::roles::RuleSet rules;
    rules.rules = {cf::roles::templates::preserveFirst(1, "kick"), cf::roles::templates::excludeRole(2, "kick", 0, -1), cf::roles::templates::excludeRole(3, "kick", 0, -1)};
    return s.setRules(rules);
  });
  CHECK(st.ok());
  CHECK(p.noticeLog().size() >= 2u);  // every warning, not only the last
  CHECK(p.statusSeverity() == Severity::Warning);
  p.noticeLog().clear();
  const auto bad = c.run([](cf::composition::ProjectSession& s) { return s.mutate(cf::pattern::MutateOptions{}); });
  if (!bad.ok()) CHECK(p.statusSeverity() == Severity::Error);
}

CHOP_TEST(variation_switch_messages_follow_the_queue) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::MutateOptions o;
    o.seed = 5;
    o.amount = 0.6;
    CHECK(s.mutate(o).ok());
  });
  EditorModel m(p);
  EditorCommands c(p, m);
  m.refresh(true, 600);
  cf::history::NodeId first = cf::history::kNoNode;
  p.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& n : s.history().listAll())
      if (n.id != s.history().active()) first = n.id;
  });
  CHECK(first != cf::history::kNoNode);
  p.hostPlaying = true;
  c.activateNode(first);
  CHECK(p.statusMessage().startsWith("Variation \"") && p.statusMessage().endsWith("will start at the next loop."));
  m.refresh(false, 600);
  CHECK(m.pendingNode() == first);
  p.withSession([&](cf::composition::ProjectSession& s) { s.onLoopBoundary(); });
  m.refresh(false, 600);
  CHECK(m.pendingNode() == cf::history::kNoNode);
  CHECK(p.statusMessage().startsWith("Switched to \""));
  // Not playing: immediate, only the second message appears.
  p.hostPlaying = false;
  c.activateNode(first);
  CHECK(p.statusMessage().startsWith("Switched to \""));
}

CHOP_TEST(requesting_a_load_asks_first_only_when_the_project_has_work) {
  ChopFractalProcessor p;
  prepare(p);
  EditorModel m(p);
  EditorCommands c(p, m);
  int asked = 0;
  bool answer = false;
  c.confirmReplace = [&](std::function<void(bool)> done) {
    ++asked;
    done(answer);
  };
  const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_g3_load");
  dir.createDirectory();
  const juce::File wav = dir.getChildFile("loop.wav");
  {
    wav.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> stream = wav.createOutputStream();
    auto writer = fmt.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16));
    CHECK(writer != nullptr);
    if (!writer) return;
    juce::AudioBuffer<float> b(1, 22050);
    for (int i = 0; i < 22050; ++i) b.setSample(0, i, 0.5f * std::sin(0.02f * static_cast<float>(i)));
    writer->writeFromAudioSampleBuffer(b, 0, 22050);
  }
  c.requestLoad(wav);  // empty project: no question
  CHECK_EQ(asked, 0);
  CHECK(waitFor([&] {
    bool has = false;
    p.withSession([&](cf::composition::ProjectSession& s) { has = s.hasSource(); });
    return has;
  }));
  makeSession(p);  // a pattern now exists
  const auto before = patternBytes(p);
  c.requestLoad(wav);
  CHECK_EQ(asked, 1);
  CHECK(patternBytes(p) == before);  // Cancel leaves everything untouched
  CHECK(p.statusMessage().contains("cancelled"));
  answer = true;
  c.requestLoad(wav);
  CHECK_EQ(asked, 2);
  CHECK(waitFor([&] { return p.statusMessage().startsWith("Loaded"); }));
  dir.deleteRecursively();
}

CHOP_TEST(many_files_in_one_drop_load_the_first_and_log_it) {
  ChopFractalProcessor p;
  prepare(p);
  EditorModel m(p);
  EditorCommands c(p, m);
  c.handleDroppedFiles({"/nonexistent/a.wav", "/nonexistent/b.wav"});
  bool found = false;
  for (const auto& e : p.noticeLog().snapshot())
    if (e.text == "Only the first file was loaded.") found = true;
  CHECK(found);
  c.handleDroppedFiles({"/tmp/readme.txt"});
  CHECK(p.statusMessage().startsWith("Drop an audio sample"));
}

// Soak: a long playing session with Evolve running, parameter moves and meter takes; nothing may hang, produce non-finite
// audio or grow the log past its capacity.
CHOP_TEST(soak_smoke_runs_audio_evolve_and_the_log_without_trouble) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::evolve::Settings e;
    e.everyLoops = 1;
    e.amount = 0.4;
    e.enabled = true;
    (void)s.setEvolve(e);
  });
  p.setPlayHead(&ph);
  juce::MidiBuffer midi;
  float worst = 0.f;
  for (int block = 0; block < 4000; ++block) {  // about 40 s of audio at 512 frames
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    p.processBlock(buf, midi);
    for (int i = 0; i < 512; ++i) {
      const float v = buf.getSample(0, i);
      CHECK(std::isfinite(v));
      worst = std::max(worst, std::fabs(v));
    }
    ph.ppq += 512.0 * ph.bpm / (60.0 * 48000.0);
    if (block % 100 == 0) {
      if (auto* prm = p.apvts.getParameter("density")) prm->setValueNotifyingHost(static_cast<float>(block % 7) / 7.0f);
      p.pollAudioFlags();
      (void)p.takeMeter();
    }
  }
  p.setPlayHead(nullptr);
  CHECK(worst < 8.f);
  CHECK(p.noticeLog().size() <= NoticeLog::kCapacity);
}
