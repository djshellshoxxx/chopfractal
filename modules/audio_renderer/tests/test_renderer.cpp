#include <algorithm>
#include <limits>
#include <atomic>
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/rng.hpp>
#include <cmath>
#include <cstdlib>
#include <new>
#include <chrono>
#include <thread>

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
using namespace chopfractal::render;

namespace {

SourcePtr makeSource(int channels, int rate, std::int64_t frames, float (*fn)(std::int64_t, std::int64_t, int)) {
  auto s = std::make_shared<SourceData>();
  s->channels = channels;
  s->sampleRate = rate;
  s->frames = frames;
  s->samples.resize(static_cast<std::size_t>(channels * frames));
  for (int c = 0; c < channels; ++c)
    for (std::int64_t i = 0; i < frames; ++i) s->samples[static_cast<std::size_t>(c * frames + i)] = fn(i, frames, c);
  return s;
}
float dc(std::int64_t, std::int64_t, int) { return 1.f; }
float ramp(std::int64_t i, std::int64_t n, int) { return static_cast<float>(i) / static_cast<float>(n); }

FlatEvent ev(std::uint64_t id, std::int64_t regionEnd, Ticks start, Ticks dur) {
  FlatEvent e;
  e.id = EventId{id};
  e.chop = ChopId{1};
  e.region = {0, regionEnd};
  e.start = start;
  e.duration = dur;
  return e;
}

std::shared_ptr<const Playback> playback(SourcePtr src, const FlatEventList& events, Ticks length = 3840) {
  auto r = makePlayback(std::move(src), events, length);
  CHECK(r.ok());
  return r.ok() ? r.value() : nullptr;
}

std::vector<std::vector<float>> renderWith(const std::shared_ptr<const Playback>& pb, int block = 512, int cycles = 1, double tail = 0.0, double bpm = 120.0, double sr = 48000.0) {
  OfflineSettings s;
  s.blockSize = block;
  s.cycles = cycles;
  s.tailSeconds = tail;
  s.bpm = bpm;
  s.sampleRate = sr;
  auto r = renderOffline(pb, s);
  CHECK(r.ok());
  return r.ok() ? r.value() : std::vector<std::vector<float>>(2);
}

bool allFinite(const std::vector<float>& v) {
  for (float x : v)
    if (!std::isfinite(x)) return false;
  return true;
}

}  // namespace

CHOP_TEST(events_start_on_the_exact_sample_and_fade_in_and_out) {
  // 120 bpm at 48 kHz: one beat (960 ticks) is exactly 24000 frames.
  auto pb = playback(makeSource(1, 48000, 24000, dc), {ev(1, 24000, 960, 960)});
  auto out = renderWith(pb);
  const auto& L = out[0];
  CHECK_EQ(L.size(), 96000u);  // 4 beats at 120 bpm = 2 s
  CHECK_NEAR(L[23999], 0.0, 0.0);
  CHECK_NEAR(L[24000], 0.0, 0.0);              // age 0: the attack ramp starts at silence
  CHECK_NEAR(L[24000 + 16], 0.5, 1e-6);        // halfway through the 32-frame default attack
  CHECK_NEAR(L[24000 + 32], 1.0, 1e-6);
  CHECK_NEAR(L[24000 + 5000], 1.0, 1e-6);
  CHECK_NEAR(L[24000 + 24000 - 1], 0.0, 1e-6); // released to silence at the gate end
  CHECK_NEAR(L[48000], 0.0, 0.0);
  CHECK(out[1] == out[0]);                     // centered mono source feeds both channels equally
}

CHOP_TEST(reverse_pitch_level_and_pan_transforms) {
  auto src = makeSource(1, 48000, 24000, ramp);
  FlatEvent rev = ev(1, 24000, 0, 960);
  rev.tx.reverse = true;
  auto out = renderWith(playback(src, {rev}));
  CHECK_NEAR(out[0][100], 23899.0 / 24000.0, 1e-5);  // reads backwards from the region end

  FlatEvent up = ev(1, 24000, 0, 960);
  up.tx.pitchSemitones = 12.f;
  out = renderWith(playback(src, {up}));
  CHECK_NEAR(out[0][500], 1000.0 / 24000.0, 1e-5);   // double speed
  CHECK_NEAR(out[0][12100], 0.0, 0.0);               // and half the length (resampling, not time-stretch)

  FlatEvent lvl = ev(1, 24000, 0, 960);
  lvl.tx.level = 0.5f;
  lvl.tx.pan = 1.f;
  auto dcSrc = makeSource(1, 48000, 24000, dc);
  out = renderWith(playback(dcSrc, {lvl}));
  CHECK_NEAR(out[0][1000], 0.0, 1e-7);               // hard right silences the left side
  CHECK_NEAR(out[1][1000], 0.5, 1e-6);
  lvl.tx.pan = -1.f;
  out = renderWith(playback(dcSrc, {lvl}));
  CHECK_NEAR(out[1][1000], 0.0, 1e-7);
  CHECK_NEAR(out[0][1000], 0.5, 1e-6);
}

CHOP_TEST(sample_rate_conversion_preserves_real_duration) {
  // A 1 s source at 44.1 kHz must last 48000 frames at 48 kHz.
  auto pb = playback(makeSource(1, 44100, 44100, dc), {ev(1, 44100, 0, 3840)}, 7680);
  auto out = renderWith(pb);
  CHECK_NEAR(out[0][47000], 1.0, 1e-5);
  CHECK_NEAR(out[0][48100], 0.0, 0.0);
}

CHOP_TEST(custom_chop_fades_override_the_defaults) {
  FlatEvent e = ev(1, 24000, 0, 960);
  e.fadeInFrames = 100;
  auto out = renderWith(playback(makeSource(1, 48000, 24000, dc), {e}));
  CHECK_NEAR(out[0][50], 0.5, 1e-6);
  CHECK_NEAR(out[0][100], 1.0, 1e-6);
}

CHOP_TEST(retrigger_is_expanded_into_decaying_sub_hits) {
  FlatEvent e = ev(1, 24000, 0, 960);
  e.tx.retrigger = 4;
  auto pb = playback(makeSource(1, 48000, 24000, dc), {e});
  CHECK_EQ(pb->events.size(), 4u);
  const Ticks starts[] = {0, 240, 480, 720};
  float level = 1.f;
  for (int k = 0; k < 4; ++k) {
    CHECK_EQ(pb->events[static_cast<std::size_t>(k)].start, starts[k]);
    CHECK_EQ(pb->events[static_cast<std::size_t>(k)].duration, 240);
    CHECK_NEAR(pb->events[static_cast<std::size_t>(k)].tx.level, level, 1e-6);
    CHECK_EQ(pb->events[static_cast<std::size_t>(k)].tx.retrigger, 1);
    level *= 0.8f;
  }
  auto out = renderWith(pb);
  CHECK_NEAR(out[0][3000], 1.0, 1e-5);     // first hit
  CHECK_NEAR(out[0][9000], 0.8, 1e-5);     // second hit, decayed
}

CHOP_TEST(playback_validation_rejects_bad_input) {
  auto src = makeSource(1, 48000, 1000, dc);
  CHECK(!makePlayback(nullptr, {}, 3840).ok());
  CHECK(!makePlayback(src, {}, 0).ok());
  CHECK(!makePlayback(src, {}, 1).ok());                          // shorter than the minimum loop
  CHECK(!makePlayback(src, {}, std::numeric_limits<Ticks>::max()).ok());  // would overflow the tick math
  CHECK(makePlayback(src, {}, 3840).ok());  // an empty pattern is valid
  CHECK(!makePlayback(src, {ev(1, 2000, 0, 100)}, 3840).ok());   // reads past the source
  CHECK(!makePlayback(src, {ev(1, 500, 3840, 100)}, 3840).ok()); // starts at the loop end
  CHECK(!makePlayback(src, {ev(1, 500, 0, 0)}, 3840).ok());
  FlatEvent bad = ev(1, 500, 0, 100);
  bad.tx.level = std::nanf("");
  CHECK(!makePlayback(src, {bad}, 3840).ok());
  bad = ev(1, 500, 0, 100);
  bad.tx.pitchSemitones = 99.f;
  CHECK(!makePlayback(src, {bad}, 3840).ok());
  auto broken = std::make_shared<SourceData>(*src);
  broken->samples.pop_back();
  CHECK(!makePlayback(broken, {}, 3840).ok());
  FlatEventList many(513, ev(1, 500, 0, 100));
  CHECK(!makePlayback(src, many, 3840).ok());
}

CHOP_TEST(output_is_identical_for_every_host_block_size) {
  auto stereo = makeSource(2, 44100, 30000, [](std::int64_t i, std::int64_t, int c) {
    Rng r(static_cast<std::uint64_t>(i) * 2 + static_cast<std::uint64_t>(c));
    return static_cast<float>(r.uniform01() * 2.0 - 1.0);
  });
  FlatEventList events;
  for (int i = 0; i < 12; ++i) {
    FlatEvent e = ev(static_cast<std::uint64_t>(i + 1), 30000, i * 311 + (i % 3) * 7, 200 + i * 37);
    e.region = {static_cast<std::int64_t>(i * 1000), static_cast<std::int64_t>(i * 1000 + 9000)};
    e.tx.reverse = i % 3 == 0;
    e.tx.pitchSemitones = static_cast<float>((i % 5) - 2);
    e.tx.pan = static_cast<float>(i % 4) / 2.f - 0.75f;
    e.tx.level = 0.4f + 0.05f * static_cast<float>(i);
    e.tx.retrigger = static_cast<std::uint8_t>(1 + i % 3);
    events.push_back(e);
  }
  auto pb = playback(stereo, events, 3840);
  const auto ref = renderWith(pb, 512, 2, 0.25);
  for (int block : {1, 7, 32, 64, 128, 256, 333, 1024, 2048}) {
    const auto got = renderWith(pb, block, 2, 0.25);
    CHECK(got == ref);
  }
  bool nonSilent = false;
  for (float x : ref[0]) nonSilent = nonSilent || std::fabs(x) > 0.01f;
  CHECK(nonSilent);
  CHECK(allFinite(ref[0]) && allFinite(ref[1]));
  // Tempo and rate coverage.
  for (double bpm : {60.0, 97.3, 174.0}) CHECK(allFinite(renderWith(pb, 256, 1, 0.1, bpm)[0]));
  for (double sr : {44100.0, 96000.0, 192000.0}) CHECK(allFinite(renderWith(pb, 256, 1, 0.1, 120.0, sr)[0]));
}

CHOP_TEST(pattern_loops_and_the_tail_rings_out_without_new_events) {
  auto src = makeSource(1, 48000, 24000, dc);
  // A long event near the end of a one-beat loop keeps sounding past the loop point.
  auto pb = playback(src, {ev(1, 24000, 0, 480), ev(2, 24000, 600, 360)}, 960);
  auto out = renderWith(pb, 512, 2, 0.5);
  const auto& L = out[0];
  CHECK_NEAR(L[1000], 1.0, 1e-5);                    // first hit of cycle 1
  CHECK_NEAR(L[24000 + 1000], 1.0, 1e-5);            // cycle 2 starts exactly one loop later (960 ticks = 24000 frames)
  const std::size_t end = 48000;                     // 2 cycles of 960 ticks
  CHECK_EQ(L.size(), end + 24000u);
  for (std::size_t i = end + 2000; i < L.size(); ++i) CHECK_NEAR(L[i], 0.0, 0.0);  // nothing new starts in the tail
}

CHOP_TEST(pass_through_without_a_source_and_when_disabled) {
  Renderer r;
  Config cfg;
  r.prepare(cfg);
  std::vector<float> a(256), b(256), oa(256, 9.f), ob(256, 9.f);
  for (std::size_t i = 0; i < a.size(); ++i) {
    a[i] = std::sin(static_cast<float>(i) * 0.1f);
    b[i] = std::cos(static_cast<float>(i) * 0.1f);
  }
  const float* in[2] = {a.data(), b.data()};
  float* out[2] = {oa.data(), ob.data()};
  TransportBlock tb;
  tb.playing = true;
  RenderParams params;
  r.process(tb, params, in, out, 2, 256);
  CHECK(oa == a && ob == b);  // no source loaded: input passes through unchanged

  auto pb = playback(makeSource(1, 48000, 24000, dc), {ev(1, 24000, 0, 960)});
  r.mailbox().publish(pb);
  params.enabled = false;
  tb.positionValid = true;
  r.process(tb, params, in, out, 2, 256);
  CHECK(oa == a && ob == b);  // disabled: pass-through even with a source and playing transport
  CHECK_EQ(r.activeVoices(), 0);

  // In-place processing (in aliasing out) is supported.
  std::vector<float> inplace = a;
  const float* in1[1] = {inplace.data()};
  float* out1[1] = {inplace.data()};
  params.enabled = true;
  tb.playing = false;
  params.dryMix = 1.f;
  for (int i = 0; i < 40; ++i) {  // let the dry smoother settle
    inplace = a;
    r.process(tb, params, in1, out1, 1, 256);
  }
  inplace = a;
  r.process(tb, params, in1, out1, 1, 256);
  CHECK_NEAR(inplace[200], a[200], 1e-5);
}

CHOP_TEST(pass_through_playback_keeps_the_input_and_allows_audition) {
  Renderer r;
  r.prepare(Config{});
  auto pt = makePassThroughPlayback(makeSource(1, 48000, 24000, dc));
  CHECK(pt.ok() && pt.value()->passThrough && pt.value()->events.empty());
  CHECK(!makePassThroughPlayback(nullptr).ok());
  r.mailbox().publish(pt.value());
  std::vector<float> x(512), oL(512), oR(512);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.25f * std::sin(static_cast<float>(i) * 0.05f);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  r.process(tb, RenderParams{}, in, out, 2, 512);
  CHECK(oL == x && oR == x);  // heard unchanged before any pattern exists
  PreviewRequest req;
  req.region = {0, 4000};
  CHECK(r.requestPreview(req));
  r.process(tb, RenderParams{}, in, out, 2, 512);
  CHECK_NEAR(oL[300], x[300] + 1.0, 1e-5);  // the audition (DC 1.0) is mixed over the passing-through input
  CHECK_EQ(r.activeVoices(), 0);
}

CHOP_TEST(dry_mix_and_output_gain_apply_to_the_whole_output) {
  Renderer r;
  r.prepare(Config{});
  r.mailbox().publish(playback(makeSource(1, 48000, 24000, dc), {ev(1, 24000, 0, 960)}));
  std::vector<float> x(512, 0.25f), oL(512), oR(512);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  TransportBlock stopped;
  RenderParams p;
  p.dryMix = 0.5f;
  p.outputGainDb = -6.0206f;  // x0.5
  for (int i = 0; i < 100; ++i) r.process(stopped, p, in, out, 2, 512);
  CHECK_NEAR(oL[500], 0.25 * 0.5 * 0.5, 1e-4);  // dry * input * output gain, nothing generated while stopped
  p.dryMix = 0.f;
  p.outputGainDb = 0.f;
  for (int i = 0; i < 100; ++i) r.process(stopped, p, in, out, 2, 512);
  CHECK_NEAR(oL[500], 0.0, 1e-6);
}

CHOP_TEST(mono_output_averages_the_channels) {
  Renderer r;
  r.prepare(Config{});
  FlatEvent e = ev(1, 24000, 0, 960);
  e.tx.pan = 1.f;  // right only: L = 0, R = 1
  r.mailbox().publish(playback(makeSource(1, 48000, 24000, dc), {e}));
  std::vector<float> x(2048, 0.f), o(2048);
  const float* in[1] = {x.data()};
  float* out[1] = {o.data()};
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  tb.ppq = 0.0;
  r.process(tb, RenderParams{}, in, out, 1, 2048);
  CHECK_NEAR(o[1000], 0.5, 1e-5);
}

CHOP_TEST(invalid_host_data_never_produces_nan_or_inf) {
  Renderer r;
  Config cfg;
  cfg.maxBlock = 256;
  r.prepare(cfg);
  r.mailbox().publish(playback(makeSource(2, 48000, 24000, dc), {ev(1, 24000, 0, 960), ev(2, 24000, 480, 480)}));
  const float nan = std::nanf("");
  std::vector<float> x(1000, nan), oL(1000), oR(1000);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  const double badBpm[] = {std::nan(""), -5.0, 0.0, 1e12, std::numeric_limits<double>::infinity()};
  for (double bpm : badBpm)
    for (double ppq : {std::nan(""), -100.0, 1e15}) {
      TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.bpm = bpm;
      tb.ppq = ppq;
      RenderParams p;
      p.dryMix = std::nanf("");
      p.outputGainDb = std::nanf("");
      r.process(tb, p, in, out, 2, 1000);  // 1000 > maxBlock exercises the chunk path
      CHECK(allFinite(oL) && allFinite(oR));
    }
  r.process(TransportBlock{}, RenderParams{}, in, out, 3, 100);  // unsupported channel count: ignored
  r.process(TransportBlock{}, RenderParams{}, in, out, 2, 0);
  r.process(TransportBlock{}, RenderParams{}, nullptr, out, 2, 100);  // missing input is treated as silence
  CHECK(allFinite(oL));
}

CHOP_TEST(voice_pool_is_bounded_and_nothing_gets_stuck) {
  Renderer r;
  r.prepare(Config{});
  FlatEventList events;
  for (std::uint64_t i = 1; i <= 14; ++i) events.push_back(ev(i, 24000, 0, 960));
  r.mailbox().publish(playback(makeSource(1, 48000, 24000, dc), events));
  std::vector<float> x(512, 0.f), oL(512), oR(512);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  tb.ppq = 0.0;
  r.process(tb, RenderParams{}, in, out, 2, 512);
  CHECK(r.activeVoices() >= 1 && r.activeVoices() <= 8);  // stolen voices have already faded out
  CHECK(allFinite(oL));
  // Stop: everything releases within the fade time.
  tb.playing = false;
  r.process(tb, RenderParams{}, in, out, 2, 512);
  r.process(tb, RenderParams{}, in, out, 2, 512);
  CHECK_EQ(r.activeVoices(), 0);
  CHECK_NEAR(oL[511], 0.0, 0.0);
}

CHOP_TEST(seek_and_loop_wrap_release_voices_and_restart_cleanly) {
  Renderer r;
  r.prepare(Config{});
  r.mailbox().publish(playback(makeSource(1, 48000, 24000, dc), {ev(1, 24000, 0, 960), ev(2, 24000, 1920, 960)}, 3840));
  std::vector<float> x(512, 0.f), oL(512), oR(512);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  tb.ppq = 0.0;
  r.process(tb, RenderParams{}, in, out, 2, 512);
  CHECK(r.activeVoices() == 1);
  // Seek to an empty region: the sounding voice fades out and nothing stale replays.
  tb.ppq = 3.9;
  r.process(tb, RenderParams{}, in, out, 2, 512);
  r.process(TransportBlock{tb.playing, true, 3.9 + 512 * 120.0 / (60.0 * 48000.0), 120.0, true}, RenderParams{}, in, out, 2, 512);
  CHECK_EQ(r.activeVoices(), 0);
  // Seek exactly onto the second event: it starts on frame 0 of that block.
  tb.ppq = 2.0;
  r.process(tb, RenderParams{}, in, out, 2, 512);
  CHECK_NEAR(oL[0], 0.0, 0.0);
  CHECK(oL[40] > 0.5f);
  CHECK_EQ(r.activeVoices(), 1);
}

CHOP_TEST(preview_plays_without_the_transport_and_never_touches_pattern_voices) {
  Renderer r;
  r.prepare(Config{});
  PreviewRequest req;
  req.region = {0, 4000};
  CHECK(r.requestPreview(req));  // queued before any source exists: ignored once drained
  std::vector<float> x(512, 0.f), oL(512), oR(512);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  r.process(TransportBlock{}, RenderParams{}, in, out, 2, 512);
  r.mailbox().publish(playback(makeSource(1, 48000, 24000, dc), {}));
  CHECK(r.requestPreview(req));
  r.process(TransportBlock{}, RenderParams{}, in, out, 2, 512);  // transport stopped
  CHECK_NEAR(oL[300], 1.0, 1e-5);
  CHECK_EQ(r.activeVoices(), 0);  // the pattern voice pool is untouched
  PreviewRequest bad;
  bad.region = {0, 99999999};
  CHECK(r.requestPreview(bad));
  r.process(TransportBlock{}, RenderParams{}, in, out, 2, 512);
  CHECK(allFinite(oL));
  int accepted = 0;
  for (int i = 0; i < 40; ++i) accepted += r.requestPreview(req) ? 1 : 0;
  CHECK_EQ(accepted, 16);  // bounded queue reports when it is full
}

CHOP_TEST(process_performs_no_heap_allocation) {
  Renderer r;
  Config cfg;
  r.prepare(cfg);
  FlatEventList events;
  for (std::uint64_t i = 1; i <= 64; ++i) {
    FlatEvent e = ev(i, 20000, static_cast<Ticks>(i * 50), 300);
    e.tx.reverse = (i % 2) == 0;
    e.tx.pitchSemitones = static_cast<float>(i % 7) - 3.f;
    events.push_back(e);
  }
  r.mailbox().publish(playback(makeSource(2, 44100, 24000, ramp), events));
  std::vector<float> x(512, 0.1f), oL(512), oR(512);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  RenderParams p;
  p.dryMix = 0.3f;
  r.process(tb, p, in, out, 2, 512);  // warm-up: first acquire
  PreviewRequest pr;
  pr.region = {0, 5000};
  gAllocs = 0;
  gCountAllocs = true;
  for (int i = 0; i < 400; ++i) {
    tb.ppq = i * 512 * 120.0 / (60.0 * 48000.0);
    if (i % 50 == 0) r.requestPreview(pr);
    if (i == 200) tb.ppq += 5.0;  // a seek
    r.process(tb, p, in, out, 2, 512);
  }
  gCountAllocs = false;
  CHECK_EQ(gAllocs.load(), 0);
}

CHOP_TEST(mailbox_hands_off_immutable_data_across_threads_without_freeing_live_objects) {
  Mailbox<std::vector<int>> box;
  CHECK(box.acquire() == nullptr);
  std::atomic<bool> done{false};
  std::atomic<long> torn{0};
  std::atomic<long> reads{0};
  std::thread reader([&] {
    while (!done.load()) {
      const std::vector<int>* v = box.acquire();
      if (v) {
        const int first = (*v)[0];
        for (int x : *v)
          if (x != first) torn.fetch_add(1);
        reads.fetch_add(1);
      }
    }
    box.acquire();
  });
  for (int k = 1; k <= 3000; ++k) {
    box.publish(std::make_shared<const std::vector<int>>(static_cast<std::size_t>(64 + k % 64), k));
    // On a loaded machine the reader thread may not be scheduled until the loop is over; let it get its first
    // reads while objects are still being replaced, so the test always exercises concurrent hand-off.
    if (k == 1)
      for (int spin = 0; spin < 5000 && reads.load() == 0; ++spin) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  done = true;
  reader.join();
  CHECK_EQ(torn.load(), 0);
  CHECK(reads.load() > 0);
  const std::vector<int>* last = box.acquire();
  CHECK(last && (*last)[0] == 3000);
  box.collect();
  CHECK(box.retainedForTesting() <= 2);  // everything older than the acknowledged object has been freed
}

CHOP_TEST(first_block_starts_at_the_target_gain_and_exact_render_lengths_do_not_grow) {
  // Output gain: the first block after prepare() must already be at the target, not ramping down from unity.
  Renderer r;
  Config cfg;
  cfg.sampleRate = 48000.0;
  cfg.maxBlock = 512;
  r.prepare(cfg);
  r.mailbox().publish(playback(makeSource(1, 48000, 48000, dc), {ev(1, 24000, 0, 960)}));
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  tb.ppq = 0.0;
  std::vector<float> inL(512, 0.f), inR(512, 0.f), outL(512, 0.f), outR(512, 0.f);
  const float* in[2] = {inL.data(), inR.data()};
  float* out[2] = {outL.data(), outR.data()};
  RenderParams p;
  p.outputGainDb = -12.f;
  r.process(tb, p, in, out, 2, 512);
  float loud = 0.f;
  for (int i = 100; i < 400; ++i) loud = std::max(loud, std::fabs(outL[static_cast<std::size_t>(i)]));
  CHECK(loud > 0.2f && loud < 0.3f);  // -12 dB is 0.251; before the fix it started near 1.0

  // 3840 ticks at 225 BPM and 44.1 kHz is exactly 47040 frames; floating-point must not add one.
  auto pb = playback(makeSource(1, 44100, 44100, dc), {ev(1, 20000, 0, 480)});
  CHECK_EQ(renderWith(pb, 512, 1, 0.0, 225.0, 44100.0)[0].size(), 47040u);
}

CHOP_TEST(retrigger_sub_hits_never_start_past_the_loop_end_and_tiny_regions_are_audible) {
  FlatEvent late = ev(1, 20000, 3800, 100);  // runs past the end of a 3840-tick loop
  late.tx.retrigger = 4;
  auto pb = playback(makeSource(1, 48000, 48000, dc), {late});
  CHECK(pb != nullptr);
  if (pb)
    for (const FlatEvent& e : pb->events) CHECK(e.start >= 0 && e.start < pb->lengthTicks);

  // A 64-frame region must reach nearly full level: the end fade covers at most half the region (it peaked at 0.48 before).
  auto tiny = playback(makeSource(1, 48000, 48000, dc), {ev(1, 64, 0, 960)});
  const auto out = renderWith(tiny);
  float peakv = 0.f;
  for (float x : out[0]) peakv = std::max(peakv, std::fabs(x));
  CHECK(peakv > 0.8f);
}

CHOP_TEST(offline_render_is_deterministic_and_validates_settings) {
  auto pb = playback(makeSource(1, 48000, 24000, ramp), {ev(1, 24000, 0, 960), ev(2, 12000, 1000, 480)});
  CHECK(renderWith(pb) == renderWith(pb));
  OfflineSettings bad;
  bad.bpm = 5.0;
  CHECK(!renderOffline(pb, bad).ok());
  bad = OfflineSettings{};
  bad.blockSize = 0;
  CHECK(!renderOffline(pb, bad).ok());
  bad = OfflineSettings{};
  bad.cycles = 1000;
  CHECK(!renderOffline(pb, bad).ok());
  CHECK(!renderOffline(nullptr, OfflineSettings{}).ok());
}

// ---- event effects: filter, tape glide, crunch ----
namespace {
double rms(const std::vector<float>& v, std::size_t a, std::size_t b) {
  double s = 0;
  for (std::size_t i = a; i < b; ++i) s += static_cast<double>(v[i]) * v[i];
  return std::sqrt(s / static_cast<double>(b - a));
}
float sine200(std::int64_t i, std::int64_t, int) { return static_cast<float>(std::sin(2.0 * 3.14159265358979 * 200.0 * static_cast<double>(i) / 48000.0)); }
float sine8k(std::int64_t i, std::int64_t, int) { return static_cast<float>(std::sin(2.0 * 3.14159265358979 * 8000.0 * static_cast<double>(i) / 48000.0)); }
std::vector<float> renderFx(SourcePtr src, const EventFx& fx) {
  FlatEvent e = ev(1, 24000, 0, 960);
  e.fx = fx;
  return renderWith(playback(std::move(src), {e}))[0];
}
}  // namespace

CHOP_TEST(filter_attenuates_the_expected_band) {
  EventFx lp;
  lp.filter = FilterType::LowPass;
  lp.cutoff = 0.4f;  // about 316 Hz
  EventFx hp = lp;
  hp.filter = FilterType::HighPass;
  const EventFx off;
  auto lowSrc = makeSource(1, 48000, 24000, sine200);
  auto highSrc = makeSource(1, 48000, 24000, sine8k);
  const double lowDry = rms(renderFx(lowSrc, off), 2000, 20000);
  const double highDry = rms(renderFx(highSrc, off), 2000, 20000);
  CHECK(rms(renderFx(highSrc, lp), 2000, 20000) < 0.02 * highDry);
  CHECK(rms(renderFx(lowSrc, lp), 2000, 20000) > 0.6 * lowDry);
  CHECK(rms(renderFx(lowSrc, hp), 2000, 20000) < 0.6 * lowDry);
  CHECK(rms(renderFx(highSrc, hp), 2000, 20000) > 0.9 * highDry);
  EventFx res = lp;
  res.cutoff = 0.5f;
  res.resonance = 1.f;
  auto edge = makeSource(1, 48000, 24000, [](std::int64_t i, std::int64_t, int) {
    return static_cast<float>(std::sin(2.0 * 3.14159265358979 * 632.0 * static_cast<double>(i) / 48000.0));
  });
  EventFx plain = lp;
  plain.cutoff = 0.5f;
  CHECK(rms(renderFx(edge, res), 2000, 20000) > 2.0 * rms(renderFx(edge, plain), 2000, 20000));  // resonance peak
  EventFx top = lp;
  top.cutoff = 1.f;  // clamped below Nyquist, must stay stable
  CHECK(allFinite(renderFx(highSrc, top)));
  CHECK(rms(renderFx(makeSource(1, 48000, 24000, [](std::int64_t, std::int64_t, int) { return 0.f; }), res), 0, 24000) == 0.0);
}

CHOP_TEST(tape_glide_changes_the_read_rate_across_the_hit) {
  auto src = makeSource(1, 48000, 24000, ramp);
  const auto base = renderFx(src, EventFx{});
  EventFx down;
  down.glideSemitones = -12.f;
  EventFx up;
  up.glideSemitones = 12.f;
  const auto d = renderFx(src, down);
  const auto u = renderFx(src, up);
  CHECK(d[12000] < base[12000] - 0.05f);  // falls behind: tape stop
  CHECK(u[12000] > base[12000] + 0.05f);  // runs ahead: rise
  CHECK(d[100] > 0.f && std::fabs(d[100] - base[100]) < 1e-3f);  // the ramp starts at the normal rate
  EventFx zero;
  zero.glideSemitones = 0.f;
  CHECK(renderFx(src, zero) == base);
}

CHOP_TEST(crunch_reduces_levels_and_holds_samples) {
  auto src = makeSource(1, 48000, 24000, ramp);
  EventFx c;
  c.crush = 1.f;  // 4 bits, hold 16
  const auto out = renderFx(src, c);
  std::vector<float> seen;
  for (std::size_t i = 2000; i < 20000; ++i)
    if (std::find(seen.begin(), seen.end(), out[i]) == seen.end()) seen.push_back(out[i]);
  CHECK(seen.size() <= 9);  // ramp 0..1 quantized to 1/8 steps
  CHECK(out[3200] == out[3215]);
  EventFx none;
  none.crush = 0.f;
  CHECK(renderFx(src, none) == renderFx(src, EventFx{}));
  EventFx mild;
  mild.crush = 0.1f;
  const auto m = renderFx(src, mild);
  CHECK(std::fabs(m[10000] - out[10000]) <= 1.f && allFinite(m));
}

CHOP_TEST(effects_are_block_size_independent_and_validated) {
  auto stereo = makeSource(2, 44100, 30000, [](std::int64_t i, std::int64_t, int c) {
    Rng r(static_cast<std::uint64_t>(i) * 2 + static_cast<std::uint64_t>(c));
    return static_cast<float>(r.uniform01() * 2.0 - 1.0);
  });
  FlatEventList events;
  for (int i = 0; i < 9; ++i) {
    FlatEvent e = ev(static_cast<std::uint64_t>(i + 1), 30000, i * 400, 360);
    e.fx.filter = static_cast<FilterType>(1 + i % 2);
    e.fx.cutoff = 0.2f + 0.08f * static_cast<float>(i);
    e.fx.resonance = 0.1f * static_cast<float>(i % 5);
    e.fx.glideSemitones = static_cast<float>(i - 4) * 3.f;
    e.fx.crush = (i % 3) * 0.4f;
    events.push_back(e);
  }
  auto pb = playback(stereo, events, 3840);
  const auto ref = renderWith(pb, 512, 2, 0.2);
  for (int block : {1, 7, 64, 333, 2048}) CHECK(renderWith(pb, block, 2, 0.2) == ref);
  CHECK(allFinite(ref[0]) && allFinite(ref[1]));
  for (const auto& bad : {EventFx{static_cast<FilterType>(3), 0.5f, 0.f, 0.f, 0.f}, EventFx{FilterType::LowPass, 2.f, 0.f, 0.f, 0.f},
                          EventFx{FilterType::Off, 0.5f, 0.f, 99.f, 0.f}, EventFx{FilterType::Off, 0.5f, 0.f, 0.f, -1.f}}) {
    FlatEvent e = ev(1, 24000, 0, 960);
    e.fx = bad;
    CHECK(!makePlayback(makeSource(1, 48000, 24000, dc), {e}, 3840).ok());
  }
}

CHOP_TEST(an_exhausted_voice_pool_never_hard_cuts_a_sounding_voice) {
  auto src = makeSource(1, 48000, 48000, [](std::int64_t, std::int64_t, int) { return 0.5f; });
  FlatEventList events;
  for (int i = 0; i < 60; ++i) events.push_back(ev(static_cast<std::uint64_t>(i + 1), 48000, i, 3000));  // 1 tick apart
  auto pb = playback(src, events, 3840);
  Renderer r;
  Config cfg;
  cfg.maxVoices = 2;
  r.prepare(cfg);
  r.mailbox().publish(pb);
  TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  tb.bpm = 120;
  std::vector<float> out(4096);
  float* o[2] = {out.data(), out.data()};
  std::vector<float> in(4096, 0.f);
  const float* i2[2] = {in.data(), in.data()};
  tb.ppq = 0.0;
  r.process(tb, RenderParams{}, i2, o, 1, 4096);
  float biggest = 0.f;
  for (std::size_t k = 1; k < out.size(); ++k) biggest = std::max(biggest, std::fabs(out[k] - out[k - 1]));
  CHECK(biggest < 0.2f);  // every steal fades; no full-level step
}

namespace {
struct ArmRun {
  Renderer r;
  std::vector<float> left;
  double ppq = 0.0;
  ArmRun() { r.prepare(Config{}); }
  void run(std::int64_t frames, int block) {
    std::vector<float> l(static_cast<std::size_t>(block)), rr(static_cast<std::size_t>(block)), in(static_cast<std::size_t>(block), 0.f);
    for (std::int64_t done = 0; done < frames; done += block) {
      const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
      TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.ppq = ppq;
      tb.bpm = 120.0;
      const float* inp[2] = {in.data(), in.data()};
      float* outp[2] = {l.data(), rr.data()};
      r.process(tb, RenderParams{}, inp, outp, 2, n);
      left.insert(left.end(), l.begin(), l.begin() + n);
      ppq += n * 120.0 / (60.0 * 48000.0);
    }
  }
};
std::shared_ptr<const Playback> loopOf(const SourcePtr& src, float level) {
  FlatEvent e = ev(1, 24000, 0, 480);
  e.tx.level = level;
  return playback(src, {e}, 3840);
}
}  // namespace

CHOP_TEST(armed_switch_lands_on_the_loop_boundary_frame_for_any_block_size) {
  for (int block : {64, 333, 512, 1000}) {
    auto src = makeSource(1, 48000, 24000, dc);
    ArmRun t;
    t.r.mailbox().publish(loopOf(src, 1.0f));
    t.run(48000, block);  // a quarter of the way into the loop, A is playing
    t.r.armMailbox().publish(std::make_shared<const ArmedSwitch>(ArmedSwitch{loopOf(src, 0.5f), 7}));
    t.run(2 * 96000 - 48000, block);
    CHECK_EQ(t.r.appliedSwitchToken(), 7u);
    // Cycle 0 (A, level 1): plateau; cycle 1 (B, level 0.5) starts exactly at frame 96000 with the 32-frame attack.
    CHECK_NEAR(t.left[5000], 1.0, 1e-6);
    CHECK_NEAR(t.left[96000 + 16], 0.25, 1e-6);
    CHECK_NEAR(t.left[96000 + 5000], 0.5, 1e-6);
    CHECK_NEAR(t.left[96000 - 1], 0.0, 1e-6);  // nothing sounded at the end of cycle 0
    CHECK_NEAR(t.left[96000 + 12000 + 100], 0.0, 1e-6);
  }
}

CHOP_TEST(armed_switch_can_be_cancelled_and_foreign_sources_are_not_switched) {
  auto src = makeSource(1, 48000, 24000, dc);
  {
    ArmRun t;
    t.r.mailbox().publish(loopOf(src, 1.0f));
    t.run(1000, 512);
    t.r.armMailbox().publish(std::make_shared<const ArmedSwitch>(ArmedSwitch{loopOf(src, 0.5f), 1}));
    t.run(512, 512);  // armed and waiting for the boundary
    t.r.armMailbox().publish(std::make_shared<const ArmedSwitch>(ArmedSwitch{nullptr, 2}));  // cancel
    t.run(2 * 96000, 512);
    CHECK_EQ(t.r.appliedSwitchToken(), 0u);
    CHECK_NEAR(t.left[96000 + 5000], 1.0, 1e-6);  // still A
  }
  {
    ArmRun t;
    t.r.mailbox().publish(loopOf(src, 1.0f));
    t.run(100, 100);
    t.r.armMailbox().publish(std::make_shared<const ArmedSwitch>(ArmedSwitch{loopOf(makeSource(1, 48000, 24000, dc), 0.5f), 3}));
    t.run(2 * 96000, 512);
    CHECK_EQ(t.r.appliedSwitchToken(), 0u);
    CHECK_NEAR(t.left[96000 + 5000], 1.0, 1e-6);
  }
}

CHOP_TEST(main_mailbox_publish_of_the_armed_playback_after_a_switch_does_not_revert_or_kill_voices) {
  auto src = makeSource(1, 48000, 24000, dc);
  ArmRun t;
  t.r.mailbox().publish(loopOf(src, 1.0f));
  auto b = loopOf(src, 0.5f);
  t.run(1000, 512);
  t.r.armMailbox().publish(std::make_shared<const ArmedSwitch>(ArmedSwitch{b, 9}));
  t.run(96000 + 2000, 512);
  CHECK_EQ(t.r.appliedSwitchToken(), 9u);
  t.r.mailbox().publish(b);  // the message thread now mirrors the switch into the main mailbox
  const std::size_t before = t.left.size();
  t.run(2 * 96000, 512);
  CHECK_NEAR(t.left[96000 + 5000], 0.5, 1e-6);
  CHECK_NEAR(t.left[2 * 96000 + 5000], 0.5, 1e-6);  // B keeps playing in the following cycle
  CHECK(before + 2 * 96000 == t.left.size());
  for (float x : t.left) CHECK(std::isfinite(x));
}

CHOP_TEST(armed_switch_processing_never_allocates) {
  auto src = makeSource(1, 48000, 24000, dc);
  Renderer r;
  r.prepare(Config{});
  r.mailbox().publish(loopOf(src, 1.0f));
  r.armMailbox().publish(std::make_shared<const ArmedSwitch>(ArmedSwitch{loopOf(src, 0.5f), 5}));
  std::vector<float> l(333), rr(333), in(333, 0.f);
  const float* inp[2] = {in.data(), in.data()};
  float* outp[2] = {l.data(), rr.data()};
  double ppq = 0.0;
  gAllocs = 0;
  gCountAllocs = true;
  for (int i = 0; i < 700; ++i) {  // crosses the loop boundary at frame 96000 inside a block
    TransportBlock tb;
    tb.playing = tb.positionValid = true;
    tb.ppq = ppq;
    r.process(tb, RenderParams{}, inp, outp, 2, 333);
    ppq += 333 * 120.0 / (60.0 * 48000.0);
  }
  gCountAllocs = false;
  CHECK_EQ(gAllocs.load(), 0);
  CHECK_EQ(r.appliedSwitchToken(), 5u);
}

// ---------------------------------------------------------------------------------------------
// live pads
// ---------------------------------------------------------------------------------------------
#include <chopfractal/audio_renderer/spsc_queue.hpp>
#include <initializer_list>

namespace {

std::shared_ptr<const PadBank> bankOf(std::initializer_list<std::pair<int, PadSlot>> slots) {
  auto b = std::make_shared<PadBank>();
  for (const auto& s : slots) b->slots[static_cast<std::size_t>(s.first)] = s.second;
  return b;
}
PadSlot slot(std::int64_t end, std::uint8_t group = 0) {
  PadSlot s;
  s.region = {0, end};
  s.chokeGroup = group;
  return s;
}
PadEvent noteOn(int frame, int note, int vel = 127) {
  PadEvent e;
  e.frame = frame;
  e.type = PadEvent::Type::NoteOn;
  e.note = static_cast<std::uint8_t>(note);
  e.velocity = static_cast<std::uint8_t>(vel);
  return e;
}
PadEvent noteOff(int frame, int note) {
  PadEvent e = noteOn(frame, note);
  e.type = PadEvent::Type::NoteOff;
  return e;
}

struct PadRig {
  Renderer r;
  double ppq = 0.0;
  bool playing = false;
  std::vector<float> out;
  PadRig(std::shared_ptr<const Playback> pb, std::shared_ptr<const PadBank> bank) {
    r.prepare(Config{});
    r.mailbox().publish(std::move(pb));
    r.padMailbox().publish(std::move(bank));
  }
  // Renders `frames` frames from absolute frame `done0`, feeding events by absolute frame; host blocks of `block`.
  void run(std::int64_t frames, int block, const std::vector<PadEvent>& absolute, PadParams params = {}) {
    std::vector<float> l(static_cast<std::size_t>(block)), rr(static_cast<std::size_t>(block)), in(static_cast<std::size_t>(block), 0.f);
    for (std::int64_t done = 0; done < frames; done += block) {
      const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
      std::vector<PadEvent> local;
      for (PadEvent e : absolute)
        if (e.frame >= base + done && e.frame < base + done + n) {
          e.frame -= static_cast<std::int32_t>(base + done);
          local.push_back(e);
        }
      TransportBlock tb;
      tb.playing = playing;
      tb.positionValid = true;
      tb.ppq = ppq;
      LiveInput live;
      live.pads = local.data();
      live.numPads = static_cast<int>(local.size());
      live.padParams = params;
      const float* inp[2] = {in.data(), in.data()};
      float* outp[2] = {l.data(), rr.data()};
      r.process(tb, RenderParams{}, live, inp, outp, 2, n);
      out.insert(out.end(), l.begin(), l.begin() + n);
      if (playing) ppq += n * 120.0 / (60.0 * 48000.0);
    }
    base += frames;
  }
  std::int64_t base = 0;
};

}  // namespace

CHOP_TEST(pad_note_on_starts_a_voice_on_the_exact_frame_for_any_block_size) {
  auto src = makeSource(1, 48000, 24000, dc);
  for (int block : {64, 333, 512, 2048}) {
    PadRig t(playback(src, {}), bankOf({{36, slot(24000)}}));
    t.run(2000, block, {noteOn(100, 36)});
    for (int i = 0; i <= 100; ++i) CHECK_NEAR(t.out[static_cast<std::size_t>(i)], 0.0, 0.0);
    std::size_t first = 0;
    while (first < t.out.size() && t.out[first] == 0.f) ++first;
    CHECK(first == 101 || first == 102);
    CHECK_NEAR(t.out[100 + 40], 1.0, 1e-6);  // past the 32-frame attack, full velocity
    CHECK_EQ(t.r.activePadVoices(), 1);
  }
}

CHOP_TEST(velocity_curves_are_exact) {
  CHECK_EQ(velocityGain(VelocityCurve::Linear, 127), 1.f);
  CHECK_NEAR(velocityGain(VelocityCurve::Linear, 64), 64.0 / 127.0, 1e-6);
  CHECK_NEAR(velocityGain(VelocityCurve::Soft, 1), std::sqrt(1.0 / 127.0), 1e-6);
  CHECK_NEAR(velocityGain(VelocityCurve::Hard, 64), (64.0 / 127.0) * (64.0 / 127.0), 1e-6);
  CHECK_EQ(velocityGain(VelocityCurve::Fixed, 1), 1.f);
  CHECK_EQ(velocityGain(VelocityCurve::Soft, 127), 1.f);
  CHECK_EQ(velocityGain(VelocityCurve::Linear, 500), 1.f);  // clamped
  CHECK_NEAR(velocityGain(VelocityCurve::Linear, -5), 1.0 / 127.0, 1e-6);
  auto src = makeSource(1, 48000, 24000, dc);
  PadRig t(playback(src, {}), bankOf({{36, slot(24000)}}));
  PadParams hard;
  hard.curve = VelocityCurve::Hard;
  t.run(1000, 512, {noteOn(0, 36, 64)}, hard);
  CHECK_NEAR(t.out[500], (64.0 / 127.0) * (64.0 / 127.0), 1e-5);
}

CHOP_TEST(mono_gate_and_choke_release_pad_voices_with_a_fade) {
  auto src = makeSource(1, 48000, 24000, dc);
  {  // Poly layers, note off is ignored
    PadRig t(playback(src, {}), bankOf({{36, slot(24000)}, {37, slot(24000)}}));
    t.run(2000, 512, {noteOn(0, 36), noteOn(10, 37), noteOff(500, 36)});
    CHECK_EQ(t.r.activePadVoices(), 2);
    CHECK_NEAR(t.out[1500], 2.0, 1e-5);  // two voices summed
  }
  {  // Mono: the second pad releases the first
    PadRig t(playback(src, {}), bankOf({{36, slot(24000)}, {37, slot(24000)}}));
    PadParams mono;
    mono.mode = PadMode::Mono;
    t.run(2000, 512, {noteOn(0, 36), noteOn(10, 37)}, mono);
    CHECK_EQ(t.r.activePadVoices(), 1);
    CHECK_NEAR(t.out[1500], 1.0, 1e-5);
  }
  {  // Gate: note off fades the voice out within 64 frames
    PadRig t(playback(src, {}), bankOf({{36, slot(24000)}}));
    PadParams gate;
    gate.mode = PadMode::Gate;
    t.run(2000, 512, {noteOn(0, 36), noteOff(1000, 36)}, gate);
    CHECK_NEAR(t.out[999], 1.0, 1e-5);
    CHECK_NEAR(t.out[1000 + 70], 0.0, 1e-6);
    CHECK_EQ(t.r.activePadVoices(), 0);
  }
  {  // choke group releases only the same group
    PadRig t(playback(src, {}), bankOf({{36, slot(24000, 1)}, {37, slot(24000, 1)}, {38, slot(24000, 2)}}));
    t.run(2000, 512, {noteOn(0, 36), noteOn(0, 38), noteOn(500, 37)});
    CHECK_EQ(t.r.activePadVoices(), 2);  // 36 choked by 37; 38 (group 2) untouched
  }
  {  // all notes off
    PadRig t(playback(src, {}), bankOf({{36, slot(24000)}, {37, slot(24000)}}));
    PadEvent all;
    all.frame = 800;
    all.type = PadEvent::Type::AllNotesOff;
    t.run(2000, 512, {noteOn(0, 36), noteOn(0, 37), all});
    CHECK_EQ(t.r.activePadVoices(), 0);
  }
}

CHOP_TEST(pads_work_with_stopped_transport_pass_through_and_survive_transport_edges) {
  auto src = makeSource(1, 48000, 24000, dc);
  {  // source loaded, no pattern yet
    auto pt = makePassThroughPlayback(src);
    CHECK(pt.ok());
    PadRig t(pt.value(), bankOf({{36, slot(24000)}}));
    t.run(1000, 512, {noteOn(50, 36)});
    CHECK_NEAR(t.out[500], 1.0, 1e-5);
  }
  {  // pad voices keep sounding when the transport stops
    PadRig t(playback(src, {}), bankOf({{36, slot(24000)}}));
    t.playing = true;
    t.run(1000, 512, {noteOn(0, 36)});
    t.playing = false;
    t.run(1000, 512, {});
    CHECK_NEAR(t.out[1500], 1.0, 1e-5);
    CHECK_EQ(t.r.activePadVoices(), 1);
  }
}

CHOP_TEST(unmapped_notes_and_oversize_host_blocks) {
  auto src = makeSource(1, 48000, 24000, dc);
  PadRig t(playback(src, {}), bankOf({{36, slot(24000)}, {40, slot(99999)}}));  // 40 reads outside the source
  t.run(512, 512, {noteOn(10, 50), noteOn(20, 40), noteOn(30, 200)});
  CHECK_EQ(t.r.activePadVoices(), 0);
  CHECK_EQ(t.r.padUnmapped(), 3u);
  // A host block larger than the prepared maximum is chunked; the event keeps its frame.
  PadRig big(playback(src, {}), bankOf({{36, slot(24000)}}));
  big.run(5000, 5000, {noteOn(3000, 36)});
  CHECK_NEAR(big.out[3000], 0.0, 0.0);
  CHECK_NEAR(big.out[3000 + 40], 1.0, 1e-5);
}

CHOP_TEST(pad_events_are_block_size_independent_and_absent_input_is_bit_identical) {
  auto src = makeSource(1, 48000, 24000, ramp);
  const std::vector<PadEvent> events = {noteOn(70, 36), noteOn(900, 37, 80), noteOff(1500, 36), noteOn(2100, 36, 30)};
  std::vector<float> ref;
  for (int block : {64, 333, 512, 2048}) {
    PadRig t(playback(src, {ev(1, 24000, 480, 240)}), bankOf({{36, slot(12000, 1)}, {37, slot(8000, 1)}}));
    t.playing = true;
    PadParams p;
    p.mode = PadMode::Gate;
    t.run(6000, block, events, p);
    if (ref.empty()) ref = t.out;
    else CHECK(ref == t.out);
  }
  float pk = 0.f;
  for (float x : ref) pk = std::max(pk, std::fabs(x));
  CHECK(pk > 0.02f);
  // no pad events: identical to the six-argument process
  PadRig a(playback(src, {ev(1, 24000, 0, 960)}), bankOf({}));
  a.playing = true;
  a.run(4000, 512, {});
  Renderer plain;
  plain.prepare(Config{});
  plain.mailbox().publish(playback(src, {ev(1, 24000, 0, 960)}));
  std::vector<float> l(512), rr(512), in(512, 0.f), outAll;
  double ppq = 0.0;
  for (int done = 0; done < 4000; done += 512) {
    const int n = std::min(512, 4000 - done);
    TransportBlock tb;
    tb.playing = tb.positionValid = true;
    tb.ppq = ppq;
    const float* inp[2] = {in.data(), in.data()};
    float* outp[2] = {l.data(), rr.data()};
    plain.process(tb, RenderParams{}, inp, outp, 2, n);
    outAll.insert(outAll.end(), l.begin(), l.begin() + n);
    ppq += n * 120.0 / (60.0 * 48000.0);
  }
  CHECK(outAll == a.out);
}

CHOP_TEST(pad_path_performs_no_heap_allocation_and_the_spsc_queue_keeps_order) {
  auto src = makeSource(1, 48000, 24000, dc);
  Renderer r;
  r.prepare(Config{});
  r.mailbox().publish(playback(src, {ev(1, 24000, 0, 960)}));
  r.padMailbox().publish(bankOf({{36, slot(24000, 1)}, {37, slot(24000, 1)}}));
  std::vector<float> l(512), rr(512), in(512, 0.f);
  std::array<PadEvent, 4> events = {noteOn(10, 36), noteOn(20, 37), noteOff(300, 36), noteOn(400, 36)};
  const float* inp[2] = {in.data(), in.data()};
  float* outp[2] = {l.data(), rr.data()};
  gAllocs = 0;
  gCountAllocs = true;
  for (int i = 0; i < 100; ++i) {
    TransportBlock tb;
    tb.playing = true;
    LiveInput live;
    live.pads = events.data();
    live.numPads = 4;
    live.padParams.mode = i % 2 ? PadMode::Gate : PadMode::Mono;
    r.process(tb, RenderParams{}, live, inp, outp, 2, 512);
  }
  gCountAllocs = false;
  CHECK_EQ(gAllocs.load(), 0);

  SpscQueue<int, 4> q;
  for (int i = 1; i <= 4; ++i) CHECK(q.push(i));
  CHECK(!q.push(5));  // full: rejected, nothing lost or duplicated
  int v = 0;
  for (int i = 1; i <= 4; ++i) {
    CHECK(q.pop(v));
    CHECK_EQ(v, i);
  }
  CHECK(!q.pop(v) && q.empty());
  CHECK(q.push(9) && q.pop(v) && v == 9);
}

// ---------------------------------------------------------------------------------------------
// multi-output stems
// ---------------------------------------------------------------------------------------------
namespace {

float peak(const std::vector<float>& v) {
  float m = 0.f;
  for (float x : v) m = std::max(m, std::fabs(x));
  return m;
}

struct StemRun {
  std::vector<float> main;
  std::vector<std::vector<float>> aux;  // [bus] left channel
};

std::shared_ptr<const Playback> twoChopPlayback(const SourcePtr& src, std::uint8_t busA, std::uint8_t busB, std::uint8_t busC = 0) {
  FlatEvent a = ev(1, 24000, 0, 960);
  FlatEvent b = ev(2, 24000, 960, 960);
  b.chop = ChopId{2};
  b.tx.level = 0.5f;
  FlatEvent c = ev(3, 24000, 1920, 960);
  c.chop = ChopId{3};
  c.tx.level = 0.25f;
  BusRouting routing;
  routing.chopBus = {{ChopId{1}, busA}, {ChopId{2}, busB}, {ChopId{3}, busC}};
  auto r = makePlayback(src, {a, b, c}, 3840, &routing);
  CHECK(r.ok());
  return r.ok() ? r.value() : nullptr;
}

StemRun runStems(const std::shared_ptr<const Playback>& pb, int auxBuses, int enabledBuses, int block, float dry = 0.f, float gainDb = 0.f, const float* inputLevel = nullptr) {
  Renderer r;
  Config cfg;
  cfg.auxBuses = auxBuses;
  r.prepare(cfg);
  r.mailbox().publish(pb);
  StemRun out;
  out.aux.resize(8);
  std::vector<float> in(static_cast<std::size_t>(block), inputLevel ? *inputLevel : 0.f);
  std::vector<float> l(static_cast<std::size_t>(block)), rr(static_cast<std::size_t>(block));
  std::vector<std::vector<float>> al(8, std::vector<float>(static_cast<std::size_t>(block))), ar(8, std::vector<float>(static_cast<std::size_t>(block)));
  RenderParams params;
  params.dryMix = dry;
  params.outputGainDb = gainDb;
  for (std::int64_t done = 0; done < 96000; done += block) {
    const int n = static_cast<int>(std::min<std::int64_t>(block, 96000 - done));
    TransportBlock tb;
    tb.playing = tb.positionValid = true;
    tb.ppq = static_cast<double>(done) * 120.0 / (60.0 * 48000.0);
    float* auxPairs[kMaxAuxBuses][2] = {};
    AuxOutputs aux;
    aux.count = enabledBuses;
    for (int b = 0; b < enabledBuses; ++b) {
      std::fill(al[static_cast<std::size_t>(b)].begin(), al[static_cast<std::size_t>(b)].end(), 0.7f);  // hosts do not clear aux buffers
      std::fill(ar[static_cast<std::size_t>(b)].begin(), ar[static_cast<std::size_t>(b)].end(), 0.7f);
      auxPairs[b][0] = al[static_cast<std::size_t>(b)].data();
      auxPairs[b][1] = ar[static_cast<std::size_t>(b)].data();
      aux.bus[b] = auxPairs[b];
    }
    const float* inp[2] = {in.data(), in.data()};
    float* outp[2] = {l.data(), rr.data()};
    r.process(tb, params, LiveInput{}, inp, outp, 2, n, aux);
    out.main.insert(out.main.end(), l.begin(), l.begin() + n);
    for (int b = 0; b < enabledBuses; ++b) out.aux[static_cast<std::size_t>(b)].insert(out.aux[static_cast<std::size_t>(b)].end(), al[static_cast<std::size_t>(b)].begin(), al[static_cast<std::size_t>(b)].begin() + n);
  }
  return out;
}

}  // namespace

CHOP_TEST(bus_routing_lookup_and_event_bus_follow_the_sorted_events) {
  BusRouting r;
  r.chopBus = {{ChopId{2}, 3}, {ChopId{9}, 8}};
  r.defaultBus = 1;
  CHECK_EQ(r.busFor(ChopId{2}), 3);
  CHECK_EQ(r.busFor(ChopId{9}), 8);
  CHECK_EQ(r.busFor(ChopId{5}), 1);  // absent -> default
  auto src = makeSource(1, 48000, 24000, dc);
  auto pb = twoChopPlayback(src, 0, 0, 0);
  CHECK(pb->eventBus.empty());  // everything on main keeps the plain form
  pb = twoChopPlayback(src, 2, 0, 5);
  CHECK_EQ(pb->eventBus.size(), pb->events.size());
  for (std::size_t i = 0; i < pb->events.size(); ++i) CHECK_EQ(pb->eventBus[i], pb->events[i].chop.value == 1 ? 2 : (pb->events[i].chop.value == 3 ? 5 : 0));
}

CHOP_TEST(a_routed_chop_appears_only_on_its_bus_and_the_sum_equals_the_unrouted_main) {
  auto src = makeSource(1, 48000, 24000, dc);
  const auto unrouted = runStems(twoChopPlayback(src, 0, 0, 0), 0, 0, 512);
  for (int block : {1, 7, 64, 333, 2048}) {
    const auto routed = runStems(twoChopPlayback(src, 1, 0, 2), 2, 2, block);
    // chop 1 (level 1.0) is on aux 1, chop 3 (0.25) on aux 2, chop 2 (0.5) on main
    CHECK_NEAR(routed.aux[0][5000], 1.0, 1e-5);
    CHECK_NEAR(routed.aux[0][24000 + 5000], 0.0, 1e-6);  // chop 2 is not on aux 1
    CHECK_NEAR(routed.aux[1][2 * 24000 + 5000], 0.25, 1e-5);
    CHECK_NEAR(routed.main[5000], 0.0, 1e-6);
    CHECK_NEAR(routed.main[24000 + 5000], 0.5, 1e-5);
    float worst = 0.f;
    for (std::size_t i = 0; i < unrouted.main.size(); ++i)
      worst = std::max(worst, std::fabs(routed.main[i] + routed.aux[0][i] + routed.aux[1][i] - unrouted.main[i]));
    CHECK(worst < 1e-6f);
    if (block != 512) {
      const auto ref = runStems(twoChopPlayback(src, 1, 0, 2), 2, 2, 512);
      CHECK(routed.aux[0] == ref.aux[0] && routed.aux[1] == ref.aux[1] && routed.main == ref.main);  // block-size independent
    }
  }
}

CHOP_TEST(disabled_aux_folds_into_main_bit_exactly_and_default_is_unchanged) {
  auto src = makeSource(1, 48000, 24000, dc);
  const auto plain = runStems(twoChopPlayback(src, 0, 0, 0), 0, 0, 512);
  const auto foldedNoAux = runStems(twoChopPlayback(src, 3, 0, 1), 0, 0, 512);   // host enabled nothing
  const auto foldedOne = runStems(twoChopPlayback(src, 3, 0, 0), 1, 1, 512);    // bus 3 is beyond the one enabled aux
  CHECK(foldedNoAux.main == plain.main);
  CHECK(foldedOne.main == plain.main);
  CHECK(peak(foldedOne.aux[0]) < 1e-6f);  // the enabled aux bus stays silent
}

CHOP_TEST(dry_mix_reaches_main_only_output_gain_reaches_every_bus_and_silent_aux_is_zeroed) {
  auto src = makeSource(1, 48000, 24000, dc);
  const float level = 0.3f;
  const auto dryRun = runStems(twoChopPlayback(src, 1, 0, 0), 1, 1, 512, 1.0f, 0.f, &level);
  CHECK_NEAR(dryRun.main[24000 + 5000], 0.5 + 0.3, 1e-4);   // chop 2 plus the dry input
  CHECK_NEAR(dryRun.aux[0][5000], 1.0, 1e-4);                // the stem never contains the input
  CHECK_NEAR(dryRun.aux[0][60000], 0.0, 1e-6);               // pre-filled with 0.7, rewritten with silence
  const auto quiet = runStems(twoChopPlayback(src, 1, 0, 0), 1, 1, 512, 0.f, -6.0206f);
  CHECK_NEAR(quiet.aux[0][5000], 0.5, 2e-3);
  CHECK_NEAR(quiet.main[24000 + 5000], 0.25, 2e-3);
  // disabled effect: everything silent but still written
  Renderer r;
  Config cfg;
  cfg.auxBuses = 1;
  r.prepare(cfg);
  r.mailbox().publish(twoChopPlayback(src, 1, 0, 0));
  std::vector<float> l(512), rr(512), in(512, 0.f), al(512, 0.7f), ar(512, 0.7f);
  float* pair[2] = {al.data(), ar.data()};
  AuxOutputs aux;
  aux.count = 1;
  aux.bus[0] = pair;
  RenderParams off;
  off.enabled = false;
  TransportBlock tb;
  tb.playing = tb.positionValid = true;
  const float* inp[2] = {in.data(), in.data()};
  float* outp[2] = {l.data(), rr.data()};
  r.process(tb, off, LiveInput{}, inp, outp, 2, 512, aux);
  CHECK(peak(al) == 0.f && peak(ar) == 0.f);
}

CHOP_TEST(stem_processing_never_allocates_and_offline_stems_sum_to_the_main_render) {
  auto src = makeSource(1, 48000, 24000, dc);
  auto pb = twoChopPlayback(src, 1, 0, 2);
  Renderer r;
  Config cfg;
  cfg.auxBuses = 8;
  r.prepare(cfg);
  r.mailbox().publish(pb);
  std::vector<float> l(512), rr(512), in(512, 0.f);
  std::vector<std::vector<float>> bufs(16, std::vector<float>(512));
  float* pairs[8][2];
  AuxOutputs aux;
  aux.count = 8;
  for (int b = 0; b < 8; ++b) {
    pairs[b][0] = bufs[static_cast<std::size_t>(b) * 2].data();
    pairs[b][1] = bufs[static_cast<std::size_t>(b) * 2 + 1].data();
    aux.bus[b] = pairs[b];
  }
  const float* inp[2] = {in.data(), in.data()};
  float* outp[2] = {l.data(), rr.data()};
  gAllocs = 0;
  gCountAllocs = true;
  for (int i = 0; i < 400; ++i) {
    TransportBlock tb;
    tb.playing = tb.positionValid = true;
    tb.ppq = i * 512 * 120.0 / (60.0 * 48000.0);
    r.process(tb, RenderParams{}, LiveInput{}, inp, outp, 2, 512, aux);
  }
  gCountAllocs = false;
  CHECK_EQ(gAllocs.load(), 0);
  OfflineSettings os;
  os.tailSeconds = 0.0;
  auto stems = renderOfflineStems(pb, os, 2);
  auto plain = renderOffline(twoChopPlayback(src, 0, 0, 0), os);
  CHECK(stems.ok() && plain.ok());
  if (stems.ok() && plain.ok()) {
    CHECK_EQ(stems.value().aux.size(), 2u);
    float worst = 0.f;
    for (std::size_t i = 0; i < plain.value()[0].size(); ++i)
      worst = std::max(worst, std::fabs(stems.value().main[0][i] + stems.value().aux[0][0][i] + stems.value().aux[1][0][i] - plain.value()[0][i]));
    CHECK(worst < 1e-6f);
  }
  CHECK(renderOffline(pb, os).ok());  // the plain offline render folds everything into main
  CHECK(!renderOfflineStems(pb, os, 9).ok());
}
