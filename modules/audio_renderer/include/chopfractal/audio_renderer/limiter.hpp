#pragma once
// Safety limiter (docs/specs/roadmap/F16): stereo-linked, look-ahead, true-peak aware, soft knee. Deterministic (no
// randomness, state advances per sample so the output never depends on the host block size) and allocation free after
// prepare(). Bypassed it is a bit-exact no-op.
//
// Signal path per frame: the input feeds a delay line of L = D + 16 frames (D = max(8, 1.5 ms) look-ahead, 16 frames of
// detector delay) and a 32-taps-per-phase Kaiser sinc polyphase interpolator that estimates the inter-sample (true) peak
// (8x below 100 kHz, 4x up to 200 kHz, sample peaks above). The peak sets a gain through a tanh soft knee that
// approaches the ceiling asymptotically; a sliding minimum over the look-ahead window, a 120 ms exponential release
// and a boxcar of the same length (the attack ramp) smooth it, so the smoothed gain is never above what the delayed
// peak needs. A sample-peak backstop clamps the result at the ceiling.
#include <atomic>
#include <cstdint>
#include <vector>

namespace chopfractal::render {

struct LimiterParams {
  bool enabled = false;
  float ceilingDb = -1.f;  // -6 .. 0 dBTP
};

struct LimiterTelemetry {
  std::atomic<float> grDb{0.f};        // most negative gain reduction since the last take (<= 0)
  std::atomic<float> peakDb{-120.f};   // true-peak estimate of the output since the last take
  std::atomic<std::uint32_t> backstops{0};  // how often the sample-peak clamp engaged (should stay 0)
  float takeGr() { return grDb.exchange(0.f, std::memory_order_relaxed); }
  float takePeak() { return peakDb.exchange(-120.f, std::memory_order_relaxed); }
};

class Limiter {
 public:
  Limiter();
  void prepare(double sampleRate, int maxBlock);  // non-RT: sizes every buffer
  int latencyFrames() const { return latency_; }  // lookahead + detector delay, constant for a sample rate
  static int latencyFor(double sampleRate);       // what latencyFrames() returns after prepare(sampleRate)
  void prime() noexcept;                           // reset, then behave as if silence had been limited for a long time (no warm-up, fully faded in)
  void reset() noexcept;                           // clears the delay line; the limiter restarts from its warm-up
  // In place, planar. Disabled (and fully faded out) it does nothing at all.
  void process(const LimiterParams& params, float* l, float* r, int frames) noexcept;
  LimiterTelemetry& telemetry() { return telemetry_; }
  bool active() const { return active_; }

 private:
  struct Chain;
  double sampleRate_ = 48000.0;
  int lookahead_ = 72;  // D
  int latency_ = 88;    // L = D + 16
  int oversample_ = 8;  // 8, 4 or 1
  float taps_[8][32] = {};
  double releaseCoef_ = 0.0;
  bool active_ = false;
  int warm_ = 0;
  float mix_ = 0.f;
  std::int64_t n_ = 0;
  // State, all sized in prepare().
  std::vector<float> hist_[2];     // last 32 inputs per channel (shifted each frame)
  std::vector<float> dly_[2];      // audio delay ring, L + 1
  std::vector<float> tpRing_;      // true-peak estimate aligned with the delayed audio, L + 1
  std::vector<double> dqVal_;      // monotonic deque for the sliding minimum
  std::vector<std::int64_t> dqIdx_;
  int dqHead_ = 0, dqCount_ = 0;
  std::vector<double> boxRing_;    // last D + 1 release-stage gains
  double boxSum_ = 0.0;
  int boxPos_ = 0;
  double gRel_ = 1.0;
  double prevGt_ = 1.0;
  int dlyPos_ = 0;
  LimiterTelemetry telemetry_;
};

}  // namespace chopfractal::render
