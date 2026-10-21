#include <chopfractal/input_capture/capture.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <vector>

#include "chop_test.hpp"

// ---- allocation counter: process() must never allocate ----
#if defined(_MSC_VER)
#define CF_NOINLINE __declspec(noinline)
#else
#define CF_NOINLINE __attribute__((noinline))
#endif
namespace {
std::atomic<bool> gCountAllocs{false};
std::atomic<long> gAllocs{0};
}  // namespace
CF_NOINLINE void* operator new(std::size_t n) {
  if (gCountAllocs.load(std::memory_order_relaxed)) gAllocs.fetch_add(1, std::memory_order_relaxed);
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
CF_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
CF_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
CF_NOINLINE void* operator new[](std::size_t n) { return operator new(n); }
CF_NOINLINE void operator delete[](void* p) noexcept { std::free(p); }
CF_NOINLINE void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
CF_NOINLINE void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (gCountAllocs.load(std::memory_order_relaxed)) gAllocs.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(n ? n : 1);
}
CF_NOINLINE void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return operator new(n, std::nothrow); }
CF_NOINLINE void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
CF_NOINLINE void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

using namespace chopfractal;
using namespace chopfractal::capture;

namespace {

// Feeds the engine like a host: channel 0 carries the absolute frame index, channel 1 twice that (both exact in float
// below 2^23), the transport starts at ppq0 and advances with the tempo.
struct Host {
  double sr = 48000.0;
  double bpm = 120.0;
  double ppq0 = 0.0;
  bool playing = true;
  bool positionValid = true;
  std::int64_t frame = 0;  // absolute frame of the next block
  std::vector<float> a = std::vector<float>(2048), b = std::vector<float>(2048);  // allocated here so run() itself never allocates

  void run(CaptureEngine& e, std::int64_t frames, int block, int inChannels = 2) {
    std::int64_t done = 0;
    while (done < frames) {
      const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
      for (int i = 0; i < n; ++i) {
        a[static_cast<std::size_t>(i)] = static_cast<float>(frame + i);
        b[static_cast<std::size_t>(i)] = static_cast<float>(2 * (frame + i));
      }
      const float* in[2] = {a.data(), b.data()};
      CaptureTransport t;
      t.playing = playing;
      t.positionValid = positionValid;
      t.bpm = bpm;
      t.ppq = ppq0 + static_cast<double>(frame) * bpm / (60.0 * sr);
      e.process(in, inChannels, n, t);
      frame += n;
      done += n;
    }
  }
};

CapturedAudio syncedTake(int block, double ppq0 = 0.3, int bars = 4) {
  CaptureEngine e;
  ArmSettings s;
  s.bars = bars;
  CHECK(e.arm(s).ok());
  Host h;
  h.ppq0 = ppq0;
  const std::int64_t limit = static_cast<std::int64_t>(48000.0 * 2.0 * (bars * 4.0 + 8.0));
  while (e.status().state != CaptureState::Captured && h.frame < limit) h.run(e, block, block);
  CHECK(e.status().state == CaptureState::Captured);
  auto r = e.take();
  CHECK(r.ok());
  return r.ok() ? std::move(r.value()) : CapturedAudio{};
}

}  // namespace

CHOP_TEST(synced_capture_starts_on_bar_line_with_sample_accuracy) {
  const CapturedAudio c = syncedTake(512);
  CHECK(c.ended == EndReason::Completed);
  CHECK(c.channels == 2 && c.sampleRate == 48000);
  CHECK(c.frames >= 383999 && c.frames <= 384001);
  CHECK(c.samples.size() == static_cast<std::size_t>(c.frames) * 2);
  // First captured frame: ceil((4.0 - 0.3) * 0.5 * 48000 - 1e-6) = 88800.
  CHECK_EQ(static_cast<long>(c.samples[0]), 88800L);
  CHECK_EQ(static_cast<long>(c.samples[static_cast<std::size_t>(c.frames)]), 2 * 88800L);  // channel 1 follows channel 0
  CHECK_EQ(static_cast<long>(c.samples[static_cast<std::size_t>(c.frames) - 1]), 88800L + c.frames - 1);  // contiguous
}

CHOP_TEST(capture_is_block_size_independent) {
  const CapturedAudio ref = syncedTake(2048);
  for (int block : {1, 7, 64, 333, 512}) {
    const CapturedAudio c = syncedTake(block);
    CHECK(c.frames == ref.frames);
    CHECK(c.samples == ref.samples);
    CHECK(c.ended == ref.ended);
  }
}

CHOP_TEST(a_capture_that_starts_exactly_on_a_bar_line_starts_at_frame_zero_of_that_block) {
  const CapturedAudio c = syncedTake(480, 0.0);  // ppq 0 is a bar line: the first block begins the capture
  CHECK_EQ(static_cast<long>(c.samples[0]), 0L);
  CHECK(c.frames >= 383999 && c.frames <= 384001);
}

CHOP_TEST(free_ring_keeps_the_last_capacity_frames_in_order) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  s.sampleRate = 8000.0;
  s.maxSeconds = 1.0;  // capacity 8000 frames
  s.channels = 1;
  CHECK(e.arm(s).ok());
  CHECK(e.status().capacityFrames == 8000);
  CHECK(e.startNow().ok());
  Host h;
  h.sr = 8000.0;
  h.run(e, 20000, 333, 1);
  e.requestStop();
  h.run(e, 1, 1, 1);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(e.status().ended == EndReason::UserStop);
  CHECK(e.status().framesCaptured == 20000);
  auto r = e.take();
  CHECK(r.ok());
  CHECK(r.value().frames == 8000 && r.value().wrapped);
  bool inOrder = r.value().samples.size() == 8000;
  for (std::size_t i = 0; inOrder && i < 8000; ++i) inOrder = r.value().samples[i] == static_cast<float>(12000 + i);
  CHECK(inOrder);
  CHECK(e.status().state == CaptureState::Idle);
}

CHOP_TEST(free_capture_shorter_than_the_ring_is_not_wrapped_and_stereo_is_compacted) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  s.maxSeconds = 10.0;
  CHECK(e.arm(s).ok());
  CHECK(e.status().state == CaptureState::Armed);
  Host h;
  h.run(e, 1000, 100);  // armed, not capturing: nothing recorded
  CHECK(e.status().framesCaptured == 0);
  CHECK(e.startNow().ok());
  h.run(e, 5000, 512);
  e.requestStop();
  h.run(e, 1, 1);
  auto r = e.take();
  CHECK(r.ok());
  CHECK(r.value().frames == 5000 && !r.value().wrapped);
  CHECK(r.value().samples.size() == 10000);
  CHECK_EQ(static_cast<long>(r.value().samples[0]), 1000L);
  CHECK_EQ(static_cast<long>(r.value().samples[4999]), 5999L);
  CHECK_EQ(static_cast<long>(r.value().samples[5000]), 2000L);
  CHECK_EQ(static_cast<long>(r.value().samples[9999]), 2 * 5999L);
}

CHOP_TEST(mono_input_duplicated_to_stereo_and_stereo_to_mono_uses_channel_0) {
  {
    CaptureEngine e;
    ArmSettings s;
    s.mode = CaptureMode::Free;
    s.channels = 2;
    CHECK(e.arm(s).ok());
    CHECK(e.startNow().ok());
    Host h;
    h.run(e, 600, 200, 1);  // the host gives one channel
    e.requestStop();
    h.run(e, 1, 1, 1);
    auto r = e.take();
    CHECK(r.ok() && r.value().channels == 2 && r.value().frames == 600);
    bool same = true;
    for (std::int64_t i = 0; i < 600; ++i) same = same && r.value().samples[static_cast<std::size_t>(i)] == r.value().samples[static_cast<std::size_t>(600 + i)];
    CHECK(same);
  }
  {
    CaptureEngine e;
    ArmSettings s;
    s.mode = CaptureMode::Free;
    s.channels = 1;
    CHECK(e.arm(s).ok());
    CHECK(e.startNow().ok());
    Host h;
    h.run(e, 600, 200, 2);  // the host gives two channels; only channel 0 is recorded, never a mix
    e.requestStop();
    h.run(e, 1, 1, 2);
    auto r = e.take();
    CHECK(r.ok() && r.value().channels == 1 && r.value().frames == 600);
    CHECK_EQ(static_cast<long>(r.value().samples[599]), 599L);
  }
}

CHOP_TEST(transport_stop_or_seek_ends_capture_with_partial_audio) {
  for (int mode = 0; mode < 2; ++mode) {
    CaptureEngine e;
    ArmSettings s;
    CHECK(e.arm(s).ok());
    Host h;
    h.ppq0 = 0.0;
    h.run(e, 48000, 512);  // armed through bar 1 (ppq 0 started at frame 0, so this run is already capturing)
    CHECK(e.status().state == CaptureState::Capturing);
    const std::int64_t before = e.status().framesCaptured;
    if (mode == 0) {
      h.playing = false;
    } else {
      h.ppq0 += 40.0;  // a seek
    }
    h.run(e, 512, 512);
    CHECK(e.status().state == CaptureState::Captured);
    CHECK(e.status().ended == EndReason::TransportStopped);
    CHECK(e.status().framesCaptured == before);
    auto r = e.take();
    CHECK(r.ok() && r.value().frames == before);
  }
}

CHOP_TEST(a_synced_capture_waits_for_a_running_transport_with_a_position) {
  CaptureEngine e;
  CHECK(e.arm(ArmSettings{}).ok());
  Host h;
  h.playing = false;
  h.run(e, 20000, 512);
  CHECK(e.status().state == CaptureState::Armed);
  h.playing = true;
  h.positionValid = false;
  h.run(e, 20000, 512);
  CHECK(e.status().state == CaptureState::Armed);  // no position: it stays armed (the UI points to Free mode)
}

CHOP_TEST(manual_start_of_a_synced_capture_without_a_transport_counts_frames) {
  CaptureEngine e;
  ArmSettings s;
  s.bars = 1;
  CHECK(e.arm(s).ok());
  CHECK(e.startNow().ok());
  Host h;
  h.playing = false;
  h.run(e, 200000, 512);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(e.status().ended == EndReason::Completed);
  CHECK(e.status().framesCaptured == 96000);  // one 4/4 bar at 120 BPM and 48 kHz
}

CHOP_TEST(tempo_drop_beyond_headroom_ends_with_overflow) {
  CaptureEngine e;
  ArmSettings s;  // sized for 120 BPM: 384000 frames + 25%
  CHECK(e.arm(s).ok());
  const std::int64_t cap = e.status().capacityFrames;
  CHECK(cap == 480001);
  Host h;
  h.bpm = 60.0;  // the host slowed down: four bars now last 768000 frames
  h.ppq0 = 3.9;
  h.run(e, 1000000, 512);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(e.status().ended == EndReason::Overflow);
  CHECK(e.status().framesCaptured == cap);
}

CHOP_TEST(arm_rejects_oversize_requests) {
  CaptureEngine e;
  ArmSettings s;
  s.bars = 8;
  s.bpmHint = 20.0;  // 96 s
  auto st = e.arm(s);
  CHECK(!st.ok() && st.error().code == ErrorCode::OutOfRange);
  CHECK(st.error().message == "That length is 96.0 s; the maximum is 60 s.");
  CHECK(e.status().state == CaptureState::Idle);

  s = ArmSettings{};
  s.sampleRate = 384000.0;
  s.bars = 4;
  s.bpmHint = 30.0;  // 32 s of stereo at 384 kHz is 98 MB of frames... over the frame cap
  st = e.arm(s);
  CHECK(!st.ok() && st.error().code == ErrorCode::LimitExceeded);
  CHECK(st.error().hint == 11520000);
  CHECK(maxCaptureFrames(384000.0, 2) == 11520000);
  CHECK(maxCaptureFrames(48000.0, 2) == 2880000);
  CHECK(maxCaptureFrames(192000.0, 2) == 11520000);
  CHECK(maxCaptureFrames(192000.0, 1) == 11520000);
  CHECK(maxCaptureFrames(96000.0, 2) == 5760000);

  s = ArmSettings{};
  s.mode = CaptureMode::Free;
  s.sampleRate = 384000.0;
  s.maxSeconds = 60.0;
  CHECK(e.arm(s).ok());  // Free mode clamps to the limit instead of refusing
  CHECK(e.status().capacityFrames == 11520000);
  CHECK(e.arm(s).error().code == ErrorCode::Conflict);  // arm while armed
  e.requestStop();
  Host h;
  h.sr = 384000.0;
  h.run(e, 1, 1);
  CHECK(e.status().state == CaptureState::Idle);
  e.reap();
  CHECK(e.status().capacityFrames == 0);  // 0 bytes held when idle

  for (auto bad : {0, 9}) {
    s = ArmSettings{};
    s.bars = bad;
    CHECK(e.arm(s).error().code == ErrorCode::OutOfRange);
  }
  s = ArmSettings{};
  s.channels = 3;
  CHECK(e.arm(s).error().code == ErrorCode::InvalidArgument);
  s = ArmSettings{};
  s.sampleRate = 4000.0;
  CHECK(e.arm(s).error().code == ErrorCode::OutOfRange);
  s = ArmSettings{};
  s.mode = CaptureMode::Free;
  s.maxSeconds = 61.0;
  CHECK(e.arm(s).error().code == ErrorCode::OutOfRange);
  s.maxSeconds = std::numeric_limits<double>::quiet_NaN();
  CHECK(e.arm(s).error().code == ErrorCode::OutOfRange);
}

CHOP_TEST(state_machine_transitions_table) {
  CaptureEngine e;
  // Idle
  CHECK(e.status().state == CaptureState::Idle);
  CHECK(e.startNow().error().code == ErrorCode::Conflict);
  CHECK(e.take().error().code == ErrorCode::NotFound);
  e.requestStop();
  e.discard();
  CHECK(e.status().state == CaptureState::Idle);
  // Armed
  ArmSettings s;
  s.mode = CaptureMode::Free;
  CHECK(e.arm(s).ok());
  CHECK(e.arm(s).error().code == ErrorCode::Conflict);
  CHECK(e.take().error().code == ErrorCode::Conflict);
  e.discard();  // not Captured: ignored
  CHECK(e.status().state == CaptureState::Armed);
  // Armed -> Capturing
  CHECK(e.startNow().ok());
  Host h;
  h.run(e, 100, 50);
  CHECK(e.status().state == CaptureState::Capturing);
  CHECK(e.startNow().error().code == ErrorCode::Conflict);
  CHECK(e.arm(s).error().code == ErrorCode::Conflict);
  CHECK(e.take().error().code == ErrorCode::Conflict);
  // Capturing -> Captured
  e.requestStop();
  h.run(e, 10, 10);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(e.arm(s).error().code == ErrorCode::Conflict);
  CHECK(e.startNow().error().code == ErrorCode::Conflict);
  // Captured -> Idle by take, then Armed again
  CHECK(e.take().ok());
  CHECK(e.status().state == CaptureState::Idle);
  CHECK(e.take().error().code == ErrorCode::NotFound);
  // Armed -> Idle by Stop; Captured -> Idle by discard
  CHECK(e.arm(s).ok());
  e.requestStop();
  h.run(e, 10, 10);
  CHECK(e.status().state == CaptureState::Idle);
  CHECK(e.arm(s).ok());
  CHECK(e.startNow().ok());
  h.run(e, 100, 100);
  e.requestStop();
  h.run(e, 10, 10);
  CHECK(e.status().state == CaptureState::Captured);
  e.discard();
  CHECK(e.status().state == CaptureState::Idle);
  e.reap();
  CHECK(e.status().capacityFrames == 0);
  CHECK(e.arm(s).ok());  // a fresh capture after the discard
}

CHOP_TEST(stop_wins_over_a_start_in_the_same_block) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  CHECK(e.arm(s).ok());
  CHECK(e.startNow().ok());
  e.requestStop();
  Host h;
  h.run(e, 10, 10);
  CHECK(e.status().state == CaptureState::Idle);  // nothing was written, so there is nothing to keep
  CHECK(e.take().error().code == ErrorCode::NotFound);
}

CHOP_TEST(on_prepare_aborts_only_when_the_audio_settings_change) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  CHECK(e.arm(s).ok());
  CHECK(!e.onPrepare(48000.0, 2));  // the same settings: nothing happens
  CHECK(e.status().state == CaptureState::Armed);
  CHECK(e.onPrepare(44100.0, 2));
  CHECK(e.status().state == CaptureState::Idle && e.status().ended == EndReason::Reconfigured);
  CHECK(e.arm(s).ok());
  CHECK(e.startNow().ok());
  Host h;
  h.run(e, 4000, 500);
  CHECK(e.onPrepare(48000.0, 1));
  CHECK(e.status().state == CaptureState::Captured && e.status().ended == EndReason::Reconfigured);
  auto r = e.take();  // the partial audio is kept at the rate it was recorded
  CHECK(r.ok() && r.value().frames == 4000 && r.value().sampleRate == 48000);
}

CHOP_TEST(non_finite_input_stored_as_zero_and_clip_counted) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  s.channels = 1;
  CHECK(e.arm(s).ok());
  CHECK(e.startNow().ok());
  std::vector<float> x(100, 0.25f);
  x[3] = std::numeric_limits<float>::quiet_NaN();
  x[4] = std::numeric_limits<float>::infinity();
  x[5] = 1.0f;
  x[6] = -0.999f;
  x[7] = 0.998f;
  const float* in[1] = {x.data()};
  CaptureTransport t;
  e.process(in, 1, 100, t);
  CaptureStatus st = e.status();
  CHECK(st.clippedSamples == 2);
  CHECK_NEAR(st.peak, 1.0, 1e-9);
  CHECK_NEAR(st.level, 1.0, 1e-9);
  e.requestStop();
  e.process(in, 1, 100, t);
  auto r = e.take();
  CHECK(r.ok());
  CHECK(r.value().samples[3] == 0.f && r.value().samples[4] == 0.f && r.value().samples[5] == 1.0f);
}

CHOP_TEST(an_armed_capture_meters_the_input_before_it_starts) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  CHECK(e.arm(s).ok());
  Host h;
  h.run(e, 1000, 250);
  CHECK(e.status().state == CaptureState::Armed);
  CHECK(e.status().peak > 0.f && e.status().level > 0.f);
  CHECK(e.status().framesCaptured == 0);
}

CHOP_TEST(process_never_allocates) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  s.sampleRate = 8000.0;
  s.maxSeconds = 1.0;
  CHECK(e.arm(s).ok());
  Host h;
  h.sr = 8000.0;
  gAllocs = 0;
  gCountAllocs = true;
  h.run(e, 2000, 256);  // armed
  CHECK(e.startNow().ok());
  h.run(e, 30000, 256);  // capturing and wrapped (capacity 8000)
  e.requestStop();
  h.run(e, 512, 256);  // stopping and captured
  gCountAllocs = false;
  CHECK(gAllocs.load() == 0);
  CHECK(e.status().state == CaptureState::Captured);
  e.discard();

  CaptureEngine e2;
  CHECK(e2.arm(ArmSettings{}).ok());
  Host h2;
  gAllocs = 0;
  gCountAllocs = true;
  h2.run(e2, 48000 * 12, 512);  // synced: waits, starts on the bar line, completes
  gCountAllocs = false;
  CHECK(gAllocs.load() == 0);
  CHECK(e2.status().state == CaptureState::Captured);
  Host idle;
  CaptureEngine e3;
  gAllocs = 0;
  gCountAllocs = true;
  idle.run(e3, 4096, 256);  // idle
  gCountAllocs = false;
  CHECK(gAllocs.load() == 0);
}

CHOP_TEST(threaded_status_polling) {
  CaptureEngine e;
  ArmSettings s;
  s.mode = CaptureMode::Free;
  s.sampleRate = 8000.0;
  s.maxSeconds = 2.0;
  CHECK(e.arm(s).ok());
  CHECK(e.startNow().ok());
  std::atomic<bool> stop{false};
  std::thread audio([&] {
    Host h;
    h.sr = 8000.0;
    while (!stop.load(std::memory_order_acquire)) h.run(e, 128, 128);
  });
  std::int64_t last = 0;
  bool monotonic = true;
  while (e.status().framesCaptured < 20000) {
    const CaptureStatus st = e.status();
    monotonic = monotonic && st.framesCaptured >= last;
    last = st.framesCaptured;
    std::this_thread::yield();
  }
  e.requestStop();
  while (e.status().state != CaptureState::Captured) std::this_thread::yield();
  auto r = e.take();  // the audio thread keeps calling process() while this runs
  CHECK(r.ok());
  CHECK(r.value().frames == 16000);
  e.reap();
  CHECK(e.status().state == CaptureState::Idle);
  CHECK(e.arm(s).ok());  // the next capture while the audio thread is still running
  CHECK(e.startNow().ok());
  while (e.status().framesCaptured < 1000) std::this_thread::yield();
  e.requestStop();
  while (e.status().state != CaptureState::Captured) std::this_thread::yield();
  e.discard();
  e.reap();
  stop.store(true, std::memory_order_release);
  audio.join();
  CHECK(monotonic);
}
