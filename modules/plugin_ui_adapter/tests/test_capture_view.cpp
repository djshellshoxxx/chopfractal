#include <chopfractal/plugin_ui_adapter/capture_view.hpp>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;
using chopfractal::capture::CaptureMode;
using chopfractal::capture::CaptureState;
using chopfractal::capture::EndReason;

namespace {
CaptureUiInput base(CaptureState st) {
  CaptureUiInput in;
  in.status.state = st;
  return in;
}
}  // namespace

CHOP_TEST(capture_view_enable_rules_follow_the_state) {
  struct Row { CaptureState st; bool arm, capture, stop, keep, discard, options; };
  const Row rows[] = {
      {CaptureState::Idle, true, false, false, false, false, true},
      {CaptureState::Armed, false, true, true, false, false, false},
      {CaptureState::Capturing, false, false, true, false, false, false},
      {CaptureState::Captured, true, false, false, true, true, true},
  };
  for (const Row& r : rows) {
    CaptureUiInput in = base(r.st);
    in.status.framesCaptured = r.st == CaptureState::Captured ? 96000 : 0;
    const CaptureView v = buildCaptureView(in);
    CHECK(v.armEnabled == r.arm);
    CHECK(v.captureEnabled == r.capture);
    CHECK(v.stopEnabled == r.stop);
    CHECK(v.keepEnabled == r.keep);
    CHECK(v.discardEnabled == r.discard);
    CHECK(v.optionsEnabled == r.options);
  }
  CaptureUiInput in = base(CaptureState::Captured);
  in.status.framesCaptured = 96000;
  CHECK(buildCaptureView(in).armNeedsConfirm);
  in.status.framesCaptured = 0;
  CHECK(!buildCaptureView(in).keepEnabled);  // nothing was recorded
  CHECK(!buildCaptureView(base(CaptureState::Idle)).armNeedsConfirm);
}

CHOP_TEST(capture_view_labels_match_the_spec_text) {
  CaptureUiInput in = base(CaptureState::Idle);
  CHECK(buildCaptureView(in).stateLabel == "Idle");
  CHECK(buildCaptureView(in).lengthLabel == "4 bars = 8.0 s at 120.0 BPM");
  in.bars = 8;
  in.bpm = 90.0;
  CHECK(buildCaptureView(in).lengthLabel == "8 bars = 21.3 s at 90.0 BPM");
  in.mode = CaptureMode::Free;
  CHECK(buildCaptureView(in).lengthLabel == "Keeps the last 30 s");
  CHECK(buildCaptureView(in).showMaxLength && !buildCaptureView(in).showBars);

  in = base(CaptureState::Armed);
  CHECK(buildCaptureView(in).stateLabel == "Armed: waiting for the host to play");
  in.hostPlaying = true;
  CHECK(buildCaptureView(in).stateLabel == "Armed: starts at the next bar");
  in.hostHasPosition = false;
  CHECK(buildCaptureView(in).stateLabel.find("Use Free mode") != std::string::npos);
  in.mode = CaptureMode::Free;
  CHECK(buildCaptureView(in).stateLabel == "Armed: press Capture to start");

  in = base(CaptureState::Capturing);
  in.status.framesCaptured = 153600;  // 3.2 s
  CHECK(buildCaptureView(in).stateLabel == "Capturing 00:03.2 / 00:08.0");
  in.mode = CaptureMode::Free;
  CHECK(buildCaptureView(in).stateLabel == "Capturing 00:03.2 (keeping the last 30 s)");

  in = base(CaptureState::Captured);
  in.status.framesCaptured = 384000;
  in.status.capacityFrames = 480001;
  CHECK(buildCaptureView(in).stateLabel == "Captured 8.0 s, stereo, 48 kHz. Keep to chop it, or Discard.");
  in.channels = 1;
  in.sampleRate = 44100.0;
  in.status.framesCaptured = 441000;
  CHECK(buildCaptureView(in).stateLabel == "Captured 10.0 s, mono, 44.1 kHz. Keep to chop it, or Discard.");
  in.keepInFlight = true;
  CHECK(buildCaptureView(in).stateLabel == "Keeping capture...");
  CHECK(!buildCaptureView(in).keepEnabled && !buildCaptureView(in).armEnabled);
}

CHOP_TEST(capture_view_time_format) {
  CHECK(formatCaptureTime(0.0) == "00:00.0");
  CHECK(formatCaptureTime(3.24) == "00:03.2");
  CHECK(formatCaptureTime(61.95) == "01:02.0");
  CHECK(formatCaptureTime(-4.0) == "00:00.0");
  CHECK(formatCaptureTime(1e300 * 1e300) == "00:00.0");
}

CHOP_TEST(capture_view_blocked_and_no_input_disable_arm_with_a_reason) {
  CaptureUiInput in = base(CaptureState::Idle);
  in.noInput = true;
  CaptureView v = buildCaptureView(in);
  CHECK(!v.armEnabled && v.disabledReason == "Capture needs an audio input. Your host gave this plugin no input channels.");
  in.noInput = false;
  in.blocked = true;
  v = buildCaptureView(in);
  CHECK(!v.armEnabled && v.disabledReason == "Capture is not available during an offline render.");
}

CHOP_TEST(capture_view_keep_confirmation_and_undo_visibility) {
  CaptureUiInput in = base(CaptureState::Captured);
  in.status.framesCaptured = 1000;
  CHECK(!buildCaptureView(in).keepNeedsConfirm);
  in.hasSource = true;
  CHECK(!buildCaptureView(in).keepNeedsConfirm);  // a source without a pattern loses nothing
  in.hasPattern = true;
  CaptureView v = buildCaptureView(in);
  CHECK(v.keepNeedsConfirm);
  CHECK(v.keepConfirmTitle == "Replace the current source?");
  CHECK(v.keepConfirmMessage == "Keeping this capture replaces the source. Your pattern and variations will be cleared. You can undo this with Undo Keep.");
  in = base(CaptureState::Idle);
  in.canUndoKeep = true;
  CHECK(buildCaptureView(in).undoKeepVisible);
  in.status.state = CaptureState::Armed;
  CHECK(!buildCaptureView(in).undoKeepVisible);  // the undo window ends when a new capture starts
}

CHOP_TEST(capture_view_meter_and_clip_indicator_do_not_rely_on_colour) {
  CaptureUiInput in = base(CaptureState::Armed);
  in.status.level = 0.5f;
  in.status.peak = 2.0f;
  CaptureView v = buildCaptureView(in);
  CHECK(v.meter == 0.5f && v.meterPeak == 1.f && !v.clipped && v.meterText.empty());
  in.status.clippedSamples = 12;
  v = buildCaptureView(in);
  CHECK(v.clipped && v.meterText == "Clipped" && v.meterTooltip == "12 samples clipped");
}

CHOP_TEST(capture_end_notices_use_the_spec_wording) {
  CHECK(captureEndNotice(EndReason::TransportStopped, 3.2).text == "Capture stopped: the host transport stopped or jumped. Kept 3.2 s.");
  CHECK(captureEndNotice(EndReason::TransportStopped, 3.2).level == CaptureNotice::Level::Warning);
  CHECK(captureEndNotice(EndReason::Overflow, 1.0).text == "Capture ended early: the tempo changed during capture.");
  CHECK(captureEndNotice(EndReason::Reconfigured, 1.0).text == "Capture cancelled: the audio settings changed.");
  CHECK(captureEndNotice(EndReason::Completed, 8.0).level == CaptureNotice::Level::None);
  CHECK(captureEndNotice(EndReason::UserStop, 8.0).text.empty());
  CHECK(captureKeptRingText(true, 30.0) == "Kept the last 30 s of the recording.");
  CHECK(captureKeptRingText(false, 30.0).empty());
}
