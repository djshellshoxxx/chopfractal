#pragma once
// View model for the input capture controls (F12): which buttons are enabled, every label, the meter, and the
// notices for the ways a capture can end. Pure functions of plain data, so the whole control surface is testable
// without a GUI. The editor reads a CaptureView each timer tick and draws it; the button handlers call the commands
// listed in docs/specs/roadmap/F12-input-capture.md section 12.
#include <chopfractal/input_capture/capture.hpp>

#include <cstdint>
#include <string>

namespace chopfractal::ui {

struct CaptureUiInput {
  capture::CaptureStatus status;
  capture::CaptureMode mode = capture::CaptureMode::SyncedBars;
  int bars = 4;                  // 1, 2, 4 or 8
  double maxSeconds = 30.0;      // Free mode: 10, 30 or 60
  double bpm = 120.0;            // the tempo the plugin is using right now (host tempo, else the manual one)
  double barQuarters = 4.0;      // quarter notes per bar of the host meter
  double sampleRate = 48000.0;
  int channels = 2;              // channels of the armed or captured audio (2 when idle and unknown)
  bool hostPlaying = false;
  bool hostHasPosition = true;   // the host reports a song position (ppq)
  bool hasSource = false;
  bool hasPattern = false;
  bool canUndoKeep = false;
  bool keepInFlight = false;     // the worker that moves the capture into the project is running
  bool blocked = false;          // an offline render is running
  bool noInput = false;          // the host gave the plugin no input channels
};

struct CaptureView {
  std::string stateLabel;        // "Idle", "Armed: ...", "Capturing 00:03.2 / 00:08.0", "Captured 8.0 s, stereo, 48 kHz. ..."
  std::string lengthLabel;       // "4 bars = 8.0 s at 120.0 BPM" (Synced) or "Keeps the last 30 s" (Free)
  bool armEnabled = false;
  bool armNeedsConfirm = false;  // Captured and unkept: ask "Discard the unsaved capture?" (buttons "Discard" / "Cancel") first
  bool captureEnabled = false;   // the Capture button (start now)
  bool stopEnabled = false;
  bool keepEnabled = false;
  bool discardEnabled = false;
  bool optionsEnabled = true;    // the mode, bars and max length boxes
  bool showBars = true;          // Synced only
  bool showMaxLength = false;    // Free only
  bool keepNeedsConfirm = false; // the loaded source has a pattern: ask before replacing it
  std::string keepConfirmTitle;
  std::string keepConfirmMessage;
  bool undoKeepVisible = false;
  float meter = 0.f;             // 0..1, from the most recent block
  float meterPeak = 0.f;         // 0..1, since arm
  bool clipped = false;
  std::string meterText;         // "Clipped" or empty
  std::string meterTooltip;      // "N samples clipped" or empty
  std::string disabledReason;    // why Arm is disabled (no input channels, offline render), else empty
  std::string emptyWaveText;     // the waveform's empty text
  std::string stripTooltip;      // "An unkept capture is lost when the project closes."
};

CaptureView buildCaptureView(const CaptureUiInput& in);

// "00:03.2" (minutes, seconds, tenths; negative and non-finite values show 00:00.0).
std::string formatCaptureTime(double seconds);
// The combo box items, in order, and the value each maps to.
constexpr int kCaptureBarsChoices[4] = {1, 2, 4, 8};
constexpr int kCaptureMaxSecondsChoices[3] = {10, 30, 60};
constexpr const char* kCaptureModeItems[2] = {"Synced to host (bars)", "Free (until Stop)"};

struct CaptureNotice {
  enum class Level : std::uint8_t { None, Info, Warning };
  Level level = Level::None;
  std::string text;              // empty when nothing needs saying
};
// What to tell the user when a capture ended by itself (or was aborted). `keptSeconds` is the captured length.
CaptureNotice captureEndNotice(capture::EndReason why, double keptSeconds);
// After Keep of a Free capture that wrapped: "Kept the last 30 s of the recording." (empty when it did not wrap).
std::string captureKeptRingText(bool wrapped, double seconds);
// The refusal texts of CaptureEngine::arm and of the processor, verbatim (tests pin them).
constexpr const char* kCaptureNoInputText = "Capture needs an audio input. Your host gave this plugin no input channels.";
constexpr const char* kCaptureOfflineText = "Capture is not available during an offline render.";
constexpr const char* kCaptureSavedInsideText = "Captured audio is saved inside the project.";
constexpr const char* kCaptureKeepingText = "Keeping capture...";
constexpr const char* kCaptureReconfiguredText = "Capture cancelled: the audio settings changed.";

}  // namespace chopfractal::ui
