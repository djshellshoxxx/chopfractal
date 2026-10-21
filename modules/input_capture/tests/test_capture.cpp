#include <algorithm>
#include <atomic>
#include <chrono>
#include <chopfractal/input_capture/capture.hpp>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
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

// A ramp signal: sample n has the value n, so any captured slice shows exactly which input frames it holds.
struct Feeder {
  std::int64_t pos = 0;
  std::vector<float> a, b;
  void block(Capture& cap, int frames, const Transport& tp, int channels = 1) {
    a.resize(static_cast<std::size_t>(frames));
    b.resize(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
      a[static_cast<std::size_t>(i)] = static_cast<float>(pos + i);
      b[static_cast<std::size_t>(i)] = -static_cast<float>(pos + i);
    }
    const float* in[2] = {a.data(), b.data()};
    cap.process(in, channels, frames, tp);
    pos += frames;
  }
};

Transport hostAt(double ppq, bool playing = true) {
  Transport t;
  t.playing = playing;
  t.positionValid = true;
  t.ppq = ppq;
  t.bpm = 120.0;
  t.quartersPerBar = 4.0;
  return t;
}

Options mono(std::int64_t target = 0, bool waitForBar = false) {
  Options o;
  o.channels = 1;
  o.sampleRate = 48000.0;
  o.targetFrames = target;
  o.waitForBar = waitForBar;
  return o;
}

}  // namespace

CHOP_TEST(records_immediately_and_keeps_every_frame_until_stopped) {
  Capture cap;
  CHECK(cap.state() == State::Idle);
  CHECK(cap.arm(mono()).ok());
  CHECK(cap.state() == State::Armed);
  Feeder f;
  f.block(cap, 300, hostAt(0.0, false));  // a stopped host: capture starts at once
  CHECK(cap.state() == State::Recording);
  f.block(cap, 200, hostAt(0.0, false));
  CHECK_EQ(cap.framesRecorded(), 500);
  cap.stop();
  f.block(cap, 100, hostAt(0.0, false));  // the stop lands at the start of this block: its frames are not recorded
  CHECK(cap.state() == State::Done && cap.reason() == StopReason::Manual);
  auto r = cap.take();
  CHECK(r.ok());
  if (!r.ok()) return;
  CHECK_EQ(r.value().frames, 500);
  CHECK(r.value().channels == 1 && r.value().sampleRate == 48000);
  for (std::int64_t i = 0; i < 500; ++i) CHECK_EQ(r.value().samples[static_cast<std::size_t>(i)], static_cast<float>(i));
  CHECK(cap.state() == State::Idle);
  CHECK(!cap.take().ok());  // nothing left to take
}

CHOP_TEST(a_target_length_stops_exactly_and_ignores_the_rest_of_the_block) {
  Capture cap;
  CHECK(cap.arm(mono(1000)).ok());
  Feeder f;
  for (int i = 0; i < 3; ++i) f.block(cap, 300, hostAt(0.0, false));
  CHECK(cap.state() == State::Recording);
  f.block(cap, 300, hostAt(0.0, false));  // frames 900..1199: only 100 are kept
  CHECK(cap.state() == State::Done && cap.reason() == StopReason::LengthReached);
  auto r = cap.take();
  CHECK(r.ok() && r.value().frames == 1000);
  if (r.ok())
    for (std::int64_t i = 0; i < 1000; ++i) CHECK_EQ(r.value().samples[static_cast<std::size_t>(i)], static_cast<float>(i));
}

CHOP_TEST(waiting_for_a_bar_starts_on_the_exact_frame_of_the_bar_line) {
  // 120 BPM at 48 kHz: one quarter is 24000 frames. A block starting at ppq 3.99 reaches bar 4.0 after 240 frames.
  Capture cap;
  CHECK(cap.arm(mono(1000, true)).ok());
  Feeder f;
  f.block(cap, 512, hostAt(3.99));
  CHECK(cap.state() == State::Done || cap.state() == State::Recording);
  auto r = [&] {
    for (int i = 0; i < 4 && cap.state() != State::Done; ++i) f.block(cap, 512, hostAt(4.0));
    return cap.take();
  }();
  CHECK(r.ok() && r.value().frames == 1000);
  if (r.ok()) CHECK_EQ(r.value().samples[0], 240.f);  // frame 240 of the first block

  // Already exactly on a bar line: start at the first frame.
  Capture c2;
  CHECK(c2.arm(mono(100, true)).ok());
  Feeder g;
  g.block(c2, 512, hostAt(8.0));
  auto r2 = c2.take();
  CHECK(r2.ok() && r2.value().samples[0] == 0.f);

  // Mid-bar and the bar line is not in this block: nothing is recorded yet.
  Capture c3;
  CHECK(c3.arm(mono(100, true)).ok());
  Feeder h;
  h.block(c3, 512, hostAt(1.0));
  CHECK(c3.state() == State::Armed && c3.framesRecorded() == 0);
}

CHOP_TEST(the_captured_audio_does_not_depend_on_the_host_block_size) {
  std::vector<std::vector<float>> results;
  for (int block : {1, 7, 64, 333, 512, 4096}) {
    Capture cap;
    CHECK(cap.arm(mono(5000, true)).ok());
    Feeder f;
    double ppq = 3.5;  // 12000 frames before bar 4
    for (int guard = 0; guard < 400000 && cap.state() != State::Done; ++guard) {
      f.block(cap, block, hostAt(ppq));
      ppq += static_cast<double>(block) * 120.0 / (60.0 * 48000.0);
    }
    auto r = cap.take();
    CHECK(r.ok() && r.value().frames == 5000);
    if (r.ok()) results.push_back(r.value().samples);
  }
  for (std::size_t i = 1; i < results.size(); ++i) CHECK(results[i] == results[0]);
  if (!results.empty()) CHECK_NEAR(results[0][0], 12000.0, 2.0);  // the loop starts 12000 frames in, +-1 frame of rounding
}

CHOP_TEST(channels_are_mapped_and_bad_input_is_made_safe) {
  Capture cap;
  Options o = mono();
  o.channels = 2;
  CHECK(cap.arm(o).ok());
  std::vector<float> l = {1.f, std::nanf(""), 3.f, std::numeric_limits<float>::infinity()};
  const float* mono1[1] = {l.data()};
  cap.process(mono1, 1, 4, hostAt(0.0, false));  // one input channel feeds both
  const float* none[2] = {nullptr, nullptr};
  cap.process(none, 2, 2, hostAt(0.0, false));   // missing buffers are silence
  cap.process(nullptr, 0, 2, hostAt(0.0, false));
  cap.stop();
  cap.process(mono1, 1, 4, hostAt(0.0, false));
  auto r = cap.take();
  CHECK(r.ok() && r.value().frames == 8 && r.value().channels == 2);
  if (!r.ok()) return;
  const auto& s = r.value().samples;
  const float expectL[8] = {1.f, 0.f, 3.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  for (int i = 0; i < 8; ++i) {
    CHECK_EQ(s[static_cast<std::size_t>(i)], expectL[i]);       // left channel
    CHECK_EQ(s[static_cast<std::size_t>(8 + i)], expectL[i]);   // right channel: the same mono source
  }
}

CHOP_TEST(stop_cancel_and_arm_follow_the_state_machine) {
  Capture cap;
  CHECK(!cap.take().ok());
  Options bad = mono();
  bad.channels = 3;
  CHECK(!cap.arm(bad).ok());
  bad = mono();
  bad.sampleRate = 1000.0;
  CHECK(!cap.arm(bad).ok());
  bad = mono();
  bad.targetFrames = 48000 * 61;  // over 60 seconds
  CHECK(!cap.arm(bad).ok());
  bad.targetFrames = -1;
  CHECK(!cap.arm(bad).ok());

  CHECK(cap.arm(mono()).ok());
  CHECK(!cap.arm(mono()).ok());  // already armed
  cap.stop();                    // armed but not recording: straight back to idle
  CHECK(cap.state() == State::Idle);

  CHECK(cap.arm(mono()).ok());
  Feeder f;
  f.block(cap, 100, hostAt(0.0, false));
  CHECK(!cap.arm(mono()).ok());  // recording
  cap.cancel();
  f.block(cap, 100, hostAt(0.0, false));
  CHECK(cap.state() == State::Idle && cap.framesRecorded() == 0);

  CHECK(cap.arm(mono(50)).ok());
  f.block(cap, 100, hostAt(0.0, false));
  CHECK(cap.state() == State::Done);
  CHECK(cap.arm(mono()).ok());   // re-arming a finished capture discards it
  cap.cancel();
}

CHOP_TEST(the_sixty_second_limit_stops_a_free_capture) {
  Capture cap;
  Options o = mono();
  o.sampleRate = 8000.0;
  CHECK(cap.arm(o).ok());
  Feeder f;
  for (int i = 0; i < 100 && cap.state() != State::Done; ++i) f.block(cap, 8000, hostAt(0.0, false));
  CHECK(cap.state() == State::Done && cap.reason() == StopReason::BufferFull);
  auto r = cap.take();
  CHECK(r.ok() && r.value().frames == 480000);
}

CHOP_TEST(frames_for_bars_matches_the_musical_length) {
  CHECK_EQ(framesForBars(2, 4.0, 120.0, 48000.0), 192000);
  CHECK_EQ(framesForBars(4, 4.0, 120.0, 48000.0), 384000);
  CHECK_EQ(framesForBars(1, 3.0, 90.0, 44100.0), 88200);  // one 3/4 bar at 90 BPM
  CHECK_EQ(framesForBars(0, 4.0, 120.0, 48000.0), 0);
  CHECK_EQ(framesForBars(1, 4.0, 0.0, 48000.0), 0);
  CHECK_EQ(framesForBars(1, 0.0, 120.0, 48000.0), 0);
  CHECK_EQ(framesForBars(1, 4.0, 120.0, 100.0), 0);
}

CHOP_TEST(process_never_allocates) {
  Capture cap;
  Options o = mono(20000, true);
  o.channels = 2;
  CHECK(cap.arm(o).ok());
  Feeder f;
  f.block(cap, 64, hostAt(3.9));  // size the feeder's vectors before counting
  gAllocs = 0;
  gCountAllocs = true;
  for (int i = 0; i < 200 && cap.state() != State::Done; ++i) {
    const float* in[2] = {f.a.data(), f.b.data()};
    cap.process(in, 2, 64, hostAt(4.0 + i * 0.01));
    if (i == 50) cap.stop();
  }
  gCountAllocs = false;
  CHECK_EQ(gAllocs.load(), 0);
  CHECK(cap.state() == State::Done);
}

CHOP_TEST(arming_stopping_and_taking_while_the_audio_thread_runs_never_tears_the_data) {
  Capture cap;
  std::atomic<bool> quit{false};
  std::thread audio([&] {
    std::vector<float> buf(97);
    std::int64_t pos = 0;
    while (!quit.load()) {
      for (std::size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<float>((pos + static_cast<std::int64_t>(i)) & 0xFFFF);  // exact in float, wraps every 65536
      const float* in[1] = {buf.data()};
      Transport t;
      cap.process(in, 1, static_cast<int>(buf.size()), t);
      pos += static_cast<std::int64_t>(buf.size());
    }
  });
  int kept = 0;
  for (int round = 0; round < 300; ++round) {
    Options o = mono(round % 3 == 0 ? 2000 : 0);
    CHECK(cap.arm(o).ok());
    for (int spin = 0; spin < 2000 && cap.state() == State::Armed; ++spin) std::this_thread::yield();
    if (round % 3 != 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      cap.stop();
    }
    for (int spin = 0; spin < 200000 && cap.state() != State::Done && cap.state() != State::Idle; ++spin) std::this_thread::yield();
    if (cap.state() == State::Done) {
      auto r = cap.take();
      CHECK(r.ok());
      if (r.ok()) {
        ++kept;
        const auto& s = r.value().samples;
        for (std::size_t i = 1; i < s.size(); ++i) CHECK_EQ(s[i], std::fmod(s[i - 1] + 1.f, 65536.f));  // one unbroken run of the input
      }
    } else {
      cap.cancel();
      for (int spin = 0; spin < 200000 && cap.state() != State::Idle; ++spin) std::this_thread::yield();
    }
  }
  quit = true;
  audio.join();
  CHECK(kept > 50);
}
