#pragma once
// Real-time-safe recorder for the plugin's audio input: Arm, record (optionally exactly N bars starting on a bar
// line), Stop, then take the audio as a planar buffer that the session loads like any other source.
//
// Threads: arm / stop / cancel / take / state queries belong to ONE control thread (the message thread);
// process() belongs to the audio thread. They communicate only through atomics, and the audio thread never
// allocates, locks or waits. Everything is sample-accurate and independent of the host block size.
//
// If the host is not calling process() (a suspended or bypassed track) a requested stop or cancel cannot take
// effect until it resumes; state() tells the UI what is really happening.
#include <chopfractal/chop_contracts/result.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chopfractal::capture {

constexpr double kMaxSeconds = 60.0;                      // matches the source import limit
constexpr std::size_t kMaxBytes = 100u * 1024u * 1024u;   // decoded size limit shared with file import

enum class State : int { Idle = 0, Armed = 1, Recording = 2, Done = 3 };
enum class StopReason : int { None = 0, Manual = 1, LengthReached = 2, BufferFull = 3 };

struct Options {
  int channels = 2;                  // 1 or 2 (input channels beyond 2 are ignored; a mono input fills both of a stereo capture)
  double sampleRate = 48000.0;
  std::int64_t targetFrames = 0;     // 0: free length, until Stop or the 60 second limit
  bool waitForBar = false;           // start at the next bar line when the host is playing with a valid position
};

// What process() needs to know about the host for one block.
struct Transport {
  bool playing = false;
  bool positionValid = false;
  double ppq = 0.0;                  // quarter notes at the first frame of the block
  double bpm = 120.0;
  double quartersPerBar = 4.0;
};

struct Captured {
  int channels = 0;
  int sampleRate = 0;
  std::int64_t frames = 0;
  std::vector<float> samples;        // planar, channel-major: samples[c * frames + i]
  StopReason reason = StopReason::None;
};

class Capture {
 public:
  // ---- control thread ----
  // Allocates the buffer. Fails while Armed or Recording, or when the options are invalid or too large.
  Status arm(const Options& options);
  void stop();                       // Armed -> Idle; Recording -> Done (applied by the next process() call)
  void cancel();                     // discard whatever is armed, recorded or waiting; back to Idle
  // Only in the Done state: moves the audio out (compacted) and returns to Idle.
  Result<Captured> take();
  State state() const { return static_cast<State>(state_.load(std::memory_order_acquire)); }
  StopReason reason() const { return static_cast<StopReason>(reason_.load(std::memory_order_acquire)); }
  std::int64_t framesRecorded() const { return written_.load(std::memory_order_relaxed); }
  std::int64_t targetFrames() const { return target_.load(std::memory_order_relaxed); }  // 0 when free length
  double sampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }

  // ---- audio thread ----
  // `in` may contain fewer channels than armed (the first is repeated), null pointers (silence) or non-finite
  // samples (written as 0). Must be called before the input buffer is overwritten with output.
  void process(const float* const* in, int numChannels, int frames, const Transport& transport);

 private:
  void finish(StopReason why);
  void write(const float* const* in, int numChannels, int from, int count);

  std::atomic<int> state_{0};
  std::atomic<int> reason_{0};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> cancelRequested_{false};
  std::atomic<std::int64_t> written_{0};
  std::atomic<std::int64_t> target_{0};
  std::atomic<double> sampleRate_{48000.0};

  // Set by arm() before it publishes Armed; read by the audio thread only after it saw Armed (acquire).
  std::vector<float> buffer_;        // planar, channel stride = capacity_
  std::int64_t capacity_ = 0;
  int channels_ = 0;
  bool waitForBar_ = false;
};

// Frames in `bars` bars at `bpm` and `sampleRate`: bars x quartersPerBar x 60 / bpm x sampleRate, rounded to the
// nearest frame. Returns 0 for invalid input.
std::int64_t framesForBars(int bars, double quartersPerBar, double bpm, double sampleRate);

}  // namespace chopfractal::capture
