#include <chopfractal/input_capture/capture.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <new>

namespace chopfractal::capture {

namespace {
constexpr double kHeadroom = 1.25;  // tempo drops during a synced capture
constexpr double kEps = 1e-6;

Error err(ErrorCode c, std::string m, std::int64_t hint = 0) { return makeError(c, std::move(m), hint); }

std::int64_t frameOffset(double ppqDelta, double bpm, double sr) {
  return static_cast<std::int64_t>(std::ceil(ppqDelta * 60.0 / bpm * sr - kEps));
}
}  // namespace

std::int64_t maxCaptureFrames(int channels, double sampleRate) {
  const std::int64_t byTime = static_cast<std::int64_t>(std::floor(kMaxCaptureSeconds * sampleRate));
  const std::int64_t byBytes = kMaxCaptureBytes / (std::max(channels, 1) * 4);
  return std::min({byTime, kMaxCaptureFrames, byBytes});
}

struct CaptureEngine::Impl {
  // Published to the audio thread by state.store(Armed, release).
  ArmSettings s;
  std::unique_ptr<float[]> buf;
  std::int64_t capacity = 0;
  // Audio-thread state (read by the message thread only while Captured, after an acquire of `state`).
  std::int64_t writePos = 0, total = 0;
  double endPpq = 0.0, expectedPpq = 0.0;
  bool wrapped = false;
  // Cross-thread.
  std::atomic<CaptureState> state{CaptureState::Idle};
  std::atomic<bool> startReq{false}, stopReq{false};
  std::atomic<EndReason> ended{EndReason::None};
  std::atomic<std::int64_t> captured{0};
  std::atomic<std::int64_t> cap{0};
  std::atomic<float> peak{0.f};
  std::atomic<std::uint32_t> clipped{0};

  void finish(CaptureState to, EndReason why) {
    ended.store(why, std::memory_order_relaxed);
    state.store(to, std::memory_order_release);
  }

  void scan(const float* const* in, int n, int frames) {
    float p = 0.f;
    const int chans = std::min(n, s.channels);
    for (int c = 0; c < chans; ++c)
      for (int i = 0; i < frames; ++i) {
        const float a = std::fabs(in[c][i]);
        if (std::isfinite(a) && a > p) p = a;
      }
    float cur = peak.load(std::memory_order_relaxed);
    while (p > cur && !peak.compare_exchange_weak(cur, p, std::memory_order_relaxed)) {
    }
  }

  // Copies in[from, from+count) into the buffer. Returns the number of frames actually stored.
  std::int64_t write(const float* const* in, int n, int from, std::int64_t count) {
    const bool ring = s.mode == CaptureMode::Free;
    if (!ring) count = std::min(count, capacity - writePos);
    if (count <= 0) return 0;
    std::uint32_t clips = 0;
    for (int c = 0; c < s.channels; ++c) {
      const float* src = in[c < n ? c : 0] + from;
      float* base = buf.get() + static_cast<std::int64_t>(c) * capacity;
      std::int64_t pos = writePos;
      for (std::int64_t i = 0; i < count; ++i) {
        float v = src[i];
        if (!std::isfinite(v)) v = 0.f;
        else if (std::fabs(v) >= kClipThreshold) ++clips;
        base[pos] = v;
        if (++pos == capacity) pos = 0;
      }
    }
    writePos = (writePos + count) % capacity;
    total += count;
    if (ring && total > capacity) wrapped = true;
    if (clips) clipped.fetch_add(clips, std::memory_order_relaxed);
    captured.store(ring ? std::min(total, capacity) : total, std::memory_order_relaxed);
    return count;
  }

  void freeBuffer() {
    buf.reset();
    capacity = 0;
    cap.store(0, std::memory_order_relaxed);
  }
};

CaptureEngine::CaptureEngine() : d_(new Impl) {}
CaptureEngine::~CaptureEngine() = default;

Status CaptureEngine::arm(const ArmSettings& s) {
  Impl& d = *d_;
  reap();
  if (d.state.load(std::memory_order_acquire) != CaptureState::Idle)
    return err(ErrorCode::Conflict, "A capture is already in progress.");
  if (s.channels < 1 || s.channels > 2) return err(ErrorCode::InvalidArgument, "Capture supports mono or stereo input.");
  if (!(s.sampleRate >= 8000.0 && s.sampleRate <= 384000.0))
    return err(ErrorCode::OutOfRange, "The sample rate is outside 8 kHz to 384 kHz.");
  const std::int64_t maxFrames = maxCaptureFrames(s.channels, s.sampleRate);
  std::int64_t capacity = 0;
  char msg[160];
  if (s.mode == CaptureMode::SyncedBars) {
    if (s.bars < 1 || s.bars > 8) return err(ErrorCode::OutOfRange, "Capture bars must be between 1 and 8.");
    if (!(s.barQuarters > 0.0 && s.barQuarters <= 64.0)) return err(ErrorCode::OutOfRange, "The bar length is invalid.");
    if (!(s.bpmHint >= 1.0 && s.bpmHint <= 999.0)) return err(ErrorCode::OutOfRange, "The tempo is outside 1 to 999 BPM.");
    const double seconds = s.bars * s.barQuarters * 60.0 / s.bpmHint;
    if (seconds > kMaxCaptureSeconds) {
      std::snprintf(msg, sizeof msg, "That length is %.1f s; the maximum is %d s.", seconds, static_cast<int>(kMaxCaptureSeconds));
      return err(ErrorCode::OutOfRange, msg, static_cast<std::int64_t>(kMaxCaptureSeconds));
    }
    const std::int64_t exact = static_cast<std::int64_t>(std::ceil(seconds * s.sampleRate));
    if (exact > maxFrames) {
      std::snprintf(msg, sizeof msg, "That capture would need %lld MB; the limit is %lld MB. Choose a shorter length.",
                    static_cast<long long>(exact * s.channels * 4 / (1024 * 1024)), static_cast<long long>(kMaxCaptureBytes / (1024 * 1024)));
      return err(ErrorCode::LimitExceeded, msg, maxFrames);
    }
    capacity = std::min(maxFrames, static_cast<std::int64_t>(std::ceil(seconds * s.sampleRate * kHeadroom)) + 1);
  } else {
    if (!(s.maxSeconds >= 1.0 && s.maxSeconds <= kMaxCaptureSeconds))
      return err(ErrorCode::OutOfRange, "The maximum length must be between 1 and 60 seconds.");
    capacity = std::min(maxFrames, static_cast<std::int64_t>(std::ceil(s.maxSeconds * s.sampleRate)));
  }
  try {
    d.buf.reset(new float[static_cast<std::size_t>(capacity) * static_cast<std::size_t>(s.channels)]());
  } catch (const std::bad_alloc&) {
    d.freeBuffer();
    return err(ErrorCode::LimitExceeded, "Not enough memory for that capture length.", maxFrames);
  }
  d.s = s;
  d.capacity = capacity;
  d.cap.store(capacity, std::memory_order_relaxed);
  d.writePos = d.total = 0;
  d.wrapped = false;
  d.endPpq = d.expectedPpq = 0.0;
  d.captured.store(0, std::memory_order_relaxed);
  d.peak.store(0.f, std::memory_order_relaxed);
  d.clipped.store(0, std::memory_order_relaxed);
  d.ended.store(EndReason::None, std::memory_order_relaxed);
  d.startReq.store(false, std::memory_order_relaxed);
  d.stopReq.store(false, std::memory_order_relaxed);
  d.state.store(CaptureState::Armed, std::memory_order_release);
  return Status{};
}

Status CaptureEngine::startNow() {
  if (d_->state.load(std::memory_order_acquire) != CaptureState::Armed) return err(ErrorCode::Conflict, "Capture is not armed.");
  d_->startReq.store(true, std::memory_order_release);
  return Status{};
}

void CaptureEngine::requestStop() {
  const CaptureState st = d_->state.load(std::memory_order_acquire);
  if (st == CaptureState::Armed || st == CaptureState::Capturing) d_->stopReq.store(true, std::memory_order_release);
}

void CaptureEngine::discard() {
  if (d_->state.load(std::memory_order_acquire) != CaptureState::Captured) return;
  d_->freeBuffer();
  d_->state.store(CaptureState::Idle, std::memory_order_release);
}

void CaptureEngine::reap() {
  if (d_->state.load(std::memory_order_acquire) == CaptureState::Idle && d_->buf) d_->freeBuffer();
}

CaptureStatus CaptureEngine::status() const {
  const Impl& d = *d_;
  CaptureStatus st;
  st.state = d.state.load(std::memory_order_acquire);
  st.ended = d.ended.load(std::memory_order_relaxed);
  st.framesCaptured = d.captured.load(std::memory_order_relaxed);
  st.capacityFrames = d.cap.load(std::memory_order_relaxed);
  st.peak = d.peak.load(std::memory_order_relaxed);
  st.clippedSamples = d.clipped.load(std::memory_order_relaxed);
  return st;
}

Result<CapturedAudio> CaptureEngine::take() {
  Impl& d = *d_;
  const CaptureState st = d.state.load(std::memory_order_acquire);
  if (st == CaptureState::Idle) return err(ErrorCode::NotFound, "There is no capture to keep.");
  if (st != CaptureState::Captured) return err(ErrorCode::Conflict, "The capture has not finished.");
  const std::int64_t frames = std::min(d.total, d.capacity);
  if (frames <= 0) {
    d.freeBuffer();
    d.state.store(CaptureState::Idle, std::memory_order_release);
    return err(ErrorCode::OutOfRange, "The capture is empty.");
  }
  CapturedAudio out;
  out.channels = d.s.channels;
  out.sampleRate = static_cast<int>(std::lround(d.s.sampleRate));
  out.frames = frames;
  out.ended = d.ended.load(std::memory_order_relaxed);
  out.wrapped = d.wrapped;
  try {
    out.samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(d.s.channels));
  } catch (const std::bad_alloc&) {
    return err(ErrorCode::LimitExceeded, "Not enough memory to keep the capture.");
  }
  for (int c = 0; c < d.s.channels; ++c) {
    const float* base = d.buf.get() + static_cast<std::int64_t>(c) * d.capacity;
    float* dst = out.samples.data() + static_cast<std::int64_t>(c) * frames;
    if (d.wrapped) {  // oldest frame sits at writePos
      const std::int64_t tail = d.capacity - d.writePos;
      std::copy(base + d.writePos, base + d.capacity, dst);
      std::copy(base, base + d.writePos, dst + tail);
    } else {
      std::copy(base, base + frames, dst);
    }
  }
  d.freeBuffer();
  d.state.store(CaptureState::Idle, std::memory_order_release);
  return out;
}

void CaptureEngine::onPrepare(double sampleRate, int channels) {
  Impl& d = *d_;
  // Hosts call prepareToPlay for ordinary transport restarts too: only a real format change aborts.
  if (std::fabs(sampleRate - d.s.sampleRate) <= 0.5 && channels == d.s.channels) return;
  switch (d.state.load(std::memory_order_acquire)) {
    case CaptureState::Armed:
      d.ended.store(EndReason::Reconfigured, std::memory_order_relaxed);
      d.freeBuffer();
      d.state.store(CaptureState::Idle, std::memory_order_release);
      break;
    case CaptureState::Capturing:
      d.finish(CaptureState::Captured, EndReason::Reconfigured);
      break;
    default:
      break;
  }
}

void CaptureEngine::process(const float* const* in, int numChannels, int frames, const CaptureTransport& t) {
  Impl& d = *d_;
  CaptureState st = d.state.load(std::memory_order_acquire);
  if (st == CaptureState::Idle || st == CaptureState::Captured) return;
  if (d.stopReq.exchange(false, std::memory_order_acq_rel)) {
    if (st == CaptureState::Armed) d.state.store(CaptureState::Idle, std::memory_order_release);
    else d.finish(CaptureState::Captured, EndReason::UserStop);
    return;
  }
  if (!in || numChannels < 1 || frames <= 0) return;
  d.scan(in, numChannels, frames);

  const double sr = d.s.sampleRate;
  const bool synced = d.s.mode == CaptureMode::SyncedBars;
  const double Q = d.s.barQuarters;
  const bool posOk = t.playing && t.positionValid && t.bpm > 0.0;
  int from = 0;

  if (st == CaptureState::Armed) {
    const bool manual = d.startReq.exchange(false, std::memory_order_acq_rel);
    if (!synced) {
      if (!manual) return;
    } else if (manual) {
      if (!posOk) return;
      d.endPpq = t.ppq + d.s.bars * Q;
    } else {
      if (!posOk) return;
      const double p1 = t.ppq + frames * t.bpm / (60.0 * sr);
      const double b = std::ceil(t.ppq / Q - 1e-9) * Q;
      if (!(b < p1)) return;
      from = static_cast<int>(std::clamp<std::int64_t>(frameOffset(b - t.ppq, t.bpm, sr), 0, frames - 1));
      d.endPpq = b + d.s.bars * Q;
    }
    d.state.store(CaptureState::Capturing, std::memory_order_release);
  } else if (synced) {
    const double tol = 2.0 * frames * t.bpm / (60.0 * sr) + 1e-6;
    if (!posOk || std::fabs(t.ppq - d.expectedPpq) > tol) {
      d.finish(CaptureState::Captured, EndReason::TransportStopped);
      return;
    }
  }

  std::int64_t count = frames - from;
  bool complete = false;
  if (synced) {
    const std::int64_t endOff = frameOffset(d.endPpq - t.ppq, t.bpm, sr);
    if (endOff <= frames) {
      count = std::max<std::int64_t>(0, endOff - from);
      complete = true;
    }
    d.expectedPpq = t.ppq + frames * t.bpm / (60.0 * sr);
  }
  const std::int64_t stored = d.write(in, numChannels, from, count);
  if (complete) {
    d.finish(CaptureState::Captured, stored == count ? EndReason::Completed : EndReason::Overflow);
  } else if (synced && stored < count) {
    d.finish(CaptureState::Captured, EndReason::Overflow);
  }
}

}  // namespace chopfractal::capture
