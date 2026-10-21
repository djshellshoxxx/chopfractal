#pragma once
// Output level meter: the audio thread pushes the peak, the clipped-sample count and the voice count of every block it
// produces, the UI thread takes the maximum since its last look. Lock-free and allocation-free on both sides.
#include <atomic>
#include <cmath>
#include <cstdint>

namespace chopfractal::render {

struct MeterReading {
  float peak[2] = {0.f, 0.f};      // linear, max |x| since the last take (mono: both equal)
  std::uint32_t clipSamples = 0;   // samples with |x| > 1 since the last take
  int activeVoices = 0;            // sounding or fading voices at the end of the last block
};

class OutputMeter {
 public:
  // Audio thread only. O(frames), reads the finished output buffer. NaN samples never count as a peak or a clip.
  void push(const float* const* out, int channels, int frames, int activeVoices) noexcept {
    float blockPeak[2] = {0.f, 0.f};
    std::uint32_t clipped = 0;
    const int nch = channels > 2 ? 2 : channels;
    for (int c = 0; c < nch; ++c) {
      const float* x = out[c];
      float peak = 0.f;
      for (int i = 0; i < frames; ++i) {
        const float a = std::fabs(x[i]);
        if (a > peak) peak = a;  // false for NaN
        if (a > 1.0f) ++clipped;
      }
      blockPeak[c] = peak;
    }
    if (nch == 1) blockPeak[1] = blockPeak[0];
    for (int c = 0; c < 2; ++c)
      if (blockPeak[c] > peak_[c].load(std::memory_order_relaxed)) peak_[c].store(blockPeak[c], std::memory_order_relaxed);
    if (clipped) clip_.fetch_add(clipped, std::memory_order_relaxed);
    voices_.store(activeVoices, std::memory_order_relaxed);
  }
  // UI thread: the maximum since the previous take; peak and clip count reset, the voice count is the last pushed.
  MeterReading take() noexcept {
    MeterReading r;
    r.peak[0] = peak_[0].exchange(0.f, std::memory_order_relaxed);
    r.peak[1] = peak_[1].exchange(0.f, std::memory_order_relaxed);
    r.clipSamples = clip_.exchange(0, std::memory_order_relaxed);
    r.activeVoices = voices_.load(std::memory_order_relaxed);
    return r;
  }

 private:
  std::atomic<float> peak_[2]{{0.f}, {0.f}};
  std::atomic<std::uint32_t> clip_{0};
  std::atomic<int> voices_{0};
};
static_assert(std::atomic<float>::is_always_lock_free, "the meter needs lock-free float atomics");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the meter needs lock-free integer atomics");

}  // namespace chopfractal::render
