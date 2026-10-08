#include <limits>
#include <atomic>
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/rng.hpp>
#include <cmath>
#include <cstdlib>
#include <new>
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
  for (int k = 1; k <= 3000; ++k) box.publish(std::make_shared<const std::vector<int>>(static_cast<std::size_t>(64 + k % 64), k));
  done = true;
  reader.join();
  CHECK_EQ(torn.load(), 0);
  CHECK(reads.load() > 0);
  const std::vector<int>* last = box.acquire();
  CHECK(last && (*last)[0] == 3000);
  box.collect();
  CHECK(box.retainedForTesting() <= 2);  // everything older than the acknowledged object has been freed
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
