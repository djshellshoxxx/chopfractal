// Input capture (F12) through the real processor: arm, record from processBlock, keep on the worker, undo, save and
// restore. The editor's capture controls are wired by the GUI work; these tests drive the same processor commands.
#include <cmath>
#include <string>

#include "PluginProcessor.h"
#include "chop_test.hpp"
#include "shell_test_support.hpp"

namespace cf = chopfractal;
using namespace shelltest;

namespace {

// Feeds `blocks` host blocks of a 1 kHz sine (continuing from `firstFrame`) and advances the playhead.
void feed(ChopFractalProcessor& p, FakePlayHead& ph, int blocks, std::int64_t& frame, int block = 512, std::vector<float>* leftIn = nullptr, std::vector<float>* leftOut = nullptr) {
  p.setPlayHead(&ph);
  juce::MidiBuffer midi;
  for (int b = 0; b < blocks; ++b) {
    juce::AudioBuffer<float> buf(2, block);
    for (int i = 0; i < block; ++i) {
      const float x = 0.5f * std::sin(2.f * 3.14159265f * 1000.f * static_cast<float>(frame + i) / 48000.f);
      buf.setSample(0, i, x);
      buf.setSample(1, i, -x);
      if (leftIn) leftIn->push_back(x);
    }
    p.processBlock(buf, midi);
    if (leftOut)
      for (int i = 0; i < block; ++i) leftOut->push_back(buf.getSample(0, i));
    frame += block;
    ph.ppq += static_cast<double>(block) * ph.bpm / (60.0 * 48000.0);
  }
  p.setPlayHead(nullptr);
}

bool contains(const juce::String& s, const char* needle) { return s.contains(needle); }
}  // namespace

CHOP_TEST(capture_flow_through_the_real_processor) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 30.0).ok());
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Armed);
  feed(p, ph, 20, frame);
  CHECK(p.captureEngine().status().framesCaptured == 0);
  CHECK(p.startCapture().ok());
  feed(p, ph, 100, frame);
  p.stopCapture();
  feed(p, ph, 1, frame);
  const auto st = p.captureEngine().status();
  CHECK(st.state == cf::capture::CaptureState::Captured);
  CHECK(st.framesCaptured == 51200);
  CHECK(st.peak > 0.4f && st.clippedSamples == 0);

  p.keepCaptureAsync(true);
  CHECK(p.captureKeepInFlight() || p.statusMessage().isNotEmpty());
  CHECK(waitFor([&] { return contains(p.statusMessage(), "Captured audio is saved"); }));
  CHECK(!p.captureKeepInFlight());
  bool embedded = false;
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.hasSource());
    CHECK(s.chopMap()->info().name.rfind("Capture ", 0) == 0);
    CHECK(s.chopMap()->info().frames == 51200);
    CHECK(s.chopMap()->info().path.empty());
    embedded = s.sourceNeedsEmbed();
  });
  CHECK(embedded);
  CHECK(p.embedSource.load());
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Idle);

  // The kept audio has no file: it must travel inside the project, even if the user never ticked "embed".
  p.embedSource = false;
  juce::MemoryBlock saved;
  p.getStateInformation(saved);
  ChopFractalProcessor q;
  prepare(q);
  q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  waitForRestore(q);
  q.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.hasSource() && !s.sourceMissing());
    CHECK(s.chopMap()->info().frames == 51200);
  });
  CHECK(q.embedSource.load());
}

CHOP_TEST(capture_records_the_input_not_the_rendered_output) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;  // playing: the renderer overwrites the buffer it was given
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 30.0).ok());
  CHECK(p.startCapture().ok());
  std::vector<float> in, out;
  feed(p, ph, 40, frame, 512, &in, &out);
  p.stopCapture();
  feed(p, ph, 1, frame);
  auto r = p.captureEngine().take();
  CHECK(r.ok());
  if (!r.ok()) return;
  CHECK(r.value().frames == 40 * 512);
  bool same = true, outDiffers = false;
  for (std::size_t i = 0; i < in.size(); ++i) {
    same = same && r.value().samples[i] == in[i];
    outDiffers = outDiffers || out[i] != in[i];
  }
  CHECK(same);
  CHECK(outDiffers);  // the pattern was really playing over the same buffer
}

CHOP_TEST(capture_undo_keep_restores_previous_source) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p, 42, "/nonexistent/loop.wav");
  const auto before = patternBytes(p);
  CHECK(!before.empty());
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  CHECK(p.startCapture().ok());
  feed(p, ph, 30, frame);
  p.stopCapture();
  feed(p, ph, 1, frame);
  p.embedSource = false;
  p.keepCaptureAsync(false);
  CHECK(waitFor([&] { return contains(p.statusMessage(), "Captured audio is saved"); }));
  CHECK(p.canUndoKeep());
  CHECK(patternBytes(p).empty());  // the pattern was cleared by the replace
  CHECK(p.undoKeep());
  CHECK(!p.canUndoKeep());
  CHECK(patternBytes(p) == before);
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.chopMap()->info().name == "loop");
    CHECK(!s.sourceNeedsEmbed());
  });
  CHECK(!p.embedSource.load());
  CHECK(!p.undoKeep());  // one level only
}

CHOP_TEST(keep_into_an_empty_project_can_be_undone_to_nothing) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  CHECK(p.startCapture().ok());
  feed(p, ph, 10, frame);
  p.stopCapture();
  feed(p, ph, 1, frame);
  p.keepCaptureAsync(false);
  CHECK(waitFor([&] { return p.canUndoKeep(); }));
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.hasSource()); });
  CHECK(p.undoKeep());
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.hasSource()); });
}

CHOP_TEST(a_new_source_ends_the_undo_window) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  CHECK(p.startCapture().ok());
  feed(p, ph, 10, frame);
  p.stopCapture();
  feed(p, ph, 1, frame);
  p.keepCaptureAsync(false);
  CHECK(waitFor([&] { return p.canUndoKeep(); }));
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.loadSource(drumLoop(), "other", "").ok()); });
  CHECK(!p.undoKeep());  // the kept capture is no longer the source
  CHECK(!p.canUndoKeep());
}

CHOP_TEST(capture_is_refused_with_no_input_channels) {
  ChopFractalProcessor p;
  prepare(p);
  p.overrideInputChannelsForTesting(0);  // the bus layout rules never allow this; hosts that did would hit the refusal
  const cf::Status st = p.armCapture(cf::capture::CaptureMode::SyncedBars, 4, 30.0);
  CHECK(!st.ok());
  CHECK(p.statusMessage() == "Capture needs an audio input. Your host gave this plugin no input channels.");
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Idle);
}

CHOP_TEST(capture_is_refused_and_skipped_during_an_offline_render) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  CHECK(p.startCapture().ok());
  p.setNonRealtime(true);
  CHECK(p.captureBlocked());
  feed(p, ph, 10, frame);
  CHECK(p.captureEngine().status().framesCaptured == 0);  // offline blocks are never recorded
  p.stopCapture();
  p.setNonRealtime(false);
  feed(p, ph, 1, frame);
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Idle);
  p.setNonRealtime(true);
  CHECK(!p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  CHECK(p.statusMessage() == "Capture is not available during an offline render.");
  p.setNonRealtime(false);
}

CHOP_TEST(synced_capture_follows_the_host_and_reports_why_it_ended) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  ph.ppq = 0.0;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::SyncedBars, 1, 30.0).ok());
  feed(p, ph, 30, frame);  // 0.32 s from a bar line: capturing
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Capturing);
  ph.playing = false;
  feed(p, ph, 1, frame);
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Captured);
  CHECK(p.captureEngine().status().ended == cf::capture::EndReason::TransportStopped);
  p.pollAudioFlags();
  CHECK(contains(p.statusMessage(), "Capture stopped: the host transport stopped or jumped. Kept 0.3 s."));
  p.discardCapture();
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Idle);

  // A completed one-bar capture is exactly one bar long at 120 BPM.
  ph.playing = true;
  ph.ppq = 0.5;
  CHECK(p.armCapture(cf::capture::CaptureMode::SyncedBars, 1, 30.0).ok());
  feed(p, ph, 400, frame);
  const auto done = p.captureEngine().status();
  CHECK(done.state == cf::capture::CaptureState::Captured && done.ended == cf::capture::EndReason::Completed);
  CHECK(done.framesCaptured >= 95999 && done.framesCaptured <= 96001);
}

CHOP_TEST(changing_the_sample_rate_cancels_a_running_capture) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  p.prepareToPlay(48000.0, 256);  // the same settings (a host restarting audio): the capture continues
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Armed);
  p.setPlayConfigDetails(2, 2, 44100.0, 512);
  p.prepareToPlay(44100.0, 512);
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Idle);
  CHECK(p.statusMessage() == "Capture cancelled: the audio settings changed.");
  (void)frame;
}

CHOP_TEST(a_kept_capture_too_large_to_embed_says_so_and_saves_without_audio) {
  ChopFractalProcessor p;
  prepare(p);
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  CHECK(p.startCapture().ok());
  feed(p, ph, 20, frame);
  p.stopCapture();
  feed(p, ph, 1, frame);
  p.keepCaptureAsync(false);
  CHECK(waitFor([&] { return p.canUndoKeep(); }));
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.setEmbeddedSourceCap(1024).ok()); });
  juce::MemoryBlock saved;
  p.getStateInformation(saved);
  CHECK(contains(p.statusMessage(), "The captured audio is too large to embed ("));
  CHECK(contains(p.statusMessage(), "Raise the embed limit or keep a shorter capture."));
  CHECK(saved.getSize() > 0);
}

CHOP_TEST(arming_while_an_unkept_capture_waits_replaces_it_and_keep_without_a_capture_is_harmless) {
  ChopFractalProcessor p;
  prepare(p);
  p.keepCaptureAsync(true);
  CHECK(p.statusMessage() == "There is no capture to keep.");
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());
  CHECK(p.startCapture().ok());
  feed(p, ph, 4, frame);
  p.stopCapture();
  feed(p, ph, 1, frame);
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Captured);
  CHECK(p.armCapture(cf::capture::CaptureMode::Free, 4, 10.0).ok());  // the editor has asked "Discard the unsaved capture?" already
  CHECK(p.captureEngine().status().state == cf::capture::CaptureState::Armed);
  CHECK(p.captureEngine().status().framesCaptured == 0);
}
