#include <chopfractal/plugin_ui_adapter/capture_view.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::ui {
namespace {
std::string fmt(const char* f, double a) {
  char b[160];
  std::snprintf(b, sizeof b, f, a);
  return b;
}
std::string fmt2(const char* f, double a, double b2) {
  char b[200];
  std::snprintf(b, sizeof b, f, a, b2);
  return b;
}
const char* channelWord(int ch) { return ch == 1 ? "mono" : "stereo"; }
}  // namespace

std::string formatCaptureTime(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
  const long tenths = std::lround(seconds * 10.0);
  char b[32];
  std::snprintf(b, sizeof b, "%02ld:%02ld.%ld", tenths / 600, (tenths / 10) % 60, tenths % 10);
  return b;
}

CaptureNotice captureEndNotice(capture::EndReason why, double keptSeconds) {
  CaptureNotice n;
  switch (why) {
    case capture::EndReason::TransportStopped:
      n.level = CaptureNotice::Level::Warning;
      n.text = fmt("Capture stopped: the host transport stopped or jumped. Kept %.1f s.", keptSeconds);
      break;
    case capture::EndReason::Overflow:
      n.level = CaptureNotice::Level::Warning;
      n.text = "Capture ended early: the tempo changed during capture.";
      break;
    case capture::EndReason::Reconfigured:
      n.level = CaptureNotice::Level::Warning;
      n.text = kCaptureReconfiguredText;
      break;
    default: break;
  }
  return n;
}

std::string captureKeptRingText(bool wrapped, double seconds) {
  return wrapped ? fmt("Kept the last %.0f s of the recording.", seconds) : std::string();
}

CaptureView buildCaptureView(const CaptureUiInput& in) {
  using capture::CaptureMode;
  using capture::CaptureState;
  CaptureView v;
  const CaptureState st = in.status.state;
  const bool synced = in.mode == CaptureMode::SyncedBars;
  const double bpm = (std::isfinite(in.bpm) && in.bpm > 0.0) ? in.bpm : 120.0;
  const double quarters = (std::isfinite(in.barQuarters) && in.barQuarters > 0.0) ? in.barQuarters : 4.0;
  // The audio's own rate and channel count win over the plugin's current ones once a capture exists.
  const double sr = in.status.sampleRate > 0 ? static_cast<double>(in.status.sampleRate) : ((std::isfinite(in.sampleRate) && in.sampleRate > 0.0) ? in.sampleRate : 48000.0);
  const int channels = in.status.channels > 0 ? in.status.channels : in.channels;
  const double barSeconds = quarters * 60.0 / bpm;

  v.showBars = synced;
  v.showMaxLength = !synced;
  v.lengthLabel = synced ? fmt2("%.0f bars = %.1f s", in.bars, in.bars * barSeconds) + fmt(" at %.1f BPM", bpm)
                         : fmt("Keeps the last %.0f s", in.maxSeconds);
  v.emptyWaveText = "Drop an audio file, press Load, or press Capture to record the track input.";
  v.stripTooltip = "An unkept capture is lost when the project closes.";

  const double elapsed = static_cast<double>(in.status.framesCaptured) / sr;
  switch (st) {
    case CaptureState::Idle: v.stateLabel = "Idle"; break;
    case CaptureState::Armed:
      if (!synced) v.stateLabel = "Armed: press Capture to start";
      else if (!in.hostPlaying) v.stateLabel = "Armed: waiting for the host to play";
      else if (!in.hostHasPosition) v.stateLabel = "Armed: the host gives no song position. Use Free mode.";
      else v.stateLabel = "Armed: starts at the next bar";
      break;
    case CaptureState::Capturing:
      v.stateLabel = synced ? "Capturing " + formatCaptureTime(elapsed) + " / " + formatCaptureTime(in.bars * barSeconds)
                            : "Capturing " + formatCaptureTime(elapsed) + fmt(" (keeping the last %.0f s)", in.maxSeconds);
      break;
    case CaptureState::Captured: {
      const double kept = std::min(elapsed, static_cast<double>(in.status.capacityFrames) / sr);
      const double khz = sr / 1000.0;
      const bool whole = std::fabs(khz - std::round(khz)) < 1e-9;
      v.stateLabel = fmt("Captured %.1f s, ", kept) + channelWord(channels) + ", " + fmt(whole ? "%.0f" : "%.1f", khz) + " kHz. Keep to chop it, or Discard.";
      break;
    }
  }

  const bool usable = !in.blocked && !in.noInput;
  v.armEnabled = usable && !in.keepInFlight && (st == CaptureState::Idle || st == CaptureState::Captured);
  v.armNeedsConfirm = v.armEnabled && st == CaptureState::Captured;
  v.captureEnabled = st == CaptureState::Armed;
  v.stopEnabled = st == CaptureState::Armed || st == CaptureState::Capturing;
  v.keepEnabled = st == CaptureState::Captured && in.status.framesCaptured > 0 && !in.keepInFlight;
  v.discardEnabled = st == CaptureState::Captured && !in.keepInFlight;
  v.optionsEnabled = st == CaptureState::Idle || st == CaptureState::Captured;
  v.keepNeedsConfirm = v.keepEnabled && in.hasSource && in.hasPattern;
  if (v.keepNeedsConfirm) {
    v.keepConfirmTitle = "Replace the current source?";
    v.keepConfirmMessage = "Keeping this capture replaces the source. Your pattern and variations will be cleared. You can undo this with Undo Keep.";
  }
  v.undoKeepVisible = in.canUndoKeep && st != CaptureState::Armed && st != CaptureState::Capturing;
  if (in.noInput) v.disabledReason = kCaptureNoInputText;
  else if (in.blocked) v.disabledReason = kCaptureOfflineText;
  if (in.keepInFlight) v.stateLabel = kCaptureKeepingText;

  v.meter = std::min(1.f, std::max(0.f, in.status.level));
  v.meterPeak = std::min(1.f, std::max(0.f, in.status.peak));
  v.clipped = in.status.clippedSamples > 0;
  if (v.clipped) {
    v.meterText = "Clipped";
    v.meterTooltip = std::to_string(in.status.clippedSamples) + " samples clipped";
  }
  return v;
}

}  // namespace chopfractal::ui
