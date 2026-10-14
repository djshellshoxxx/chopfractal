#pragma once
// Real-time-safe input capture: a four-state machine (Idle, Armed, Capturing, Captured) that records the
// track input either from the next bar line (SyncedBars) or until stopped, keeping the last N seconds (Free).
// The buffer is allocated on arm() (message thread); process() never allocates, locks or logs.
#include <chopfractal/chop_contracts/result.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace chopfractal::capture {

enum class CaptureState : std::uint8_t { Idle = 0, Armed = 1, Capturing = 2, Captured = 3 };
enum class CaptureMode : std::uint8_t { SyncedBars, Free };
enum class EndReason : std::uint8_t { None, Completed, UserStop, TransportStopped, Overflow, Reconfigured };

constexpr double kMaxCaptureSeconds = 60.0;
constexpr std::int64_t kMaxCaptureBytes = 100ll * 1024 * 1024;
constexpr std::int64_t kMaxCaptureFrames = 60ll * 192000;
constexpr double kClipThreshold = 0.999;

struct ArmSettings {
  CaptureMode mode = CaptureMode::SyncedBars;
  int channels = 2;            // 1 or 2
  double sampleRate = 48000.0;  // 8000..384000
  int bars = 4;                // SyncedBars: 1..8
  double barQuarters = 4.0;    // quarter notes per bar (numerator * 4 / denominator)
  double bpmHint = 120.0;      // sizes the buffer; the real length follows the host position
  double maxSeconds = 30.0;    // Free: ring length, 1..60
};

struct CaptureTransport {
  bool playing = false;
  bool positionValid = false;
  double ppq = 0.0;
  double bpm = 120.0;
};

struct CaptureStatus {
  CaptureState state = CaptureState::Idle;
  EndReason ended = EndReason::None;
  std::int64_t framesCaptured = 0;
  std::int64_t capacityFrames = 0;
  float peak = 0.f;
  std::uint32_t clippedSamples = 0;
};

struct CapturedAudio {
  int channels = 0;
  int sampleRate = 0;
  std::int64_t frames = 0;
  std::vector<float> samples;  // planar: channel 0 frames, then channel 1 frames
  EndReason ended = EndReason::None;
  bool wrapped = false;  // Free mode: older audio was overwritten
};

// Largest capture (in frames) for a channel count and sample rate.
std::int64_t maxCaptureFrames(int channels, double sampleRate);

class CaptureEngine {
 public:
  CaptureEngine();
  ~CaptureEngine();
  CaptureEngine(const CaptureEngine&) = delete;
  CaptureEngine& operator=(const CaptureEngine&) = delete;

  // ---- message thread ----
  Status arm(const ArmSettings& settings);  // Idle -> Armed; Conflict otherwise
  Status startNow();                        // Armed -> Capturing at the next block
  void requestStop();                       // Armed -> Idle, Capturing -> Captured (acted on at the next block)
  void discard();                           // Captured -> Idle (frees the buffer)
  Result<CapturedAudio> take();             // Captured -> Idle, moving the audio out
  CaptureStatus status() const;
  float takeRecentPeak();  // largest |sample| since the previous call (for a decaying meter); resets to 0
  void reap();                              // frees the buffer once the audio thread has returned to Idle

  // ---- prepareToPlay (audio stopped) ----
  // Aborts Armed (discarded) or Capturing (partial audio kept) when the format differs from the armed one.
  void onPrepare(double sampleRate, int channels);

  // ---- audio thread ----
  void process(const float* const* in, int numChannels, int frames, const CaptureTransport& t);

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace chopfractal::capture
