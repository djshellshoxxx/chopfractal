#include <chopfractal/input_capture/capture.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>

namespace chopfractal::capture {
namespace {
constexpr double kMinBpm = 20.0, kMaxBpm = 999.0;
constexpr double kTempoHeadroom = 1.25;  // synced buffers hold 25% more than the nominal length, for tempo drops

std::uint8_t u8(CaptureState s) { return static_cast<std::uint8_t>(s); }

std::string formatMessage(const char* fmt, double a, double b = 0.0) {
  char text[160];
  std::snprintf(text, sizeof text, fmt, a, b);
  return text;
}
}  // namespace

std::int64_t maxCaptureFrames(double sampleRate, int channels) {
  const std::int64_t bySeconds = static_cast<std::int64_t>(std::floor(kMaxCaptureSeconds * sampleRate));
  const std::int64_t byBytes = kMaxCaptureBytes / (static_cast<std::int64_t>(std::max(1, channels)) * static_cast<std::int64_t>(sizeof(float)));
  return std::min({bySeconds, kMaxCaptureFrames, byBytes});
}

CaptureEngine::CaptureEngine() = default;
CaptureEngine::~CaptureEngine() = default;

Status CaptureEngine::arm(const ArmSettings& s) {
  if (state_.load(std::memory_order_acquire) != u8(CaptureState::Idle))
    return makeError(ErrorCode::Conflict, "Discard or keep the current capture first.");
  if (s.channels < 1 || s.channels > 2) return makeError(ErrorCode::InvalidArgument, "Capture records one or two input channels.");
  if (!(std::isfinite(s.sampleRate) && s.sampleRate >= 8000.0 && s.sampleRate <= 384000.0)) return makeError(ErrorCode::OutOfRange, "The sample rate is outside 8 kHz to 384 kHz.");
  const std::int64_t maxFrames = maxCaptureFrames(s.sampleRate, s.channels);
  std::int64_t cap = 0;
  if (s.mode == CaptureMode::SyncedBars) {
    if (s.bars < 1 || s.bars > 8) return makeError(ErrorCode::OutOfRange, "Capture 1 to 8 bars.");
    if (!(std::isfinite(s.barQuarters) && s.barQuarters > 0.0 && s.barQuarters <= 64.0)) return makeError(ErrorCode::OutOfRange, "The bar length is not valid.");
    if (!(std::isfinite(s.bpmHint) && s.bpmHint >= kMinBpm && s.bpmHint <= kMaxBpm)) return makeError(ErrorCode::OutOfRange, "The tempo is outside 20 to 999 BPM.");
    const double seconds = static_cast<double>(s.bars) * s.barQuarters * 60.0 / s.bpmHint;
    if (seconds > kMaxCaptureSeconds)
      return makeError(ErrorCode::OutOfRange, formatMessage("That length is %.1f s; the maximum is %.0f s.", seconds, kMaxCaptureSeconds), static_cast<std::int64_t>(kMaxCaptureSeconds));
    const double need = std::ceil(seconds * s.sampleRate);
    if (need > static_cast<double>(maxFrames)) {
      const double mb = need * s.channels * sizeof(float) / (1024.0 * 1024.0);
      return makeError(ErrorCode::LimitExceeded, formatMessage("That capture would need %.0f MB; the limit is %.0f MB. Choose a shorter length.", mb, static_cast<double>(kMaxCaptureBytes / (1024 * 1024))), maxFrames);
    }
    cap = std::min<std::int64_t>(maxFrames, static_cast<std::int64_t>(std::ceil(seconds * s.sampleRate * kTempoHeadroom)) + 1);
  } else {
    if (!(std::isfinite(s.maxSeconds) && s.maxSeconds >= 1.0 && s.maxSeconds <= kMaxCaptureSeconds)) return makeError(ErrorCode::OutOfRange, "The maximum length is 1 to 60 seconds.");
    cap = std::min<std::int64_t>(maxFrames, static_cast<std::int64_t>(std::ceil(s.maxSeconds * s.sampleRate)));
  }
  cap = std::max<std::int64_t>(cap, 1);
  std::vector<float> fresh;
  try {
    fresh.assign(static_cast<std::size_t>(cap) * static_cast<std::size_t>(s.channels), 0.f);
  } catch (const std::bad_alloc&) {
    return makeError(ErrorCode::LimitExceeded, "Not enough memory for that capture length.", maxFrames);
  } catch (const std::length_error&) {
    return makeError(ErrorCode::LimitExceeded, "Not enough memory for that capture length.", maxFrames);
  }
  buf_.swap(fresh);
  settings_ = s;
  cap_ = cap;
  total_ = 0;
  writePos_ = 0;
  useFrames_ = false;
  endFrames_ = 0;
  endPpq_ = expectedPpq_ = 0.0;
  startReq_.store(false, std::memory_order_relaxed);
  stopReq_.store(false, std::memory_order_relaxed);
  ended_.store(static_cast<std::uint8_t>(EndReason::None), std::memory_order_relaxed);
  framesCaptured_.store(0, std::memory_order_relaxed);
  capacity_.store(cap, std::memory_order_relaxed);
  peak_.store(0.f, std::memory_order_relaxed);
  level_.store(0.f, std::memory_order_relaxed);
  clipped_.store(0, std::memory_order_relaxed);
  channelsPub_.store(s.channels, std::memory_order_relaxed);
  sampleRatePub_.store(static_cast<int>(std::lround(s.sampleRate)), std::memory_order_relaxed);
  state_.store(u8(CaptureState::Armed), std::memory_order_release);
  return {};
}

Status CaptureEngine::startNow() {
  if (state_.load(std::memory_order_acquire) != u8(CaptureState::Armed)) return makeError(ErrorCode::Conflict, "Arm the capture first.");
  startReq_.store(true, std::memory_order_release);
  return {};
}

void CaptureEngine::requestStop() {
  const std::uint8_t st = state_.load(std::memory_order_acquire);
  if (st == u8(CaptureState::Armed) || st == u8(CaptureState::Capturing)) stopReq_.store(true, std::memory_order_release);
}

void CaptureEngine::discard() {
  std::uint8_t expected = u8(CaptureState::Captured);
  state_.compare_exchange_strong(expected, u8(CaptureState::Idle), std::memory_order_acq_rel);
}

void CaptureEngine::reap() {
  if (state_.load(std::memory_order_acquire) == u8(CaptureState::Idle) && !buf_.empty()) {
    std::vector<float>().swap(buf_);
    capacity_.store(0, std::memory_order_relaxed);
  }
}

CaptureStatus CaptureEngine::status() const {
  CaptureStatus s;
  s.state = static_cast<CaptureState>(state_.load(std::memory_order_acquire));
  s.ended = static_cast<EndReason>(ended_.load(std::memory_order_relaxed));
  s.framesCaptured = framesCaptured_.load(std::memory_order_relaxed);
  s.capacityFrames = capacity_.load(std::memory_order_relaxed);
  s.peak = peak_.load(std::memory_order_relaxed);
  s.level = level_.load(std::memory_order_relaxed);
  s.clippedSamples = clipped_.load(std::memory_order_relaxed);
  s.channels = channelsPub_.load(std::memory_order_relaxed);
  s.sampleRate = sampleRatePub_.load(std::memory_order_relaxed);
  return s;
}

Result<CapturedAudio> CaptureEngine::take() {
  const std::uint8_t st = state_.load(std::memory_order_acquire);
  if (st == u8(CaptureState::Idle)) return makeError(ErrorCode::NotFound, "There is no capture to keep.");
  if (st != u8(CaptureState::Captured)) return makeError(ErrorCode::Conflict, "The capture is still running.");
  const std::int64_t frames = std::min(total_, cap_);
  CapturedAudio out;
  out.channels = settings_.channels;
  out.sampleRate = static_cast<int>(std::lround(settings_.sampleRate));
  out.frames = frames;
  out.ended = static_cast<EndReason>(ended_.load(std::memory_order_relaxed));
  out.wrapped = settings_.mode == CaptureMode::Free && total_ > cap_;
  const std::size_t cap = static_cast<std::size_t>(cap_), n = static_cast<std::size_t>(frames);
  if (frames <= 0) {
    state_.store(u8(CaptureState::Idle), std::memory_order_release);
    return makeError(ErrorCode::NotFound, "The capture is empty.");
  }
  if (out.wrapped) {  // oldest sample is at writePos_: rotate every channel into time order
    for (int c = 0; c < settings_.channels; ++c) {
      float* ch = buf_.data() + static_cast<std::size_t>(c) * cap;
      std::rotate(ch, ch + writePos_, ch + cap);
    }
  }
  for (int c = 1; c < settings_.channels; ++c)  // compact the stride from cap to frames (ascending move, destination is below the source)
    std::memmove(buf_.data() + static_cast<std::size_t>(c) * n, buf_.data() + static_cast<std::size_t>(c) * cap, n * sizeof(float));
  buf_.resize(n * static_cast<std::size_t>(settings_.channels));
  if (buf_.capacity() > 2 * buf_.size()) buf_.shrink_to_fit();
  out.samples = std::move(buf_);
  buf_ = std::vector<float>();
  capacity_.store(0, std::memory_order_relaxed);
  state_.store(u8(CaptureState::Idle), std::memory_order_release);
  return out;
}

bool CaptureEngine::onPrepare(double sampleRate, int channels) {
  const std::uint8_t st = state_.load(std::memory_order_acquire);
  if (st != u8(CaptureState::Armed) && st != u8(CaptureState::Capturing)) return false;
  if (sampleRate == settings_.sampleRate && std::min(2, channels) == settings_.channels) return false;
  startReq_.store(false, std::memory_order_relaxed);
  stopReq_.store(false, std::memory_order_relaxed);
  if (st == u8(CaptureState::Armed)) {
    ended_.store(static_cast<std::uint8_t>(EndReason::Reconfigured), std::memory_order_relaxed);
    state_.store(u8(CaptureState::Idle), std::memory_order_release);
  } else {
    finish(EndReason::Reconfigured);
  }
  return true;
}

void CaptureEngine::finish(EndReason why) {
  ended_.store(static_cast<std::uint8_t>(why), std::memory_order_relaxed);
  framesCaptured_.store(total_, std::memory_order_relaxed);
  state_.store(u8(CaptureState::Captured), std::memory_order_release);
}

// Copies frames [begin, end) of the input into the buffer (a ring in Free mode). Non-finite samples are stored as zero.
void CaptureEngine::write(const float* const* in, int numChannels, int begin, int end) {
  const std::int64_t n = end - begin;
  if (n <= 0) return;
  const bool ring = settings_.mode == CaptureMode::Free;
  for (int c = 0; c < settings_.channels; ++c) {
    const float* src = (in && numChannels > 0) ? in[std::min(c, numChannels - 1)] : nullptr;
    float* dst = buf_.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(cap_);
    std::int64_t pos = ring ? writePos_ : total_;
    std::int64_t done = 0;
    while (done < n) {
      const std::int64_t chunk = std::min(n - done, cap_ - pos);
      if (src) {
        for (std::int64_t i = 0; i < chunk; ++i) {
          const float x = src[begin + done + i];
          dst[pos + i] = std::isfinite(x) ? x : 0.f;
        }
      } else {
        std::fill(dst + pos, dst + pos + chunk, 0.f);
      }
      done += chunk;
      pos += chunk;
      if (pos >= cap_) pos = 0;  // only the ring reaches here with more to write; linear writes are clipped by the caller
    }
  }
  if (ring) writePos_ = (writePos_ + n) % cap_;
  total_ += n;
}

void CaptureEngine::process(const float* const* in, int numChannels, int frames, const CaptureTransport& t) {
  const std::uint8_t st = state_.load(std::memory_order_acquire);
  if (st != u8(CaptureState::Armed) && st != u8(CaptureState::Capturing)) return;
  if (frames <= 0) return;

  // Meters: peak and clipping over every frame of the block, on the channels being recorded.
  {
    float blockPeak = 0.f;
    std::uint32_t clips = 0;
    const int metered = std::max(1, std::min(numChannels, settings_.channels));
    for (int c = 0; c < metered; ++c) {
      const float* src = (in && c < numChannels) ? in[c] : nullptr;
      if (!src) continue;
      for (int i = 0; i < frames; ++i) {
        const float a = std::isfinite(src[i]) ? std::fabs(src[i]) : 0.f;
        if (a > blockPeak) blockPeak = a;
        if (a >= kClipThreshold) ++clips;
      }
    }
    level_.store(blockPeak, std::memory_order_relaxed);
    if (blockPeak > peak_.load(std::memory_order_relaxed)) peak_.store(blockPeak, std::memory_order_relaxed);
    if (clips) clipped_.store(clipped_.load(std::memory_order_relaxed) + clips, std::memory_order_relaxed);
  }

  const double sr = settings_.sampleRate;
  const double bpm = (std::isfinite(t.bpm) && t.bpm >= kMinBpm && t.bpm <= kMaxBpm) ? t.bpm : settings_.bpmHint;
  const double qpf = bpm / (60.0 * sr);  // quarter notes per frame
  const bool synced = settings_.mode == CaptureMode::SyncedBars;
  const double q = settings_.barQuarters;
  const double p0 = (std::isfinite(t.ppq) ? t.ppq : 0.0);
  const double p1 = p0 + static_cast<double>(frames) * qpf;
  int begin = 0;
  bool justStarted = false;

  if (st == u8(CaptureState::Armed)) {
    if (stopReq_.exchange(false, std::memory_order_acq_rel)) {
      ended_.store(static_cast<std::uint8_t>(EndReason::UserStop), std::memory_order_relaxed);
      state_.store(u8(CaptureState::Idle), std::memory_order_release);
      return;
    }
    bool start = false;
    if (startReq_.exchange(false, std::memory_order_acq_rel)) {
      start = true;
      if (synced) {
        if (t.playing && t.positionValid) {
          useFrames_ = false;
          endPpq_ = p0 + static_cast<double>(settings_.bars) * q;
        } else {
          useFrames_ = true;  // no running transport to follow: the length is a frame count at the current tempo
          endFrames_ = static_cast<std::int64_t>(std::ceil(static_cast<double>(settings_.bars) * q * 60.0 / bpm * sr));
        }
      }
    } else if (synced && t.playing && t.positionValid) {
      const double b = std::ceil(p0 / q - 1e-9) * q;  // next bar line at or after the block start
      // The start frame uses the same rounding as Renderer::schedule; deciding by it (not by b < p1) keeps the start
      // identical for every block size even when the bar line falls within float noise of a block edge.
      const double off = std::max(0.0, std::ceil((b - p0) * 60.0 / bpm * sr - 1e-6));
      if (off < static_cast<double>(frames)) {
        begin = static_cast<int>(off);
        useFrames_ = false;
        endPpq_ = b + static_cast<double>(settings_.bars) * q;
        start = true;
      }
    }
    if (!start) return;
    total_ = 0;
    writePos_ = 0;
    framesCaptured_.store(0, std::memory_order_relaxed);
    state_.store(u8(CaptureState::Capturing), std::memory_order_release);
    justStarted = true;
  } else if (stopReq_.exchange(false, std::memory_order_acq_rel)) {
    finish(EndReason::UserStop);
    return;
  }

  // Capturing.
  if (synced && !useFrames_ && !justStarted) {
    const double tolerance = 2.0 * static_cast<double>(frames) * qpf + 1e-9;
    if (!t.playing || !t.positionValid || std::fabs(p0 - expectedPpq_) > tolerance) {  // stopped, or a seek / loop wrap
      finish(EndReason::TransportStopped);
      return;
    }
  }
  int end = frames;
  bool completed = false;
  if (synced) {
    if (useFrames_) {
      const std::int64_t left = endFrames_ - total_;
      if (left <= static_cast<std::int64_t>(frames - begin)) {
        end = begin + static_cast<int>(std::max<std::int64_t>(left, 0));
        completed = true;
      }
    } else {
      const double off = std::ceil((endPpq_ - p0) * 60.0 / bpm * sr - 1e-6);
      if (off <= static_cast<double>(frames)) {
        end = static_cast<int>(std::max(off, static_cast<double>(begin)));
        completed = true;
      }
    }
    if (total_ + (end - begin) > cap_) {  // the buffer is full before the bars are: the tempo dropped past the headroom
      end = begin + static_cast<int>(cap_ - total_);
      write(in, numChannels, begin, end);
      framesCaptured_.store(total_, std::memory_order_relaxed);
      finish(EndReason::Overflow);
      return;
    }
  }
  write(in, numChannels, begin, end);
  framesCaptured_.store(total_, std::memory_order_relaxed);
  expectedPpq_ = p1;
  if (completed) finish(EndReason::Completed);
}

}  // namespace chopfractal::capture
