// F01 shell tests: persisted extras and A/B through the real processor, worker restore of reference-only audio, and
// export on a worker thread with progress and cancel. Message-loop helpers: waitFor / waitForRestore (shell_test_support).
#include <algorithm>
#include <chrono>
#include <memory>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {

// The first int after the magic is the wrapper version (juce writes ints little endian).
int wrapperVersion(const juce::MemoryBlock& state) {
  const auto* b = static_cast<const unsigned char*>(state.getData());
  return state.getSize() >= 8 ? (b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24)) : -1;
}

juce::File tempDir(const char* name) {
  juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
  dir.deleteRecursively();
  dir.createDirectory();
  return dir;
}

// 32-bit float WAV of the drum loop: decodes back to exactly the same samples, so the source id matches.
juce::File writeDrumWav(const juce::File& dir, const juce::String& name, const std::shared_ptr<const cf::render::SourceData>& src) {
  juce::File f = dir.getChildFile(name);
  f.deleteFile();
  juce::WavAudioFormat wav;
  std::unique_ptr<juce::OutputStream> os(new juce::FileOutputStream(f));
  std::unique_ptr<juce::AudioFormatWriter> w = wav.createWriterFor(
      os, juce::AudioFormatWriter::Options{}.withSampleRate(static_cast<double>(src->sampleRate)).withNumChannels(1).withBitsPerSample(32));
  CHECK(w != nullptr);
  if (!w) return f;
  const float* ch[1] = {src->channel(0)};
  CHECK(w->writeFromFloatArrays(ch, 1, static_cast<int>(src->frames)));
  return f;
}

// A processor whose project references `file` (path set, audio not embedded).
void makeReferenceSession(ChopFractalProcessor& p, const juce::File& file) {
  juce::String err;
  auto data = ChopFractalProcessor::decodeFile(file, err);
  CHECK(data != nullptr);
  if (!data) return;
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.loadSource(data, file.getFileNameWithoutExtension().toStdString(), file.getFullPathName().toStdString()).ok());
    cf::composition::DetectOptions d;
    d.analysis.sensitivity = 0.8;
    CHECK(s.applyChops(d).ok());
    cf::pattern::Settings settings;
    settings.bars = 2;
    settings.density = 0.7;
    settings.seed = 42;
    CHECK(s.generate(settings).ok());
  });
}

ExportRequest longWavRequest(const juce::File& target) {
  ExportRequest r;
  r.kind = ExportRequest::Kind::Wav;
  r.path = target.getFullPathName().toStdString();
  r.wav.loops = 16;
  r.wav.wav.sampleRate = 96000;
  r.wav.wav.format = cf::wav::Format::Float32;
  r.wav.tailSeconds = 5.0;
  return r;
}

bool stateIs(ChopFractalProcessor& p, ExportStatus::State s) { return p.exportStatus().state == s; }

}  // namespace

CHOP_TEST(state_round_trip_restores_extras_ab_manual_bpm_and_embed_flag) {
  ChopFractalProcessor a;
  prepare(a);
  makeSession(a);
  juce::MemoryBlock plain;
  a.getStateInformation(plain);
  CHECK_EQ(wrapperVersion(plain), 1);  // nothing new persisted: the exact old wrapper version

  cf::composition::ProjectExtras e;
  e.seed = 123456789;
  e.motif = "xa.x";
  e.fractalDepth = 3;
  e.roleIndex = 2;
  e.detectMode = 2;
  e.exportUi = {2, 3, 4, {}};
  e.ui.editHits = true;
  e.ui.search = "kick";
  std::vector<std::uint8_t> bytesA, bytesB;
  a.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.setExtras(e).ok());
    CHECK(s.storeSnapshot(0).ok());
    bytesA = cf::pattern::serialize(*s.pattern());
    cf::pattern::MutateOptions o;
    o.seed = 7;
    o.amount = 1.0;
    CHECK(s.mutate(o).ok());
    CHECK(s.storeSnapshot(1).ok());
    bytesB = cf::pattern::serialize(*s.pattern());
  });
  CHECK(bytesA != bytesB);
  a.manualBpm = 97.5;
  a.embedSource = true;
  juce::MemoryBlock saved;
  a.getStateInformation(saved);
  CHECK_EQ(wrapperVersion(saved), 2);

  ChopFractalProcessor b;
  prepare(b);
  b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  waitForRestore(b);
  cf::composition::ProjectExtras got;
  b.withSession([&](cf::composition::ProjectSession& s) {
    got = s.extras();
    CHECK(s.hasSnapshot(0) && s.hasSnapshot(1));
    CHECK(s.recallSnapshot(0).ok());
    CHECK(cf::pattern::serialize(*s.pattern()) == bytesA);
    CHECK(s.recallSnapshot(1).ok());
    CHECK(cf::pattern::serialize(*s.pattern()) == bytesB);
  });
  CHECK(got == e);
  CHECK_NEAR(b.manualBpm.load(), 97.5, 1e-9);
  CHECK(b.embedSource.load());
  CHECK(!b.restoreInFlight());
  juce::MemoryBlock again;
  b.getStateInformation(again);
  CHECK_EQ(wrapperVersion(again), 2);

  // A wrapper version newer than this build is refused with the existing message and changes nothing.
  juce::MemoryBlock future = saved;
  static_cast<unsigned char*>(future.getData())[4] = 3;
  ChopFractalProcessor c;
  makeSession(c);
  const auto before = patternBytes(c);
  c.setStateInformation(future.getData(), static_cast<int>(future.getSize()));
  CHECK(c.statusMessage().contains("different version"));
  CHECK(patternBytes(c) == before);
}

CHOP_TEST(reference_only_restore_decodes_on_a_worker) {
  const juce::File dir = tempDir("chopfractal-f01-restore");
  const juce::File wav = writeDrumWav(dir, "restore_loop.wav", drumLoop());
  ChopFractalProcessor a;
  prepare(a);
  makeReferenceSession(a, wav);
  a.embedSource = false;
  juce::MemoryBlock saved;
  a.getStateInformation(saved);
  CHECK_EQ(wrapperVersion(saved), 1);

  ChopFractalProcessor b;
  prepare(b);
  const int decodesBefore = ChopFractalProcessor::decodeCallsOnThisThread();
  b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  CHECK_EQ(ChopFractalProcessor::decodeCallsOnThisThread(), decodesBefore);  // the calling thread decoded nothing
  CHECK(b.restoreInFlight());
  bool missing = false, patternThere = false;
  b.withSession([&](cf::composition::ProjectSession& s) {
    missing = s.sourceMissing();
    patternThere = s.pattern() != nullptr && s.chopMap() != nullptr;
    CHECK(s.playback() != nullptr && (s.playback()->passThrough || !s.playback()->source));  // usable, silent until the audio arrives
    CHECK(s.takeNotices().empty());                                                              // no premature "not found"
  });
  CHECK(missing);
  CHECK(patternThere);  // markers and pattern are there before the audio
  CHECK(b.statusMessage() == "Restoring audio from restore_loop.wav...");

  CHECK(waitFor([&] { return !b.restoreInFlight(); }));
  bool hasAudio = false;
  b.withSession([&](cf::composition::ProjectSession& s) {
    hasAudio = s.sourceData() != nullptr && !s.sourceMissing();
    CHECK(s.playback() && !s.playback()->passThrough && !s.playback()->events.empty());
  });
  CHECK(hasAudio);
  CHECK(b.statusMessage().startsWith("Restored audio"));
  CHECK(b.statusMessage() == "Restored audio: restore_loop.wav");
  CHECK(patternBytes(b) == patternBytes(a));
  CHECK(b.startPlayback());

  // A file that is gone: pattern and markers are kept, the existing warning text is shown.
  wav.deleteFile();
  ChopFractalProcessor c;
  prepare(c);
  c.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  waitForRestore(c);
  bool stillMissing = false;
  c.withSession([&](cf::composition::ProjectSession& s) { stillMissing = s.sourceMissing() && s.pattern() != nullptr; });
  CHECK(stillMissing);
  CHECK(c.statusMessage().startsWith("The source \"restore_loop\" was not found at "));
  CHECK(c.statusMessage().endsWith("Markers and pattern are kept; relink the file to hear playback."));

  // A different file under the same name: refused with the "not the audio" wording.
  auto other = std::make_shared<cf::render::SourceData>(*drumLoop());
  other->samples[5] += 0.25f;
  writeDrumWav(dir, "restore_loop.wav", other);
  ChopFractalProcessor d;
  prepare(d);
  d.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  waitForRestore(d);
  CHECK(d.statusMessage().startsWith("The file for \"restore_loop\" is not the audio this project was saved with."));
  bool dMissing = false;
  d.withSession([&](cf::composition::ProjectSession& s) { dMissing = s.sourceMissing(); });
  CHECK(dMissing);
  dir.deleteRecursively();
}

CHOP_TEST(stale_restore_result_is_dropped) {
  const juce::File dir = tempDir("chopfractal-f01-stale");
  const juce::File wav = writeDrumWav(dir, "stale_loop.wav", drumLoop());
  ChopFractalProcessor first;
  prepare(first);
  makeReferenceSession(first, wav);
  first.embedSource = false;
  juce::MemoryBlock refState;
  first.getStateInformation(refState);

  ChopFractalProcessor second;  // a different project: another seed, embedded audio
  prepare(second);
  makeSession(second, 777);
  second.embedSource = true;
  juce::MemoryBlock otherState;
  second.getStateInformation(otherState);

  ChopFractalProcessor p;
  prepare(p);
  p.setStateInformation(refState.getData(), static_cast<int>(refState.getSize()));
  CHECK(p.restoreInFlight());
  p.setStateInformation(otherState.getData(), static_cast<int>(otherState.getSize()));  // replaces the project while the decode runs
  waitForRestore(p);
  CHECK(patternBytes(p) == patternBytes(second));
  bool embedded = false;
  p.withSession([&](cf::composition::ProjectSession& s) { embedded = s.sourceData() != nullptr && s.chopMap() && s.chopMap()->info().path.empty(); });
  CHECK(embedded);                                                  // the second project's audio, untouched by the first project's late result
  CHECK(!p.statusMessage().startsWith("Restored audio"));

  // A user-initiated load (Locate Source / Load Loop) during the decode also wins.
  ChopFractalProcessor q;
  prepare(q);
  q.setStateInformation(refState.getData(), static_cast<int>(refState.getSize()));
  CHECK(q.restoreInFlight());
  q.loadFileAsync(wav, true);  // relink from the same file; bumps the epoch
  CHECK(waitFor([&] { return q.statusMessage() == "Relinked stale_loop.wav"; }));
  waitForRestore(q);
  CHECK(q.statusMessage() == "Relinked stale_loop.wav");  // the superseded restore did not overwrite the status
  bool ok = false;
  q.withSession([&](cf::composition::ProjectSession& s) { ok = !s.sourceMissing(); });
  CHECK(ok);
  dir.deleteRecursively();
}

CHOP_TEST(export_runs_on_a_worker_reports_progress_and_cancels) {
  const juce::File dir = tempDir("chopfractal-f01-export");
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  CHECK(stateIs(p, ExportStatus::State::Idle));
  const juce::File target = dir.getChildFile("long.wav");

  const double t0 = juce::Time::getMillisecondCounterHiRes();
  CHECK(p.startExport(longWavRequest(target)).ok());
  const double blockedMs = juce::Time::getMillisecondCounterHiRes() - t0;
  CHECK(blockedMs < 50.0);  // planning only; the render runs on the worker
  CHECK(stateIs(p, ExportStatus::State::Running));
  CHECK(p.exportDisplayName() == "long.wav");
  CHECK(p.statusMessage() == "Exporting long.wav...");

  // The fraction rises monotonically while the worker renders.
  float last = p.exportStatus().fraction;
  int rises = 0;
  for (int i = 0; i < 400 && stateIs(p, ExportStatus::State::Running) && rises < 3; ++i) {
    pump(10);
    const float f = p.exportStatus().fraction;
    CHECK(f >= last);
    if (f > last) ++rises;
    last = f;
  }
  CHECK(rises >= 1);
  CHECK(stateIs(p, ExportStatus::State::Running));

  auto second = p.startExport(longWavRequest(dir.getChildFile("second.wav")));
  CHECK(!second.ok() && second.error().code == cf::ErrorCode::Conflict && second.error().message == "An export is already running.");

  const double c0 = juce::Time::getMillisecondCounterHiRes();
  p.cancelExport();
  CHECK(waitFor([&] { return stateIs(p, ExportStatus::State::Cancelled); }, 3000));
  CHECK(juce::Time::getMillisecondCounterHiRes() - c0 < 1500.0);  // the worker stops within about a second
  CHECK(!target.exists());
  CHECK(!dir.getChildFile("second.wav").exists());
  CHECK(p.statusMessage() == "Export cancelled.");
  CHECK_EQ(dir.getNumberOfChildFiles(juce::File::findFiles), 0);  // no temp files left either

  // A following export succeeds.
  ExportRequest quick = longWavRequest(dir.getChildFile("quick.wav"));
  quick.wav.loops = 1;
  quick.wav.wav.sampleRate = 44100;
  quick.wav.tailSeconds = 0.5;
  CHECK(p.startExport(quick).ok());
  CHECK(waitFor([&] { return stateIs(p, ExportStatus::State::Done); }, 20000));
  CHECK(dir.getChildFile("quick.wav").existsAsFile());
  CHECK(p.statusMessage().startsWith("Exported quick.wav"));
  CHECK(p.exportStatus().fraction == 1.f);

  // Planning errors come back at once and nothing starts (the file exists and overwrite was not confirmed).
  auto exists = p.startExport(quick);
  CHECK(!exists.ok() && exists.error().code == cf::ErrorCode::Conflict);
  CHECK(stateIs(p, ExportStatus::State::Done));

  // Producer Kit: progress, completion text, and a cancelled kit leaves its directory empty.
  ExportRequest kit;
  kit.kind = ExportRequest::Kind::Kit;
  kit.path = dir.getChildFile("kit").getFullPathName().toStdString();
  CHECK(p.startExport(kit).ok());
  CHECK(waitFor([&] { return stateIs(p, ExportStatus::State::Done); }, 20000));
  CHECK(p.statusMessage().startsWith("Producer Kit written to kit ("));
  CHECK(p.statusMessage().endsWith(" files)"));
  CHECK(dir.getChildFile("kit").getNumberOfChildFiles(juce::File::findFiles) >= 3);
  dir.deleteRecursively();
}

CHOP_TEST(export_with_nothing_to_render_is_refused_without_starting_a_worker) {
  ChopFractalProcessor p;
  prepare(p);
  const juce::File dir = tempDir("chopfractal-f01-export-empty");
  auto st = p.startExport(longWavRequest(dir.getChildFile("none.wav")));
  CHECK(!st.ok() && st.error().message == "there is no pattern to render");
  CHECK(stateIs(p, ExportStatus::State::Idle));
  dir.deleteRecursively();
}

CHOP_TEST(processor_destruction_while_exporting_does_not_hang) {
  const juce::File dir = tempDir("chopfractal-f01-destroy");
  auto p = std::make_unique<ChopFractalProcessor>();
  prepare(*p);
  makeSession(*p);
  ExportRequest r = longWavRequest(dir.getChildFile("never.wav"));
  r.wav.tailSeconds = 30.0;
  CHECK(p->startExport(r).ok());
  pump(30);
  CHECK(stateIs(*p, ExportStatus::State::Running));
  const auto t0 = std::chrono::steady_clock::now();
  p.reset();  // cancels and joins the worker
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK(seconds < 6.0);
  pump(50);  // a pending completion callback must find the processor gone and do nothing
  CHECK(!dir.getChildFile("never.wav").exists());
  dir.deleteRecursively();
}

CHOP_TEST(destroying_the_processor_during_a_restore_does_not_hang) {
  const juce::File dir = tempDir("chopfractal-f01-destroy-restore");
  const juce::File wav = writeDrumWav(dir, "d.wav", drumLoop());
  juce::MemoryBlock state;
  {
    ChopFractalProcessor a;
    prepare(a);
    makeReferenceSession(a, wav);
    a.embedSource = false;
    a.getStateInformation(state);
  }
  auto p = std::make_unique<ChopFractalProcessor>();
  prepare(*p);
  p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  p.reset();
  pump(100);
  dir.deleteRecursively();
}

CHOP_TEST(editor_restores_widgets_from_state_and_shows_export_progress) {
  if (!haveDisplay()) return;
  const juce::File dir = tempDir("chopfractal-f01-gui");
  juce::MemoryBlock state;
  {
    ChopFractalProcessor p;
    prepare(p);
    makeSession(p);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor != nullptr);
    if (!editor) return;
    editor->setVisible(true);
    pump(200);
    auto* motif = findTextEditor(*editor, "Fractal motif: x hit, a accent, dot rest");
    auto* depth = findCombo(*editor, "Fractal depth");
    auto* seed = findTextEditor(*editor, "Seed");
    auto* role = findCombo(*editor, "Role for the selected hit's chop");
    auto* detect = findCombo(*editor, "Detection mode");
    auto* format = findCombo(*editor, "Export format");
    auto* edit = findButton(*editor, "Move / resize hits");
    CHECK(motif && depth && seed && role && detect && format && edit);
    if (!(motif && depth && seed && role && detect && format && edit)) return;
    motif->setText("xa.x", true);
    depth->setSelectedId(3, juce::sendNotification);
    seed->setText("987654321", true);
    role->setSelectedId(4, juce::sendNotification);
    detect->setSelectedId(3, juce::sendNotification);
    format->setSelectedId(1, juce::sendNotification);
    edit->setToggleState(true, juce::sendNotification);
    pump(100);  // TextEditor change messages are asynchronous
    p.withSession([&](cf::composition::ProjectSession& s) {
      const auto& e = s.extras();
      CHECK(e.motif == "xa.x");
      CHECK_EQ(e.fractalDepth, 3);
      CHECK_EQ(e.seed, 987654321u);
      CHECK_EQ(e.roleIndex, 3);
      CHECK_EQ(e.detectMode, 2);
      CHECK_EQ(e.exportUi.formatIndex, 0);
      CHECK(e.ui.editHits);
    });
    if (auto* generate = findButton(*editor, "Generate")) generate->onClick();
    p.embedSource = true;
    p.getStateInformation(state);
    CHECK_EQ(wrapperVersion(state), 2);
  }
  {
    ChopFractalProcessor p;
    prepare(p);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    waitForRestore(p);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor != nullptr);
    if (!editor) return;
    editor->setVisible(true);
    pump(250);
    auto* motif = findTextEditor(*editor, "Fractal motif: x hit, a accent, dot rest");
    auto* depth = findCombo(*editor, "Fractal depth");
    auto* seed = findTextEditor(*editor, "Seed");
    auto* role = findCombo(*editor, "Role for the selected hit's chop");
    auto* detect = findCombo(*editor, "Detection mode");
    auto* format = findCombo(*editor, "Export format");
    auto* edit = findButton(*editor, "Move / resize hits");
    CHECK(motif && depth && seed && role && detect && format && edit);
    if (!(motif && depth && seed && role && detect && format && edit)) return;
    CHECK(motif->getText() == "xa.x");
    CHECK_EQ(depth->getSelectedId(), 3);
    CHECK(seed->getText() == "987654321");
    CHECK_EQ(role->getSelectedId(), 4);
    CHECK_EQ(detect->getSelectedId(), 3);
    CHECK_EQ(format->getSelectedId(), 1);
    CHECK(edit->getToggleState());

    // A project loaded while the editor is open re-syncs the widgets (Change::Extras).
    ChopFractalProcessor other;
    prepare(other);
    makeSession(other);
    other.embedSource = true;
    juce::MemoryBlock plain;
    other.getStateInformation(plain);
    p.setStateInformation(plain.getData(), static_cast<int>(plain.getSize()));
    pump(250);
    CHECK(motif->getText() == "x.xx");
    CHECK_EQ(depth->getSelectedId(), 2);
    CHECK(seed->getText() == "1");
    CHECK(!edit->getToggleState());
  }
  {
    // Export progress in the status bar, the Cancel Export button and the disabled export buttons.
    ChopFractalProcessor p;
    prepare(p);
    makeSession(p);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor != nullptr);
    if (!editor) return;
    editor->setVisible(true);
    pump(250);
    auto* cancel = findButton(*editor, "Cancel Export");
    auto* exportWav = findButton(*editor, "Export WAV");
    auto* exportKit = findButton(*editor, "Export Kit");
    CHECK(cancel && exportWav && exportKit);
    if (!(cancel && exportWav && exportKit)) return;
    CHECK(!cancel->isVisible());
    CHECK(exportWav->isEnabled() && exportKit->isEnabled());
    ExportRequest r = longWavRequest(dir.getChildFile("gui.wav"));
    r.wav.tailSeconds = 30.0;
    CHECK(p.startExport(r).ok());
    CHECK(waitFor([&] { return cancel->isVisible(); }, 3000));
    CHECK(!exportWav->isEnabled() && !exportKit->isEnabled());
    CHECK_EQ(cancel->getWidth(), 106);
    CHECK_EQ(cancel->getHeight(), 16);
    CHECK(waitFor([&] { return p.statusMessage().startsWith("Exporting gui.wav: ") && p.statusMessage().endsWith("%"); }, 3000));
    cancel->onClick();
    CHECK(waitFor([&] { return stateIs(p, ExportStatus::State::Cancelled); }, 3000));
    CHECK(waitFor([&] { return !cancel->isVisible() && exportWav->isEnabled() && exportKit->isEnabled(); }, 3000));
    CHECK(!dir.getChildFile("gui.wav").exists());
    CHECK(p.statusMessage() == "Export cancelled.");
  }
  dir.deleteRecursively();
}
