// F09: the armed loop-boundary switch. 120 BPM at 48 kHz: 1 tick = 25 frames; a 3840-tick bar = 96000 frames.
#include <algorithm>
#include <atomic>
#include <chopfractal/audio_renderer/renderer.hpp>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "armed_fixture.hpp"
#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;
using namespace armedfx;

namespace {

constexpr int kBar = 96000;     // frames per 3840-tick bar at 120 BPM / 48 kHz
constexpr double kTpf = 0.04;   // ticks per frame

// Regions: [0,40000) = +0.5, [40000,41000) = -0.25, [41000,42000) = +0.125 (a click per scene), the rest 0.
SourcePtr clickSource() {
  auto s = std::make_shared<SourceData>();
  s->channels = 1;
  s->sampleRate = 48000;
  s->frames = 48000;
  s->samples.assign(48000, 0.f);
  for (int i = 0; i < 40000; ++i) s->samples[static_cast<std::size_t>(i)] = 0.5f;
  for (int i = 40000; i < 41000; ++i) s->samples[static_cast<std::size_t>(i)] = -0.25f;
  for (int i = 41000; i < 42000; ++i) s->samples[static_cast<std::size_t>(i)] = 0.125f;
  return s;
}

FlatEvent clickA(std::uint64_t id, Ticks start) { return evt(id, 0, 40000, start, 240); }          // +0.5 click, 6000 frames
FlatEvent clickB(std::uint64_t id, Ticks start) { return evt(id, 40000, 41000, start, 120); }     // -0.25 click, 3000 frames
FlatEvent clickC(std::uint64_t id, Ticks start) { return evt(id, 41000, 42000, start, 120); }     // +0.125 click

struct Run {
  Renderer r;
  std::vector<float> out;  // left channel
  explicit Run(int maxBlock = 2048) {
    Config c;
    c.maxBlock = maxBlock;
    r.prepare(c);
  }
  int pos = 0;  // host frame of the next block
  // Publishes `a`, plays a few frames (position 0 is itself a loop boundary), then arms `b`.
  void start(const std::shared_ptr<const Playback>& a, const std::shared_ptr<const Playback>& b, std::uint64_t token, int block = 512) {
    r.mailbox().publish(a);
    render(64, block);
    if (b) arm(b, token);
  }
  // Renders `frames` frames from the current position with the given block size (valid ppq).
  void render(int frames, int block, bool playing = true) {
    const int from = pos;
    pos += frames;
    std::vector<float> x(static_cast<std::size_t>(block), 0.f), oL(static_cast<std::size_t>(block)), oR(static_cast<std::size_t>(block));
    const float* in[2] = {x.data(), x.data()};
    float* o[2] = {oL.data(), oR.data()};
    RenderParams p;
    for (int done = 0; done < frames; done += block) {
      const int n = std::min(block, frames - done);
      TransportBlock tb;
      tb.playing = playing;
      tb.positionValid = true;
      tb.bpm = 120.0;
      tb.ppq = static_cast<double>(from + done) * kTpf / 960.0;
      r.process(tb, p, in, o, 2, n);
      out.insert(out.end(), oL.begin(), oL.begin() + n);
    }
  }
  void arm(const std::shared_ptr<const Playback>& pb, std::uint64_t token) { r.armMailbox().publish(std::make_shared<const ArmedSwitch>(ArmedSwitch{pb, token})); }
};

int firstNonZero(const std::vector<float>& v, int from) {
  for (std::size_t i = static_cast<std::size_t>(from); i < v.size(); ++i)
    if (v[i] != 0.f) return static_cast<int>(i);
  return -1;
}

std::uint64_t fnvOf(const std::vector<float>& v) {
  std::uint64_t h = 1469598103934665603ull;
  hashInto(h, v.data(), static_cast<int>(v.size()));
  return h;
}

}  // namespace

CHOP_TEST(armed_switch_lands_exactly_on_the_loop_boundary_frame) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0), clickA(2, 1920)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0), clickB(2, 960)}, 3840);
  Run run;
  run.start(A, B, 1);
  CHECK_EQ(run.r.appliedSwitchToken(), 0u);
  run.render(2 * kBar - 64, 512);
  CHECK_EQ(run.r.appliedSwitchToken(), 1u);
  const auto& L = run.out;
  // Loop 0 is scene A: its click starts at frame 0 (the attack ramp makes the first audible sample frame 1).
  CHECK(firstNonZero(L, 0) == 1);
  CHECK(L[2000] > 0.f);
  CHECK(L[48000 + 2000] > 0.f);  // A's second click
  // Loop 1 is scene B. Its tick-0 click first sounds at frame kBar + 1, within one frame of the boundary.
  const int firstB = firstNonZero(L, kBar - 100);
  CHECK(firstB >= kBar && firstB <= kBar + 2);
  CHECK(L[kBar + 500] < 0.f);
  for (int i = kBar - 100; i < kBar + 6000; ++i) CHECK(L[static_cast<std::size_t>(i)] <= 0.f);  // no A event after the boundary
  CHECK(L[kBar + 24000 + 500] < 0.f);                                                          // B's second click (tick 960)
  CHECK(L[kBar + 48000 + 2000] == 0.f);                                                         // A's second-click slot stays silent
  // The reference: scene B playing alone from the same host position matches the switched run bit for bit.
  Run ref;
  ref.r.mailbox().publish(B);
  ref.pos = kBar;
  ref.render(kBar, 512);
  CHECK(std::equal(ref.out.begin(), ref.out.end(), L.begin() + kBar));
}

CHOP_TEST(switch_is_identical_for_block_sizes_1_7_64_333_512_2048) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0), clickA(2, 1920), clickA(3, 2400)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0), clickB(2, 960)}, 3840);
  std::vector<std::uint64_t> hashes;
  for (int block : {2048, 1, 7, 64, 333, 512}) {
    Run run;
    run.start(A, B, 1, block);
    run.render(kBar + 30000 - 64, block);
    CHECK_EQ(run.r.appliedSwitchToken(), 1u);
    hashes.push_back(fnvOf(run.out));
  }
  for (std::size_t i = 1; i < hashes.size(); ++i) CHECK_EQ(hashes[i], hashes[0]);
}

CHOP_TEST(voices_ring_over_the_switch) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {evt(1, 0, 40000, 3000, 1800)}, 3840);  // starts at tick 3000, sounds until tick 4800 (frame 120000)
  const auto B = pbOf(src, {clickB(1, 0)}, 3840);
  Run run;
  run.start(A, B, 1);
  run.render(2 * kBar - 64, 512);
  Run a, b;
  a.r.mailbox().publish(A);
  a.render(2 * kBar, 512);
  b.r.mailbox().publish(B);
  b.pos = kBar;
  b.render(kBar, 512);
  const std::size_t n = static_cast<std::size_t>(kBar);
  CHECK(run.out[n + 1000] > 0.2f);  // A's note is still sounding after the boundary (B's click is only -0.25)
  for (int i = 0; i < 20000; ++i) {
    const std::size_t k = n + static_cast<std::size_t>(i);
    CHECK_NEAR(run.out[k], a.out[k] + b.out[static_cast<std::size_t>(i)], 1e-6);
  }
}

CHOP_TEST(cancel_token_clears_a_pending_switch) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0)}, 3840);
  Run run, ref;
  run.start(A, B, 1);
  run.render(kBar - 5000 - 64, 512);
  run.arm(nullptr, 2);
  run.render(13000, 512);
  CHECK_EQ(run.r.appliedSwitchToken(), 0u);
  ref.r.mailbox().publish(A);
  ref.render(kBar + 8000, 512);
  CHECK(run.out == ref.out);
  CHECK(run.out[kBar + 500] > 0.f);  // still scene A
}

CHOP_TEST(switch_is_ignored_for_a_different_source_pointer_or_pass_through) {
  const SourcePtr src = clickSource();
  const SourcePtr other = clickSource();
  const auto A = pbOf(src, {clickA(1, 0)}, 3840);
  const auto Bother = pbOf(other, {clickB(1, 0)}, 3840);
  auto pass = makePassThroughPlayback(src);
  CHECK(pass.ok());
  for (int variant = 0; variant < 2; ++variant) {
    Run run, ref;
    run.start(A, variant == 0 ? Bother : pass.value(), 1);
    run.render(2 * kBar - 64, 512);
    ref.r.mailbox().publish(A);
    ref.render(2 * kBar, 512);
    CHECK_EQ(run.r.appliedSwitchToken(), 0u);
    CHECK(run.out == ref.out);
  }
}

CHOP_TEST(main_mailbox_publish_of_the_same_playback_neither_reverts_nor_kills_voices) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {evt(1, 0, 40000, 3000, 1800), clickA(2, 0)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0), clickB(2, 2000)}, 3840);
  Run withRepublish, without;
  for (Run* run : {&withRepublish, &without}) {
    run->start(A, B, 1);
    run->render(kBar + 3000 - 64, 512);  // A's long note is still sounding here
    CHECK_EQ(run->r.appliedSwitchToken(), 1u);
  }
  CHECK(withRepublish.r.activeVoices() >= 1);
  withRepublish.r.mailbox().publish(B);  // what the session does after observing the switch
  for (Run* run : {&withRepublish, &without}) run->render(2 * kBar - 3000 + 3000, 512);
  CHECK(withRepublish.out == without.out);
  CHECK(withRepublish.out[static_cast<std::size_t>(2 * kBar + 500)] < 0.f);  // loop 2 is still scene B, not A
}

CHOP_TEST(main_mailbox_publish_of_a_new_playback_replaces_a_boundary_switch) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0)}, 3840);
  const auto C = pbOf(src, {clickC(1, 0)}, 3840);
  Run run;
  run.start(A, B, 1);
  run.render(kBar + 4000 - 64, 512);
  CHECK(run.out[static_cast<std::size_t>(kBar + 500)] < 0.f);
  run.r.mailbox().publish(C);  // an edit made after the switch wins over it
  run.render(kBar - 4000 + 4000, 512);
  CHECK(run.out[static_cast<std::size_t>(2 * kBar + 500)] > 0.f);  // loop 2 is C (+0.125), where B would be negative
}

CHOP_TEST(a_stale_arm_in_the_mailbox_does_not_come_back_after_prepare) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0)}, 3840);
  Run run;
  run.start(A, B, 1);
  run.render(kBar + 10 - 64, 512);
  CHECK_EQ(run.r.appliedSwitchToken(), 1u);
  run.r.mailbox().publish(A);  // the session restores scene A
  Config c;
  run.r.prepare(c);            // device restart
  run.out.clear();
  run.pos = 2 * kBar - 1000;
  run.render(3000, 512);
  CHECK(run.out[1000 + 2000] > 0.f);  // loop 2 is A (positive), not the old arm's B
}

CHOP_TEST(scenes_of_different_lengths_switch_on_the_current_scenes_boundary) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0)}, 3840);                    // 1 bar
  const auto B = pbOf(src, {clickB(1, 0), clickB(2, 4000)}, 7680);   // 2 bars: the second click is at tick 4000
  Run run;
  run.start(A, B, 1);
  run.render(2 * kBar - 64, 512);
  CHECK_EQ(run.r.appliedSwitchToken(), 1u);
  // Switched at tick 3840; B plays at the host's phase, so its tick-4000 click lands 160 ticks (4000 frames) later.
  CHECK(run.out[static_cast<std::size_t>(kBar + 3000)] == 0.f);
  CHECK(run.out[static_cast<std::size_t>(kBar + 4000 + 500)] < 0.f);
  CHECK(run.out[2000] > 0.f);
}

CHOP_TEST(a_stopped_transport_holds_the_arm_until_playback_resumes) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0)}, 3840);
  Run run;
  run.start(A, B, 1);
  run.pos = kBar - 4096;
  run.render(kBar, 512, false);  // the host is stopped for a whole bar: nothing applies
  CHECK_EQ(run.r.appliedSwitchToken(), 0u);
  run.pos = kBar - 4096;
  run.render(4096 + 4096, 512, true);
  CHECK_EQ(run.r.appliedSwitchToken(), 1u);
}

CHOP_TEST(a_start_exactly_on_a_loop_point_is_not_a_boundary_crossing) {
  const SourcePtr src = clickSource();
  const auto A = pbOf(src, {clickA(1, 0)}, 3840);
  const auto B = pbOf(src, {clickB(1, 0)}, 3840);
  Run run;
  run.r.mailbox().publish(A);
  run.arm(B, 1);
  run.render(512, 512);  // the first block starts at host frame 0, exactly on a loop point
  CHECK_EQ(run.r.appliedSwitchToken(), 0u);
  CHECK(run.out[200] > 0.f);  // still scene A
  run.render(kBar + 100 - 512, 512);
  CHECK_EQ(run.r.appliedSwitchToken(), 1u);  // the arm waited for the next real crossing
  CHECK(run.out[static_cast<std::size_t>(kBar + 50)] < 0.f);
}

CHOP_TEST(no_arm_means_bit_identical_output_to_the_previous_renderer) {
  CHECK_EQ(noArmScenarioHash(), 4077986580202376823ull);
}

CHOP_TEST(arm_mailbox_stress_under_tsan) {
  const SourcePtr src = clickSource();
  std::vector<std::shared_ptr<const Playback>> pbs;
  for (int i = 0; i < 6; ++i) pbs.push_back(pbOf(src, {clickA(1, static_cast<Ticks>(i * 100)), clickB(2, 960)}, 3840));
  Run run(512);
  run.r.mailbox().publish(pbs[0]);
  std::atomic<bool> done{false};
  std::atomic<std::uint64_t> published{0};
  std::thread publisher([&] {
    std::uint64_t token = 0;
    int i = 0;
    while (!done.load()) {
      ++token;
      const bool cancel = (token % 5) == 0;
      run.arm(cancel ? nullptr : pbs[static_cast<std::size_t>(1 + (i++ % 5))], token);
      if (token % 7 == 0) run.r.mailbox().publish(pbs[static_cast<std::size_t>(token % 6)]);
      published.store(token);
      std::this_thread::yield();
    }
  });
  std::vector<float> x(256, 0.f), oL(256), oR(256);
  const float* in[2] = {x.data(), x.data()};
  float* o[2] = {oL.data(), oR.data()};
  RenderParams p;
  std::uint64_t lastApplied = 0;
  bool finite = true;
  for (int i = 0; i < 6000; ++i) {
    TransportBlock tb;
    tb.playing = true;
    tb.positionValid = true;
    tb.ppq = static_cast<double>(i) * 256.0 * kTpf / 960.0;
    run.r.process(tb, p, in, o, 2, 256);
    for (float f : oL) finite = finite && std::isfinite(f);
    const std::uint64_t a = run.r.appliedSwitchToken();
    CHECK(a >= lastApplied);
    lastApplied = a;
  }
  done = true;
  publisher.join();
  CHECK(finite);
  CHECK(lastApplied <= published.load());
}
