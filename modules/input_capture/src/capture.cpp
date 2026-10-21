#include <chopfractal/input_capture/capture.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace chopfractal::capture {

namespace {
std::int64_t maxFrames(double sampleRate) { return static_cast<std::int64_t>(std::floor(kMaxSeconds * sampleRate)); }
}  // namespace

std::int64_t framesForBars(int bars, double quartersPerBar, double bpm, double sampleRate) {
  if (bars < 1 || !(quartersPerBar > 0.0) || !(bpm >= 1.0 && bpm <= 1000.0) || !(sampleRate >= 8000.0 && sampleRate <= 384000.0)) return 0;
  const double frames = static_cast<double>(bars) * quartersPerBar * 60.0 / bpm * sampleRate;
  if (!std::isfinite(frames) || frames < 1.0) return 0;
  return static_cast<std::int64_t>(std::llround(frames));
}

Status Capture::arm(const Options& o) {
  const State now = state();
  if (now == State::Armed || now == State::Recording) return makeError(ErrorCode::Conflict, "a capture is already armed or recording; stop it first");
  if (o.channels < 1 || o.channels > 2) return makeError(ErrorCode::InvalidArgument, "capture supports mono or stereo input");
  if (!(o.sampleRate >= 8000.0 && o.sampleRate <= 384000.0)) return makeError(ErrorCode::InvalidArgument, "unsupported sample rate");
  const std::int64_t limit = maxFrames(o.sampleRate);
  if (o.targetFrames < 0 || o.targetFrames > limit) return makeError(ErrorCode::LimitExceeded, "a capture can be at most 60 seconds", limit);
  const std::int64_t capacity = o.targetFrames > 0 ? o.targetFrames : limit;
  const std::size_t bytes = static_cast<std::size_t>(capacity) * static_cast<std::size_t>(o.channels) * sizeof(float);
  if (bytes > kMaxBytes) return makeError(ErrorCode::LimitExceeded, "the capture would exceed the 100 MB limit");

  // The audio thread ignores the buffer unless it sees Armed or Recording, so it is safe to replace it here.
  buffer_.assign(static_cast<std::size_t>(capacity) * static_cast<std::size_t>(o.channels), 0.f);
  capacity_ = capacity;
  channels_ = o.channels;
  waitForBar_ = o.waitForBar;
  sampleRate_.store(o.sampleRate, std::memory_order_relaxed);
  target_.store(o.targetFrames, std::memory_order_relaxed);
  written_.store(0, std::memory_order_relaxed);
  stopRequested_.store(false, std::memory_order_relaxed);
  cancelRequested_.store(false, std::memory_order_relaxed);
  reason_.store(static_cast<int>(StopReason::None), std::memory_order_relaxed);
  state_.store(static_cast<int>(State::Armed), std::memory_order_release);
  return {};
}

void Capture::stop() {
  int expected = static_cast<int>(State::Armed);
  // Not yet recording: nothing to keep, and a compare-exchange cannot lose a race with the audio thread starting.
  if (state_.compare_exchange_strong(expected, static_cast<int>(State::Idle), std::memory_order_acq_rel)) return;
  if (state() == State::Recording) stopRequested_.store(true, std::memory_order_release);
}

void Capture::cancel() {
  int expected = static_cast<int>(State::Armed);
  if (state_.compare_exchange_strong(expected, static_cast<int>(State::Idle), std::memory_order_acq_rel)) return;
  switch (state()) {
    case State::Recording:
      cancelRequested_.store(true, std::memory_order_release);
      break;
    case State::Done:
      buffer_.clear();
      buffer_.shrink_to_fit();
      written_.store(0, std::memory_order_relaxed);
      state_.store(static_cast<int>(State::Idle), std::memory_order_release);
      break;
    default:
      break;
  }
}

Result<Captured> Capture::take() {
  if (state() != State::Done) return makeError(ErrorCode::InvalidArgument, "there is no finished capture to keep");
  const std::int64_t frames = written_.load(std::memory_order_relaxed);
  if (frames <= 0) {
    cancel();
    return makeError(ErrorCode::InvalidArgument, "no audio was captured");
  }
  Captured out;
  out.channels = channels_;
  out.sampleRate = static_cast<int>(std::lround(sampleRate_.load(std::memory_order_relaxed)));
  out.frames = frames;
  out.reason = reason();
  out.samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels_));
  for (int c = 0; c < channels_; ++c) {
    const float* src = buffer_.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(capacity_);
    std::copy(src, src + frames, out.samples.begin() + static_cast<std::ptrdiff_t>(c) * frames);
  }
  buffer_.clear();
  buffer_.shrink_to_fit();
  written_.store(0, std::memory_order_relaxed);
  state_.store(static_cast<int>(State::Idle), std::memory_order_release);
  return out;
}

void Capture::finish(StopReason why) {
  reason_.store(static_cast<int>(why), std::memory_order_relaxed);
  state_.store(static_cast<int>(State::Done), std::memory_order_release);
}

void Capture::write(const float* const* in, int numChannels, int from, int count) {
  const std::int64_t w = written_.load(std::memory_order_relaxed);
  const std::int64_t target = target_.load(std::memory_order_relaxed);
  const std::int64_t limit = target > 0 ? target : capacity_;
  const std::int64_t n = std::max<std::int64_t>(0, std::min<std::int64_t>(count, limit - w));
  for (int c = 0; c < channels_; ++c) {
    float* dst = buffer_.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(capacity_) + static_cast<std::size_t>(w);
    const float* src = nullptr;
    if (in && numChannels > 0) src = in[std::min(c, numChannels - 1)];  // a mono input feeds both channels
    if (!src) {
      std::fill(dst, dst + n, 0.f);
      continue;
    }
    src += from;
    for (std::int64_t i = 0; i < n; ++i) dst[i] = std::isfinite(src[i]) ? src[i] : 0.f;
  }
  written_.store(w + n, std::memory_order_relaxed);
  if (target > 0 && w + n >= target) finish(StopReason::LengthReached);
  else if (w + n >= capacity_) finish(StopReason::BufferFull);
}

void Capture::process(const float* const* in, int numChannels, int frames, const Transport& tp) {
  if (frames <= 0) return;
  const State st = state();
  if (st != State::Armed && st != State::Recording) return;
  if (cancelRequested_.exchange(false, std::memory_order_acq_rel)) {
    written_.store(0, std::memory_order_relaxed);
    state_.store(static_cast<int>(State::Idle), std::memory_order_release);
    return;
  }
  if (stopRequested_.exchange(false, std::memory_order_acq_rel)) {
    if (st == State::Recording) finish(StopReason::Manual);
    return;  // an Armed capture was already returned to Idle by stop()
  }
  int start = 0;
  if (st == State::Armed) {
    if (waitForBar_ && tp.playing && tp.positionValid && std::isfinite(tp.ppq) && tp.bpm >= 1.0 && tp.bpm <= 1000.0) {
      const double bar = tp.quartersPerBar > 0.0 ? tp.quartersPerBar : 4.0;
      const double framesPerQuarter = sampleRate_.load(std::memory_order_relaxed) * 60.0 / tp.bpm;
      double phase = std::fmod(tp.ppq, bar);
      if (phase < 0.0) phase += bar;
      const double toNext = (phase < 1e-9 || bar - phase < 1e-9) ? 0.0 : bar - phase;  // already on a bar line: start now
      const double offset = std::ceil(toNext * framesPerQuarter - 1e-6);
      if (offset >= static_cast<double>(frames)) return;  // the bar line is in a later block
      start = static_cast<int>(offset);
    }
    int expected = static_cast<int>(State::Armed);
    if (!state_.compare_exchange_strong(expected, static_cast<int>(State::Recording), std::memory_order_acq_rel)) return;  // stopped meanwhile
  }
  write(in, numChannels, start, frames - start);
}

}  // namespace chopfractal::capture
