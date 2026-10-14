#include <chopfractal/input_capture/capture.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "chop_test.hpp"

// ---- allocation counter: process() must never allocate ----
// The replaced operators are non-inlinable so optimizing compilers always see a matching operator new /
// operator delete pair (GCC's -Wmismatched-new-delete otherwise flags free() inside an inlined delete).
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
// Every form must be replaced together (std::stable_sort uses the nothrow form), or sanitizers report a
// new/free mismatch.
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
constexpr double kSr = 48000.0;

struct Feeder {
  CaptureEngine& e;
  double ppq0 = 0.3, bpm = 120.0;
  bool playing = true, valid = true;
  std::int64_t idx = 0;  // absolute sample index; input value is idx + 1
  std::vector<float> a, b;
  explicit Feeder(CaptureEngine& eng) : e(eng) {}
  void run(std::int64_t frames, int block, int numCh = 2) {
    std::int64_t done = 0;
    while (done < frames) {
      const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
      a.resize(n);
      b.resize(n);
      for (int i = 0; i < n; ++i) {
        a[i] = static_cast<float>(idx + i + 1);
        b[i] = -a[i];
      }
      const float* in[2] = {a.data(), b.data()};
      CaptureTransport t;
      t.playing = playing;
      t.positionValid = valid;
      t.bpm = bpm;
      t.ppq = ppq0 + idx * bpm / (60.0 * kSr);
      e.process(in, numCh, n, t);
      idx += n;
      done += n;
    }
  }
};

ArmSettings synced(int bars = 4) {
  ArmSettings s;
  s.bars = bars;
  return s;
}
ArmSettings freeMode(double seconds = 30.0, double sr = kSr) {
  ArmSettings s;
  s.mode = CaptureMode::Free;
  s.maxSeconds = seconds;
  s.sampleRate = sr;
  return s;
}
}  // namespace

CHOP_TEST(synced_capture_starts_on_bar_line_with_sample_accuracy) {
  CaptureEngine e;
  CHECK(e.arm(synced()));
  Feeder f(e);
  f.run(kSr * 12, 512);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(e.status().ended == EndReason::Completed);
  auto r = e.take();
  CHECK(r.ok());
  const auto& a = r.value();
  CHECK_EQ(a.channels, 2);
  CHECK_EQ(a.sampleRate, 48000);
  CHECK(std::llabs(a.frames - 384000) <= 1);
  const std::int64_t first = static_cast<std::int64_t>(std::ceil((4.0 - 0.3) * 0.5 * kSr - 1e-6));  // 88800
  CHECK_EQ(a.samples[0], static_cast<float>(first + 1));
  CHECK_EQ(a.samples[a.frames], -static_cast<float>(first + 1));  // channel 1
  CHECK_EQ(a.samples[a.frames - 1], static_cast<float>(first + a.frames));
  CHECK(e.status().state == CaptureState::Idle);
}

CHOP_TEST(capture_is_block_size_independent) {
  std::vector<float> ref;
  for (int block : {1, 7, 64, 333, 2048}) {
    CaptureEngine e;
    CHECK(e.arm(synced(1)));
    Feeder f(e);
    // 1 sample blocks are slow; a 1-bar capture is 96000 frames, which is fine.
    f.run(kSr * 4, block);
    auto r = e.take();
    CHECK(r.ok());
    if (ref.empty()) ref = r.value().samples;
    else CHECK(ref == r.value().samples);
  }
  CHECK(ref.size() > 0);
}

CHOP_TEST(free_ring_keeps_the_last_capacity_frames_in_order) {
  CaptureEngine e;
  ArmSettings s = freeMode(1.0, 8000.0);  // capacity 8000
  CHECK(e.arm(s));
  CHECK(e.startNow());
  Feeder f(e);
  f.run(1, 1);  // the start request is consumed at the next block
  f.run(20000 - 1, 333);
  e.requestStop();
  f.run(1, 1);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(e.status().ended == EndReason::UserStop);
  auto r = e.take();
  CHECK(r.ok());
  const auto& a = r.value();
  CHECK_EQ(a.frames, 8000);
  CHECK(a.wrapped);
  for (int i = 0; i < 8000; ++i) CHECK_EQ(a.samples[i], static_cast<float>(12000 + i + 1));
}

CHOP_TEST(mono_input_duplicated_to_stereo_and_stereo_to_mono_uses_channel_0) {
  CaptureEngine e;
  CHECK(e.arm(freeMode(1.0)));
  CHECK(e.startNow());
  Feeder f(e);
  f.run(100, 100, 1);
  e.requestStop();
  f.run(1, 1, 1);
  auto r = e.take();
  CHECK(r.ok());
  CHECK_EQ(r.value().frames, 100);
  CHECK(std::equal(r.value().samples.begin(), r.value().samples.begin() + 100, r.value().samples.begin() + 100));

  ArmSettings m = freeMode(1.0);
  m.channels = 1;
  CHECK(e.arm(m));
  CHECK(e.startNow());
  Feeder g(e);
  g.run(50, 50, 2);
  e.requestStop();
  g.run(1, 1);
  auto r2 = e.take();
  CHECK(r2.ok());
  CHECK_EQ(r2.value().channels, 1);
  CHECK_EQ(r2.value().samples.size(), static_cast<std::size_t>(50));
  CHECK_EQ(r2.value().samples[0], 1.f);  // channel 0, never a mix
}

CHOP_TEST(transport_stop_or_seek_ends_capture_with_partial_audio) {
  for (int mode = 0; mode < 2; ++mode) {
    CaptureEngine e;
    CHECK(e.arm(synced()));
    Feeder f(e);
    f.ppq0 = 0.0;  // bar line at once
    f.run(4096, 512);
    CHECK(e.status().state == CaptureState::Capturing);
    if (mode == 0) f.playing = false;
    else f.ppq0 += 100.0;  // seek
    f.run(512, 512);
    CHECK(e.status().state == CaptureState::Captured);
    CHECK(e.status().ended == EndReason::TransportStopped);
    auto r = e.take();
    CHECK(r.ok());
    CHECK_EQ(r.value().frames, 4096);
  }
}

CHOP_TEST(tempo_drop_beyond_headroom_ends_with_overflow) {
  CaptureEngine e;
  CHECK(e.arm(synced(1)));  // 2 s at 120 BPM, capacity 2.5 s + 1
  Feeder f(e);
  f.ppq0 = 0.0;
  f.bpm = 120.0;
  f.run(512, 512);
  // Host slows the tempo a lot: the bar takes far longer than the headroom allows. Keep ppq continuous.
  f.bpm = 30.0;
  f.ppq0 = 512 * 120.0 / (60.0 * kSr) - 512 * 30.0 / (60.0 * kSr);
  f.run(kSr * 6, 512);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(e.status().ended == EndReason::Overflow);
  CHECK_EQ(e.status().framesCaptured, e.status().capacityFrames);
}

CHOP_TEST(arm_rejects_oversize_requests) {
  CaptureEngine e;
  ArmSettings s = synced(8);
  s.bpmHint = 20.0;
  auto st = e.arm(s);
  CHECK(!st.ok() && st.error().code == ErrorCode::OutOfRange);
  CHECK(st.error().message.find("96.0 s") != std::string::npos);
  s = synced(4);
  s.sampleRate = 384000.0;
  s.bpmHint = 40.0;  // 24 s at 384 kHz = 9.2M frames fits
  CHECK(e.arm(s));
  e.requestStop();
  CaptureTransport t;
  float z[4] = {};
  const float* in[2] = {z, z};
  e.process(in, 2, 4, t);
  e.reap();
  s.bpmHint = 17.0;  // 56.5 s at 384 kHz exceeds the frame cap
  st = e.arm(s);
  CHECK(!st.ok() && st.error().code == ErrorCode::LimitExceeded);
  CHECK_EQ(st.error().hint, 11520000);
  CHECK_EQ(maxCaptureFrames(2, 384000.0), 11520000);
  CHECK_EQ(maxCaptureFrames(2, 48000.0), 2880000);
  ArmSettings fr = freeMode(60.0, 384000.0);  // ring clamps
  CHECK(e.arm(fr));
  CHECK_EQ(e.status().capacityFrames, 11520000);
  CHECK(!e.arm(fr).ok() && e.arm(fr).error().code == ErrorCode::Conflict);
  s = synced();
  s.channels = 3;
  e.requestStop();
  e.process(in, 2, 4, t);
  CHECK(!e.arm(s).ok());
  s.channels = 2;
  s.sampleRate = 100.0;
  CHECK(!e.arm(s).ok());
}

CHOP_TEST(state_machine_transitions_table) {
  CaptureEngine e;
  CHECK(!e.startNow().ok());                                    // Idle
  CHECK(e.take().error().code == ErrorCode::NotFound);          // Idle
  CHECK(e.arm(freeMode(1.0)));                                  // -> Armed
  CHECK(e.take().error().code == ErrorCode::Conflict);          // Armed
  CHECK(e.startNow());
  Feeder f(e);
  f.run(64, 64);                                                // -> Capturing
  CHECK(e.status().state == CaptureState::Capturing);
  CHECK(e.take().error().code == ErrorCode::Conflict);
  e.discard();                                                  // no-op while capturing
  CHECK(e.status().state == CaptureState::Capturing);
  e.requestStop();
  f.run(64, 64);
  CHECK(e.status().state == CaptureState::Captured);
  CHECK(!e.arm(freeMode(1.0)).ok());                            // Captured -> Conflict
  e.discard();                                                  // -> Idle
  CHECK(e.status().state == CaptureState::Idle);
  CHECK(e.status().capacityFrames == 0);
  CHECK(e.arm(freeMode(1.0)));                                  // Armed -> stop -> Idle
  e.requestStop();
  f.run(8, 8);
  CHECK(e.status().state == CaptureState::Idle);
  e.reap();
  // Reconfiguring aborts: Armed discards, Capturing keeps partial audio.
  CHECK(e.arm(freeMode(1.0)));
  e.onPrepare(kSr, 2);  // same format: a plain transport restart does not abort
  CHECK(e.status().state == CaptureState::Armed);
  e.onPrepare(44100.0, 2);
  CHECK(e.status().state == CaptureState::Idle && e.status().ended == EndReason::Reconfigured);
  CHECK(e.arm(freeMode(1.0)));
  CHECK(e.startNow());
  f.run(32, 32);
  e.onPrepare(44100.0, 2);
  CHECK(e.status().state == CaptureState::Captured && e.status().ended == EndReason::Reconfigured);
  CHECK(e.take().ok());
  // Synced capture never starts without a host position.
  CHECK(e.arm(synced(1)));
  f.valid = false;
  f.run(4096, 512);
  CHECK(e.status().state == CaptureState::Armed);
}

CHOP_TEST(non_finite_input_stored_as_zero_and_clip_counted) {
  CaptureEngine e;
  CHECK(e.arm(freeMode(1.0)));
  CHECK(e.startNow());
  float l[4] = {std::numeric_limits<float>::quiet_NaN(), 1.0f, -0.5f, std::numeric_limits<float>::infinity()};
  const float* in[2] = {l, l};
  CaptureTransport t;
  e.process(in, 2, 4, t);
  e.requestStop();
  e.process(in, 2, 4, t);
  CHECK_EQ(e.status().clippedSamples, 2u);  // the 1.0 in each channel
  CHECK_EQ(e.status().peak, 1.0f);
  CHECK_EQ(e.takeRecentPeak(), 1.0f);
  CHECK_EQ(e.takeRecentPeak(), 0.0f);  // consumed: a meter decays between polls
  auto r = e.take();
  CHECK(r.ok());
  CHECK_EQ(r.value().samples[0], 0.f);
  CHECK_EQ(r.value().samples[1], 1.f);
  CHECK_EQ(r.value().samples[3], 0.f);
}

CHOP_TEST(process_never_allocates) {
  CaptureEngine e;
  std::vector<float> blk(512, 0.1f);
  const float* in[2] = {blk.data(), blk.data()};
  CaptureTransport t;
  t.playing = t.positionValid = true;
  CHECK(e.arm(freeMode(1.0, 8000.0)));
  CHECK(e.startNow());
  gAllocs = 0;
  gCountAllocs = true;
  e.process(in, 2, 512, t);  // idle->capturing
  for (int i = 0; i < 40; ++i) e.process(in, 2, 512, t);  // wraps the ring
  e.requestStop();
  e.process(in, 2, 512, t);
  e.process(in, 2, 512, t);  // captured: no-op
  gCountAllocs = false;
  CHECK_EQ(gAllocs.load(), 0);
  CHECK(e.status().state == CaptureState::Captured);
  e.discard();
  CHECK(e.arm(synced(1)));
  gAllocs = 0;
  gCountAllocs = true;
  for (int i = 0; i < 400; ++i) {
    t.ppq = i * 512 * 120.0 / (60.0 * kSr);
    e.process(in, 2, 512, t);
  }
  gCountAllocs = false;
  CHECK_EQ(gAllocs.load(), 0);
  CHECK(e.status().state == CaptureState::Captured);
}

CHOP_TEST(threaded_status_polling) {
  CaptureEngine e;
  CHECK(e.arm(freeMode(1.0, 8000.0)));
  CHECK(e.startNow());
  std::atomic<bool> go{true};
  std::thread audio([&] {
    std::vector<float> blk(64, 0.2f);
    const float* in[2] = {blk.data(), blk.data()};
    CaptureTransport t;
    while (go.load()) e.process(in, 2, 64, t);
  });
  while (e.status().state != CaptureState::Capturing) std::this_thread::yield();
  for (int i = 0; i < 2000; ++i) (void)e.status();
  e.requestStop();
  while (e.status().state != CaptureState::Captured) std::this_thread::yield();
  go = false;
  audio.join();
  auto r = e.take();
  CHECK(r.ok());
  CHECK(r.value().frames > 0);
  e.reap();
}
