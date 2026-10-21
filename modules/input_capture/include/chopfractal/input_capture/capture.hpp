#pragma once
// Input capture engine: records the track's audio input into a preallocated buffer, free-running (a ring that keeps
// the last N seconds) or synced to the host's bar grid, so the plugin can chop a loop without a trip through a file.
//
// Threading
//   * process() is the only audio-thread call: no allocation, no locks, no I/O, no logging. While Idle or Captured it
//     costs one acquire load.
//   * arm(), startNow(), requestStop(), discard(), take(), reap() and status() are message-thread (or worker) calls.
//     take() may run on a worker, but only while the state is Captured, which the audio thread never touches.
//   * onPrepare() runs when audio is stopped (prepareToPlay).
// The buffer is owned by the engine. The message thread allocates it in arm() and touches it only while the state is
// Idle or Captured (acquire); the audio thread touches it only while Armed or Capturing. Every state change that hands the
// buffer across threads is a release store, so no further synchronization is needed.
#include <chopfractal/chop_contracts/result.hpp>

#include <atomic>
#include <cstdint>
#include <vector>

namespace chopfractal::capture {

enum class CaptureState : std::uint8_t { Idle = 0, Armed = 1, Capturing = 2, Captured = 3 };
enum class CaptureMode : std::uint8_t { SyncedBars, Free };
enum class EndReason : std::uint8_t { None, Completed, UserStop, TransportStopped, Overflow, Reconfigured };

struct ArmSettings {
  CaptureMode mode = CaptureMode::SyncedBars;
  int channels = 2;              // 1 or 2, the main input bus
  double sampleRate = 48000.0;   // 8000..384000
  int bars = 4;                  // SyncedBars: 1..8
  double barQuarters = 4.0;      // quarter notes per bar (numerator * 4 / denominator)
  double bpmHint = 120.0;        // sizes the buffer; the real length follows ppq
  double maxSeconds = 30.0;      // Free: ring length, 1..60
};

// What the host's transport says about the block being processed.
struct CaptureTransport {
  bool playing = false;
  bool positionValid = false;
  double ppq = 0.0;              // quarter notes at the first frame of the block
  double bpm = 120.0;
};

struct CaptureStatus {
  CaptureState state = CaptureState::Idle;
  EndReason ended = EndReason::None;     // why the last capture stopped (valid in Captured, kept until the next arm)
  std::int64_t framesCaptured = 0;       // frames written since the capture started (Free: may exceed the capacity)
  std::int64_t capacityFrames = 0;
  float peak = 0.f;                      // largest |sample| since arm
  float level = 0.f;                     // largest |sample| of the most recent block (for a meter)
  std::uint32_t clippedSamples = 0;      // samples with |x| >= 0.999 since arm
  int channels = 0;                      // of the current or last capture (0 before the first arm)
  int sampleRate = 0;
};

struct CapturedAudio {
  int channels = 0;
  int sampleRate = 0;
  std::int64_t frames = 0;
  std::vector<float> samples;  // planar, same layout as render::SourceData
  EndReason ended = EndReason::None;
  bool wrapped = false;        // Free mode: older audio was overwritten
};

constexpr double kMaxCaptureSeconds = 60.0;                       // == plugin kMaxSourceSeconds
constexpr std::int64_t kMaxCaptureBytes = 100ll * 1024 * 1024;    // == plugin kMaxDecodedBytes
constexpr std::int64_t kMaxCaptureFrames = 60ll * 192000;         // == source::kMaxSourceFrames
constexpr float kClipThreshold = 0.999f;

// Largest number of frames per channel a capture may hold at this rate and channel count.
std::int64_t maxCaptureFrames(double sampleRate, int channels);

class CaptureEngine {
 public:
  CaptureEngine();
  ~CaptureEngine();
  CaptureEngine(const CaptureEngine&) = delete;
  CaptureEngine& operator=(const CaptureEngine&) = delete;

  // ---- message thread (allocate here, never on the audio thread) ----
  // Idle -> Armed; allocates the buffer. Conflict when not Idle, InvalidArgument/OutOfRange for bad settings,
  // LimitExceeded (hint = the largest permitted frame count) when the capture would not fit, including "not enough memory".
  Status arm(const ArmSettings& s);
  // Armed -> Capturing at the next block (Free mode, or a manual start of a synced capture). Conflict when not Armed.
  Status startNow();
  // Armed -> Idle, Capturing -> Captured, both at the next block start. A no-op in other states.
  void requestStop();
  // Captured -> Idle (the buffer is released by reap()). A no-op in other states.
  void discard();
  // Captured only: unwraps the ring, compacts and moves the audio out; -> Idle. NotFound when Idle, Conflict when busy.
  Result<CapturedAudio> take();
  CaptureStatus status() const;
  // Frees the buffer once the state is Idle. Call from the 30 Hz timer.
  void reap();

  // ---- prepareToPlay (audio stopped) ----
  // A changed sample rate or channel count aborts Armed (to Idle) and Capturing (to Captured, partial audio kept) with
  // EndReason::Reconfigured. Returns true when it aborted something.
  bool onPrepare(double sampleRate, int channels);

  // ---- audio thread: no allocation, no locks, no logging ----
  void process(const float* const* in, int numChannels, int frames, const CaptureTransport& t);

 private:
  void finish(EndReason why);
  void write(const float* const* in, int numChannels, int begin, int end);

  std::atomic<std::uint8_t> state_{0};
  std::atomic<std::uint8_t> ended_{0};
  std::atomic<bool> startReq_{false};
  std::atomic<bool> stopReq_{false};
  std::atomic<std::int64_t> framesCaptured_{0};
  std::atomic<std::int64_t> capacity_{0};
  std::atomic<float> peak_{0.f};
  std::atomic<float> level_{0.f};
  std::atomic<std::uint32_t> clipped_{0};
  std::atomic<int> channelsPub_{0};
  std::atomic<int> sampleRatePub_{0};

  // Written by arm() before the release store of Armed; afterwards owned by the audio thread until Captured/Idle.
  ArmSettings settings_;
  std::vector<float> buf_;       // planar, stride cap_
  std::int64_t cap_ = 0;
  // Audio-thread state while Armed/Capturing.
  std::int64_t total_ = 0;       // frames written
  std::int64_t writePos_ = 0;
  bool useFrames_ = false;       // manual start without a running transport: the end is a frame count
  std::int64_t endFrames_ = 0;
  double endPpq_ = 0.0;
  double expectedPpq_ = 0.0;
};

}  // namespace chopfractal::capture
