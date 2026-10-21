// Multiple outputs (F11): per-chop bus routing, aux buffers, fold-down and the offline stems.
#include <chopfractal/audio_renderer/renderer.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <thread>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

constexpr int kRate = 48000;

// Four 6000-frame slices of different tones, so every chop has its own signature.
SourcePtr toneSource() {
  auto s = std::make_shared<SourceData>();
  s->channels = 2;
  s->sampleRate = kRate;
  s->frames = 24000;
  s->samples.resize(2 * 24000);
  for (std::int64_t i = 0; i < s->frames; ++i) {
    const int slice = static_cast<int>(i / 6000);
    const double hz = 220.0 * (slice + 1);
    const float v = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / kRate));
    s->samples[static_cast<std::size_t>(i)] = v;
    s->samples[static_cast<std::size_t>(24000 + i)] = 0.8f * v;
  }
  return s;
}

FlatEvent hit(std::uint64_t id, int chop, Ticks start, Ticks dur = 480) {
  FlatEvent e;
  e.id = EventId{id};
  e.chop = ChopId{static_cast<std::uint64_t>(chop)};
  e.region = {(chop - 1) * 6000, chop * 6000};
  e.start = start;
  e.duration = dur;
  return e;
}

// Overlapping hits of chops 1..4 across a two-beat loop: enough polyphony to exercise the mix.
FlatEventList pattern() {
  FlatEventList ev;
  std::uint64_t id = 1;
  for (int k = 0; k < 12; ++k) ev.push_back(hit(id++, 1 + k % 4, static_cast<Ticks>(k * 150), 600));
  return ev;
}

std::shared_ptr<const Playback> build(const FlatEventList& ev, const BusRouting* routing = nullptr, Ticks length = 1920) {
  auto r = makePlayback(toneSource(), ev, length, PlaybackOptions{}, routing);
  CHECK(r.ok());
  return r.ok() ? r.value() : nullptr;
}

BusRouting routeChops(std::initializer_list<std::pair<int, int>> pairs, int def = 0) {
  BusRouting r;
  for (const auto& p : pairs) r.chopBus.push_back({ChopId{static_cast<std::uint64_t>(p.first)}, static_cast<std::uint8_t>(p.second)});
  std::sort(r.chopBus.begin(), r.chopBus.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  r.defaultBus = static_cast<std::uint8_t>(def);
  return r;
}

OfflineSettings settings(int block = 512, double tail = 0.2) {
  OfflineSettings s;
  s.blockSize = block;
  s.cycles = 1;
  s.tailSeconds = tail;
  s.sampleRate = kRate;
  return s;
}

float peak(const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
  float m = 0.f;
  for (std::size_t i = from; i < std::min(to, v.size()); ++i) m = std::max(m, std::fabs(v[i]));
  return m;
}

bool same(const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0; }

// Drives Renderer::process over `frames` frames in host blocks of `block`, silent input, optional aux buses.
struct Driven {
  std::vector<std::vector<float>> main{2};
  std::vector<std::vector<std::vector<float>>> aux;
};
Driven drive(const std::shared_ptr<const Playback>& pb, int auxBuses, int enabledAux, int frames, int block, int maxBlock = 2048, RenderParams params = {},
             const float* inputL = nullptr) {
  Renderer r;
  Config cfg;
  cfg.sampleRate = kRate;
  cfg.maxBlock = maxBlock;
  cfg.auxBuses = auxBuses;
  r.prepare(cfg);
  r.mailbox().publish(pb);
  Driven d;
  d.main[0].assign(static_cast<std::size_t>(frames), 0.f);
  d.main[1].assign(static_cast<std::size_t>(frames), 0.f);
  d.aux.assign(static_cast<std::size_t>(enabledAux), std::vector<std::vector<float>>(2, std::vector<float>(static_cast<std::size_t>(frames), 0.7f)));
  std::vector<float> silence(static_cast<std::size_t>(block), 0.f);
  for (int pos = 0; pos < frames; pos += block) {
    const int n = std::min(block, frames - pos);
    TransportBlock tb;
    tb.playing = true;
    tb.positionValid = true;
    tb.bpm = 120.0;
    tb.ppq = static_cast<double>(pos) * 120.0 / (60.0 * kRate);
    const float* x = inputL ? inputL + pos : silence.data();
    const float* in[2] = {x, x};
    float* o[2] = {d.main[0].data() + pos, d.main[1].data() + pos};
    AuxOutputs aux;
    float* ap[kMaxAuxBuses][2] = {};
    aux.count = enabledAux;
    for (int b = 0; b < enabledAux; ++b) {
      ap[b][0] = d.aux[static_cast<std::size_t>(b)][0].data() + pos;
      ap[b][1] = d.aux[static_cast<std::size_t>(b)][1].data() + pos;
      aux.bus[b] = ap[b];
    }
    r.process(tb, params, in, o, 2, n, aux);
  }
  return d;
}

}  // namespace

CHOP_TEST(bus_routing_lookup_default_and_validation) {
  BusRouting r = routeChops({{3, 2}, {5, 8}, {9, 0}}, 1);
  CHECK_EQ(static_cast<int>(r.busFor(ChopId{3})), 2);
  CHECK_EQ(static_cast<int>(r.busFor(ChopId{5})), 8);
  CHECK_EQ(static_cast<int>(r.busFor(ChopId{9})), 0);
  CHECK_EQ(static_cast<int>(r.busFor(ChopId{4})), 1);  // absent: the default bus
  CHECK(!r.allMain());
  CHECK(BusRouting{}.allMain());
  CHECK(routeChops({{1, 0}, {2, 0}}).allMain());

  BusRouting bad = routeChops({{1, 9}});
  CHECK(!makePlayback(toneSource(), pattern(), 1920, PlaybackOptions{}, &bad).ok());
  BusRouting badDefault;
  badDefault.defaultBus = 9;
  CHECK(!makePlayback(toneSource(), pattern(), 1920, PlaybackOptions{}, &badDefault).ok());
  BusRouting unsorted;
  unsorted.chopBus = {{ChopId{4}, 1}, {ChopId{2}, 1}};
  CHECK(!makePlayback(toneSource(), pattern(), 1920, PlaybackOptions{}, &unsorted).ok());
}

CHOP_TEST(event_bus_follows_the_sorted_prepared_events) {
  FlatEventList ev = {hit(1, 3, 900), hit(2, 1, 0), hit(3, 2, 450), hit(4, 4, 450)};
  ev[3].tx.retrigger = 2;  // expands to two prepared events
  const BusRouting r = routeChops({{1, 0}, {2, 5}, {3, 2}, {4, 8}});
  auto pb = build(ev, &r);
  if (!pb) return;
  CHECK_EQ(pb->eventBus.size(), pb->events.size());
  for (std::size_t i = 0; i < pb->events.size(); ++i) CHECK_EQ(static_cast<int>(pb->eventBus[i]), static_cast<int>(r.busFor(pb->events[i].chop)));
  CHECK(pb->events.size() == 5);
  // No routing, all-main routing: the Playback carries no bus table and equals the plain one.
  const BusRouting mains = routeChops({{1, 0}, {2, 0}});
  CHECK(build(ev, &mains)->eventBus.empty());
  CHECK(build(ev, nullptr)->eventBus.empty());
  auto plain = makePlayback(toneSource(), ev, 1920);
  CHECK(plain.ok() && plain.value()->events.size() == pb->events.size());
}

CHOP_TEST(routing_default_is_bit_identical_to_legacy_process) {
  const FlatEventList ev = pattern();
  const BusRouting mains = routeChops({{1, 0}, {2, 0}, {3, 0}, {4, 0}});
  auto legacy = build(ev);
  auto routedMain = build(ev, &mains);
  if (!legacy || !routedMain) return;
  const int frames = 512 * 188;
  // The six-argument form against the aux form with nothing enabled, on playbacks with and without a routing table.
  Renderer a;
  Config cfg;
  cfg.sampleRate = kRate;
  a.prepare(cfg);
  a.mailbox().publish(legacy);
  std::vector<float> oL(512), oR(512), silence(512, 0.f);
  std::vector<float> legacyOut;
  for (int pos = 0; pos < frames; pos += 512) {
    TransportBlock tb;
    tb.playing = true;
    tb.positionValid = true;
    tb.ppq = static_cast<double>(pos) * 120.0 / (60.0 * kRate);
    const float* in[2] = {silence.data(), silence.data()};
    float* out[2] = {oL.data(), oR.data()};
    a.process(tb, RenderParams{}, in, out, 2, 512);
    legacyOut.insert(legacyOut.end(), oL.begin(), oL.end());
  }
  for (const auto& pb : {legacy, routedMain}) {
    const Driven d = drive(pb, 0, 0, frames, 512);
    CHECK(same(d.main[0], legacyOut));
  }
  const Driven withAuxPrepared = drive(routedMain, 4, 4, frames, 512);  // buses enabled but nothing routed to them
  CHECK(same(withAuxPrepared.main[0], legacyOut));
  for (const auto& bus : withAuxPrepared.aux) CHECK(peak(bus[0]) == 0.f && peak(bus[1]) == 0.f);
  // renderOffline is the aux-free path of renderOfflineStems.
  auto off = renderOffline(legacy, settings());
  auto stems = renderOfflineStems(legacy, settings(), 0);
  CHECK(off.ok() && stems.ok());
  if (off.ok() && stems.ok()) {
    CHECK(same(off.value()[0], stems.value().main[0]) && same(off.value()[1], stems.value().main[1]));
    CHECK(stems.value().aux.empty());
  }
}

CHOP_TEST(chop_routed_to_aux_appears_only_on_that_bus) {
  // Chop 1 (220 Hz) on Out 2 during the first beat, chop 2 (440 Hz) on main during the second.
  const FlatEventList ev = {hit(1, 1, 0, 480), hit(2, 2, 960, 480)};
  const BusRouting r = routeChops({{1, 1}, {2, 0}});
  auto pb = build(ev, &r);
  if (!pb) return;
  auto s = renderOfflineStems(pb, settings(), 1);
  CHECK(s.ok());
  if (!s.ok()) return;
  const auto& m = s.value().main;
  const auto& a = s.value().aux[0];
  const std::size_t beat = 24000;
  CHECK(peak(a[0], 0, beat) > 0.2f && peak(a[1], 0, beat) > 0.15f);
  CHECK(peak(a[0], beat) == 0.f && peak(a[1], beat) == 0.f);   // chop 2 never reaches the aux bus
  CHECK(peak(m[0], 0, beat) == 0.f);                           // chop 1 never reaches main
  CHECK(peak(m[0], beat, 2 * beat) > 0.2f);
  CHECK_NEAR(peak(a[1], 0, beat) / peak(a[0], 0, beat), 0.8, 0.02);  // the stereo image survives
}

CHOP_TEST(aux_sum_equals_the_unrouted_main_mix) {
  const FlatEventList ev = pattern();
  const BusRouting r = routeChops({{1, 1}, {2, 2}, {3, 3}, {4, 4}});
  auto routed = build(ev, &r);
  auto plain = build(ev);
  if (!routed || !plain) return;
  auto s = renderOfflineStems(routed, settings(), 4);
  auto ref = renderOffline(plain, settings());
  CHECK(s.ok() && ref.ok());
  if (!s.ok() || !ref.ok()) return;
  const auto& st = s.value();
  CHECK(peak(ref.value()[0]) > 0.2f);
  CHECK(peak(st.main[0]) == 0.f);  // everything went to the aux buses
  for (int c = 0; c < 2; ++c)
    for (std::size_t i = 0; i < ref.value()[static_cast<std::size_t>(c)].size(); ++i) {
      float sum = st.main[static_cast<std::size_t>(c)][i];
      for (int b = 0; b < 4; ++b) sum += st.aux[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)][i];
      CHECK_NEAR(sum, ref.value()[static_cast<std::size_t>(c)][i], 1e-6);
    }
  for (int b = 0; b < 4; ++b) CHECK(peak(st.aux[static_cast<std::size_t>(b)][0]) > 0.1f);
}

CHOP_TEST(a_bus_the_host_did_not_enable_folds_into_main) {
  const FlatEventList ev = pattern();
  const BusRouting r = routeChops({{1, 3}, {2, 1}, {3, 8}});
  auto routed = build(ev, &r);
  auto plain = build(ev);
  if (!routed || !plain) return;
  // One aux bus enabled: chop 2 (bus 1) goes there, chops 1 and 3 (buses 3 and 8) fold into main with chop 4.
  const int frames = 48000;
  const Driven folded = drive(routed, 1, 1, frames, 512);
  const Driven unrouted = drive(plain, 0, 0, frames, 512);
  const Driven none = drive(routed, 0, 0, frames, 512);
  CHECK(same(none.main[0], unrouted.main[0]) && same(none.main[1], unrouted.main[1]));  // no aux at all: bit-exact
  for (int i = 0; i < frames; ++i) {
    const float sum = folded.main[0][static_cast<std::size_t>(i)] + folded.aux[0][0][static_cast<std::size_t>(i)];
    CHECK_NEAR(sum, unrouted.main[0][static_cast<std::size_t>(i)], 1e-6);
  }
  CHECK(peak(folded.aux[0][0]) > 0.1f);
  // Mono hosts get no aux: the buffers are cleared and main carries everything.
}

CHOP_TEST(dry_mix_reaches_main_only_and_output_gain_applies_to_every_bus) {
  const FlatEventList ev = pattern();
  const BusRouting r = routeChops({{1, 1}, {2, 2}});
  auto pb = build(ev, &r);
  if (!pb) return;
  std::vector<float> in(48000);
  for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.25f * static_cast<float>(std::sin(0.05 * static_cast<double>(i)));
  RenderParams dryOnly;
  dryOnly.dryMix = 1.f;
  const Driven d = drive(pb, 2, 2, 48000, 512, 2048, dryOnly, in.data());
  const Driven wet = drive(pb, 2, 2, 48000, 512);
  for (int b = 0; b < 2; ++b)
    for (int i = 0; i < 48000; ++i) CHECK(d.aux[static_cast<std::size_t>(b)][0][static_cast<std::size_t>(i)] == wet.aux[static_cast<std::size_t>(b)][0][static_cast<std::size_t>(i)]);
  // The dry input shows up on main: with the pattern's chops 3 and 4 also on main, the difference is exactly the input.
  for (int i = 100; i < 48000; i += 997) CHECK_NEAR(d.main[0][static_cast<std::size_t>(i)] - wet.main[0][static_cast<std::size_t>(i)], in[static_cast<std::size_t>(i)], 1e-6);

  RenderParams half;
  half.outputGainDb = -6.0205999f;
  const Driven h = drive(pb, 2, 2, 48000, 512, 2048, half);
  CHECK_NEAR(peak(h.main[0]) / peak(wet.main[0]), 0.5, 1e-3);
  for (int b = 0; b < 2; ++b) CHECK_NEAR(peak(h.aux[static_cast<std::size_t>(b)][0]) / peak(wet.aux[static_cast<std::size_t>(b)][0]), 0.5, 1e-3);
}

CHOP_TEST(aux_buffers_are_fully_written_when_silent) {
  const FlatEventList ev = {hit(1, 1, 0, 480)};
  const BusRouting r = routeChops({{1, 1}});
  auto pb = build(ev, &r);
  if (!pb) return;
  auto check = [](const Driven& d) {
    for (const auto& bus : d.aux)
      for (const auto& ch : bus) CHECK(peak(ch) == 0.f);  // drive() pre-fills every aux buffer with 0.7
  };
  RenderParams off;
  off.enabled = false;
  check(drive(pb, 1, 1, 4096, 512, 2048, off));                // disabled
  check(drive(nullptr, 1, 1, 4096, 512));                      // no source published
  auto pass = makePassThroughPlayback(toneSource());
  CHECK(pass.ok());
  if (pass.ok()) check(drive(pass.value(), 1, 1, 4096, 512));  // pass-through
  // Silent stretch of a running pattern: after the hit has rung out.
  const Driven d = drive(pb, 1, 1, 48000, 512);
  CHECK(peak(d.aux[0][0], 0, 24000) > 0.1f);
  CHECK(peak(d.aux[0][0], 36000) == 0.f);
  // More enabled buses than the renderer was prepared for, and a buffer the renderer never mixes into.
  const Driven wide = drive(pb, 1, 3, 4096, 512);
  CHECK(peak(wide.aux[0][0], 0, 4096) > 0.1f);
  CHECK(peak(wide.aux[1][0]) == 0.f && peak(wide.aux[2][0]) == 0.f);
  // An unprepared renderer clears them as well.
  Renderer cold;
  std::vector<float> a(256, 0.7f), b(256, 0.7f), c(256, 0.7f), e(256, 0.7f), x(256, 0.f), o0(256), o1(256);
  float* ap[1][2] = {{a.data(), b.data()}};
  AuxOutputs aux;
  aux.count = 1;
  aux.bus[0] = ap[0];
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {o0.data(), o1.data()};
  cold.process(TransportBlock{}, RenderParams{}, in, out, 2, 256, aux);
  CHECK(peak(a) == 0.f && peak(b) == 0.f);
  (void)c;
  (void)e;
}

CHOP_TEST(block_size_independence_with_aux) {
  const BusRouting r = routeChops({{1, 1}, {2, 2}, {3, 0}, {4, 2}});
  auto pb = build(pattern(), &r);
  if (!pb) return;
  const int frames = 60000;
  const Driven ref = drive(pb, 2, 2, frames, 2048);
  CHECK(peak(ref.aux[0][0]) > 0.1f && peak(ref.aux[1][0]) > 0.1f);
  for (int block : {1, 7, 64, 333, 777}) {
    const Driven d = drive(pb, 2, 2, frames, block);
    CHECK(same(d.main[0], ref.main[0]) && same(d.main[1], ref.main[1]));
    for (int b = 0; b < 2; ++b)
      for (int c = 0; c < 2; ++c) CHECK(same(d.aux[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)], ref.aux[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)]));
  }
  // Host blocks larger than the prepared maximum are chunked, and the aux pointers move with the chunks.
  const Driven big = drive(pb, 2, 2, frames, 4000, 256);
  for (int b = 0; b < 2; ++b) CHECK(same(big.aux[static_cast<std::size_t>(b)][0], ref.aux[static_cast<std::size_t>(b)][0]));
  CHECK(same(big.main[0], ref.main[0]));
}

CHOP_TEST(a_gap_in_the_enabled_buses_is_served_and_the_disabled_bus_folds_into_main) {
  // Out 2 (bus 1) disabled by the host, Out 3 (bus 2) enabled: chop 1 routed to bus 1 plays on main, chop 2 on bus 2.
  const FlatEventList ev = {hit(1, 1, 0, 480), hit(2, 2, 960, 480)};
  const BusRouting r = routeChops({{1, 1}, {2, 2}});
  auto pb = build(ev, &r);
  if (!pb) return;
  Renderer rd;
  Config cfg;
  cfg.sampleRate = kRate;
  cfg.auxBuses = 2;
  rd.prepare(cfg);
  rd.mailbox().publish(pb);
  const int frames = 48000;
  std::vector<float> m0(frames), m1(frames), b0(frames, 0.7f), b1(frames, 0.7f), x(512, 0.f);
  for (int pos = 0; pos < frames; pos += 512) {
    const int n = std::min(512, frames - pos);
    TransportBlock tb;
    tb.playing = true;
    tb.positionValid = true;
    tb.ppq = static_cast<double>(pos) * 120.0 / (60.0 * kRate);
    const float* in[2] = {x.data(), x.data()};
    float* o[2] = {m0.data() + pos, m1.data() + pos};
    float* ap[2][2] = {{nullptr, nullptr}, {b0.data() + pos, b1.data() + pos}};
    AuxOutputs aux;
    aux.count = 2;
    aux.bus[0] = ap[0];
    aux.bus[1] = ap[1];
    rd.process(tb, RenderParams{}, in, o, 2, n, aux);
  }
  CHECK(peak(m0, 0, 24000) > 0.2f);      // chop 1 folded into main
  CHECK(peak(m0, 24000) == 0.f);         // chop 2 is on its own bus
  CHECK(peak(b0, 0, 24000) == 0.f);
  CHECK(peak(b0, 24000, 48000) > 0.2f);
}

CHOP_TEST(previews_and_pads_play_on_main_only) {
  const BusRouting r = routeChops({{1, 1}, {2, 1}, {3, 1}, {4, 1}});
  auto pb = build({hit(1, 1, 1500, 100)}, &r);
  if (!pb) return;
  Renderer rd;
  Config cfg;
  cfg.sampleRate = kRate;
  cfg.auxBuses = 1;
  rd.prepare(cfg);
  rd.mailbox().publish(pb);
  auto bank = std::make_shared<PadBank>();
  bank->slots[36].region = {0, 6000};
  rd.padMailbox().publish(bank);
  PreviewRequest pr;
  pr.region = {6000, 12000};
  CHECK(rd.requestPreview(pr));
  std::vector<float> m0(512), m1(512), a0(512, 0.7f), a1(512, 0.7f), x(512, 0.f);
  float* ap[1][2] = {{a0.data(), a1.data()}};
  AuxOutputs aux;
  aux.count = 1;
  aux.bus[0] = ap[0];
  PadEvent pe;
  pe.frame = 0;
  pe.note = 36;
  LiveInput live;
  live.pads = &pe;
  live.numPads = 1;
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {m0.data(), m1.data()};
  TransportBlock tb;  // stopped transport: only the preview and the pad sound
  rd.process(tb, RenderParams{}, in, out, 2, 512, live, aux);
  CHECK(peak(m0) > 0.05f);
  CHECK(peak(a0) == 0.f && peak(a1) == 0.f);
}

CHOP_TEST(mono_main_gets_the_whole_mix_and_clears_aux) {
  const BusRouting r = routeChops({{1, 1}, {2, 1}, {3, 1}, {4, 1}});
  auto routed = build(pattern(), &r);
  auto plain = build(pattern());
  if (!routed || !plain) return;
  auto run = [&](const std::shared_ptr<const Playback>& pb, bool withAux) {
    Renderer rd;
    Config cfg;
    cfg.sampleRate = kRate;
    cfg.auxBuses = 1;
    rd.prepare(cfg);
    rd.mailbox().publish(pb);
    std::vector<float> out(24000, 0.f), a0(24000, 0.7f), a1(24000, 0.7f), x(512, 0.f);
    float* ap[1][2] = {{a0.data(), a1.data()}};
    AuxOutputs aux;
    if (withAux) {
      aux.count = 1;
      aux.bus[0] = ap[0];
    }
    for (int pos = 0; pos < 24000; pos += 512) {
      const int n = std::min(512, 24000 - pos);
      TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.ppq = static_cast<double>(pos) * 120.0 / (60.0 * kRate);
      const float* in[1] = {x.data()};
      float* o[1] = {out.data() + pos};
      AuxOutputs chunk = aux;
      float* cp[1][2] = {{a0.data() + pos, a1.data() + pos}};
      if (withAux) chunk.bus[0] = cp[0];
      rd.process(tb, RenderParams{}, in, o, 1, n, chunk);
    }
    CHECK(!withAux || (peak(a0) == 0.f && peak(a1) == 0.f));
    return out;
  };
  // A mono host cannot use aux buses: the routed playback sounds exactly like the unrouted one.
  CHECK(same(run(routed, true), run(plain, false)));
}

CHOP_TEST(the_limiter_delays_the_aux_buses_like_main) {
  // The limiter fades its delayed path in over the first frames after it starts, so the pattern begins a beat in.
  FlatEventList late = pattern();
  for (FlatEvent& e : late) e.start += 960;
  const BusRouting r = routeChops({{1, 1}, {2, 1}});
  auto routed = build(late, &r, 3840);
  auto plain = build(late, nullptr, 3840);
  if (!routed || !plain) return;
  RenderParams lim;
  lim.limiterEnabled = true;
  lim.limiterCeilingDb = 0.f;
  lim.outputGainDb = -12.f;  // far below the ceiling: the limiter only delays
  const int frames = 60000;
  const Driven a = drive(routed, 1, 1, frames, 512, 2048, lim);
  const Driven ref = drive(plain, 0, 0, frames, 512, 2048, lim);
  // The pattern stays far below the ceiling, so the limiter only delays: main + aux lines up with the unrouted limited main.
  float worst = 0.f;
  for (int i = 0; i < frames; ++i) worst = std::max(worst, std::fabs(a.main[0][static_cast<std::size_t>(i)] + a.aux[0][0][static_cast<std::size_t>(i)] - ref.main[0][static_cast<std::size_t>(i)]));
  CHECK(worst < 1e-4f);
  CHECK(peak(a.aux[0][0]) > 0.1f);
  // The first sound arrives later than without the limiter, by exactly the limiter latency.
  const Driven nolim = drive(routed, 1, 1, frames, 512);
  auto firstNonZero = [](const std::vector<float>& v) {
    for (std::size_t i = 0; i < v.size(); ++i)
      if (v[i] != 0.f) return static_cast<long>(i);
    return -1L;
  };
  CHECK_EQ(firstNonZero(a.aux[0][0]) - firstNonZero(nolim.aux[0][0]), static_cast<long>(Limiter::latencyFor(kRate)));
}

CHOP_TEST(republishing_the_routing_while_processing_is_safe) {
  const FlatEventList ev = pattern();
  const BusRouting a = routeChops({{1, 1}, {2, 2}});
  const BusRouting b = routeChops({{3, 2}, {4, 1}});
  const BusRouting c = routeChops({{1, 3}}, 4);
  const std::shared_ptr<const Playback> pbs[3] = {build(ev, &a), build(ev, &b), build(ev, &c)};
  if (!pbs[0] || !pbs[1] || !pbs[2]) return;
  Renderer rd;
  Config cfg;
  cfg.sampleRate = kRate;
  cfg.auxBuses = 4;
  rd.prepare(cfg);
  rd.mailbox().publish(pbs[0]);
  std::atomic<bool> stop{false};
  std::thread publisher([&] {
    int k = 0;
    while (!stop.load()) {
      rd.mailbox().publish(pbs[++k % 3]);
      std::this_thread::yield();
    }
  });
  std::vector<float> m0(512), m1(512), x(512, 0.f);
  std::vector<std::vector<float>> bus(8, std::vector<float>(512));
  float* ap[4][2];
  AuxOutputs aux;
  aux.count = 4;
  for (int i = 0; i < 4; ++i) {
    ap[i][0] = bus[static_cast<std::size_t>(2 * i)].data();
    ap[i][1] = bus[static_cast<std::size_t>(2 * i + 1)].data();
    aux.bus[i] = ap[i];
  }
  bool finite = true;
  for (int i = 0; i < 1500; ++i) {
    TransportBlock tb;
    tb.playing = true;
    tb.positionValid = true;
    tb.ppq = static_cast<double>(i) * 512.0 * 120.0 / (60.0 * kRate);
    const float* in[2] = {x.data(), x.data()};
    float* out[2] = {m0.data(), m1.data()};
    rd.process(tb, RenderParams{}, in, out, 2, 512, aux);
    for (const auto& v : bus)
      for (float s : v) finite = finite && std::isfinite(s);
  }
  stop = true;
  publisher.join();
  CHECK(finite);
}
