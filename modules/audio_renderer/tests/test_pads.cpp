// Performance pads (F07): MIDI-style note events start voices on their exact sample on top of the pattern.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/audio_renderer/spsc_queue.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

constexpr int kRate = 48000;

SourcePtr dcSource(std::int64_t frames = 96000) {
  auto s = std::make_shared<SourceData>();
  s->channels = 1;
  s->sampleRate = kRate;
  s->frames = frames;
  s->samples.assign(static_cast<std::size_t>(frames), 1.f);
  return s;
}

// Note 36 + k plays [k * 4800, (k + 1) * 4800) of the source, except where a test says otherwise.
std::shared_ptr<const PadBank> makeBank(std::int64_t sliceFrames = 4800, int notes = 8, int base = 36) {
  auto b = std::make_shared<PadBank>();
  for (int k = 0; k < notes; ++k) b->slots[static_cast<std::size_t>(base + k)].region = {k * sliceFrames, (k + 1) * sliceFrames};
  return b;
}

PadEvent on(int frame, int note, int vel = 127) {
  PadEvent e;
  e.frame = frame;
  e.type = PadEvent::Type::NoteOn;
  e.note = static_cast<std::uint8_t>(note);
  e.velocity = static_cast<std::uint8_t>(vel);
  return e;
}
PadEvent off(int frame, int note) {
  PadEvent e = on(frame, note);
  e.type = PadEvent::Type::NoteOff;
  return e;
}
PadEvent allOff(int frame) {
  PadEvent e = on(frame, 0);
  e.type = PadEvent::Type::AllNotesOff;
  return e;
}

struct Rig {
  Renderer r;
  explicit Rig(SourcePtr src, bool pattern = false, std::shared_ptr<const PadBank> bank = nullptr, int maxBlock = 2048, int maxVoices = 8) {
    Config cfg;
    cfg.sampleRate = kRate;
    cfg.maxBlock = maxBlock;
    cfg.maxVoices = maxVoices;
    r.prepare(cfg);
    if (pattern) {
      FlatEvent e;
      e.id = EventId{1};
      e.chop = ChopId{1};
      e.region = {0, 60000};
      e.start = 0;
      e.duration = 3840;  // the whole 4-beat loop
      auto pb = makePlayback(src, {e}, 3840);
      CHECK(pb.ok());
      r.mailbox().publish(pb.value());
    } else {
      auto pb = makePassThroughPlayback(src);
      CHECK(pb.ok());
      r.mailbox().publish(pb.value());
    }
    r.padMailbox().publish(bank ? bank : makeBank());
  }
  // One host block. Pad frames are relative to this block.
  std::vector<float> block(int frames, const std::vector<PadEvent>& pads = {}, PadParams params = {}, bool playing = false, double ppq = 0.0, bool* ok = nullptr) {
    std::vector<float> in(static_cast<std::size_t>(frames), 0.f), l(static_cast<std::size_t>(frames)), rr(static_cast<std::size_t>(frames));
    const float* inp[2] = {in.data(), in.data()};
    float* outp[2] = {l.data(), rr.data()};
    TransportBlock tb;
    tb.playing = playing;
    tb.positionValid = true;
    tb.ppq = ppq;
    tb.bpm = 120.0;
    RenderParams rp;
    LiveInput live;
    live.pads = pads.empty() ? nullptr : pads.data();
    live.numPads = static_cast<int>(pads.size());
    live.padParams = params;
    r.process(tb, rp, inp, outp, 2, frames, live);
    if (ok) *ok = l == rr;
    return l;
  }
};

float peakOf(const std::vector<float>& v, std::size_t from = 0, std::size_t to = ~std::size_t{0}) {
  float m = 0.f;
  for (std::size_t i = from; i < v.size() && i < to; ++i) m = std::max(m, std::fabs(v[i]));
  return m;
}

}  // namespace

CHOP_TEST(velocity_curves_are_exact) {
  CHECK(velocityGain(VelocityCurve::Linear, 127) == 1.f);
  CHECK(velocityGain(VelocityCurve::Soft, 127) == 1.f);
  CHECK(velocityGain(VelocityCurve::Hard, 127) == 1.f);
  CHECK_NEAR(velocityGain(VelocityCurve::Linear, 64), 64.0 / 127.0, 1e-7);
  CHECK_NEAR(velocityGain(VelocityCurve::Soft, 1), std::sqrt(1.0 / 127.0), 1e-7);
  CHECK_NEAR(velocityGain(VelocityCurve::Hard, 64), (64.0 / 127.0) * (64.0 / 127.0), 1e-7);
  CHECK(velocityGain(VelocityCurve::Fixed, 1) == 1.f);
  CHECK(velocityGain(VelocityCurve::Fixed, 127) == 1.f);
  CHECK(velocityGain(VelocityCurve::Linear, 0) == velocityGain(VelocityCurve::Linear, 1));    // clamped to 1..127
  CHECK(velocityGain(VelocityCurve::Linear, 500) == 1.f);
  CHECK(velocityGain(VelocityCurve::Linear, -5) == velocityGain(VelocityCurve::Linear, 1));
}

CHOP_TEST(pad_note_on_starts_a_voice_on_the_exact_frame) {
  Rig rig(dcSource());
  bool same = false;
  const auto out = rig.block(512, {on(100, 36)}, {}, false, 0.0, &same);
  CHECK(same);
  for (int i = 0; i <= 100; ++i) CHECK(out[static_cast<std::size_t>(i)] == 0.f);
  int first = -1;
  for (int i = 0; i < 512 && first < 0; ++i)
    if (out[static_cast<std::size_t>(i)] != 0.f) first = i;
  CHECK(first >= 101 && first <= 102);
  CHECK_NEAR(out[100 + 40], 1.0, 1e-5);  // after the 32 frame attack the chop plays at full level
}

CHOP_TEST(velocity_scales_the_level_through_the_selected_curve) {
  for (int curve = 0; curve < 4; ++curve) {
    Rig rig(dcSource());
    PadParams p;
    p.curve = static_cast<VelocityCurve>(curve);
    const auto out = rig.block(512, {on(0, 36, 64)}, p);
    CHECK_NEAR(out[200], velocityGain(p.curve, 64), 1e-5);
  }
}

CHOP_TEST(poly_layers_and_respects_the_voice_limit) {
  Rig rig(dcSource());
  const auto two = rig.block(512, {on(0, 36), on(0, 37)});
  CHECK_NEAR(two[300], 2.0, 1e-5);  // Poly layers
  Rig many(dcSource(), false, nullptr, 2048, 4);
  std::vector<PadEvent> ev;
  for (int k = 0; k < 8; ++k) ev.push_back(on(k * 10, 36 + k));
  const auto out = many.block(1024, ev);
  CHECK_NEAR(out[900], 4.0, 1e-4);  // eight notes on four voices: the quietest are stolen (faded out), four keep sounding
  CHECK(out[900] <= 4.0001f);
}

CHOP_TEST(mono_releases_previous_pad_voice_but_not_pattern_voices) {
  Rig rig(dcSource(), true);
  PadParams mono;
  mono.mode = PadMode::Mono;
  const auto base = rig.block(512, {}, mono, true, 0.0);
  CHECK_NEAR(base[400], 1.0, 1e-5);  // the pattern event alone
  const auto a = rig.block(512, {on(0, 36)}, mono, true, 512.0 * 120.0 / (60.0 * kRate));
  CHECK_NEAR(a[300], 2.0, 1e-5);
  const auto b = rig.block(512, {on(0, 37)}, mono, true, 1024.0 * 120.0 / (60.0 * kRate));
  CHECK_NEAR(b[300], 2.0, 1e-5);  // pad 36 was released (64 frame fade), pad 37 and the pattern remain: not 3
  CHECK(b[10] > 2.0f - 0.2f && b[10] < 3.0f);  // still fading at the start of the block
}

CHOP_TEST(gate_note_off_releases_with_a_64_frame_fade_and_one_shot_ignores_note_off) {
  PadParams gate;
  gate.mode = PadMode::Gate;
  Rig g(dcSource(), false, makeBank(20000));
  const auto out = g.block(2048, {on(0, 36), off(1000, 36)}, gate);
  CHECK_NEAR(out[999], 1.0, 1e-5);
  CHECK(out[1000 + 32] < 0.7f && out[1000 + 32] > 0.3f);  // halfway through the fade
  CHECK(out[1000 + 70] == 0.f);
  PadParams poly;
  Rig p(dcSource(), false, makeBank(20000));
  const auto kept = p.block(2048, {on(0, 36), off(1000, 36)}, poly);
  CHECK_NEAR(kept[1500], 1.0, 1e-5);  // one-shot: the note-off is ignored
  // Gate: a retrigger of the same note releases the earlier voice.
  Rig g2(dcSource(), false, makeBank(20000));
  const auto re = g2.block(2048, {on(0, 36), on(500, 36)}, gate);
  CHECK_NEAR(re[1200], 1.0, 1e-5);
}

CHOP_TEST(choke_group_releases_same_group_pad_voices_only) {
  auto bank = std::make_shared<PadBank>(*makeBank(20000));
  bank->slots[36].chokeGroup = 1;
  bank->slots[37].chokeGroup = 1;
  bank->slots[38].chokeGroup = 2;
  bank->slots[39].chokeGroup = 0;
  Rig rig(dcSource(), false, bank);
  const auto out = rig.block(2048, {on(0, 36), on(0, 38), on(0, 39), on(1000, 37)});
  CHECK_NEAR(out[900], 3.0, 1e-5);
  CHECK_NEAR(out[1500], 3.0, 1e-5);  // 36 choked by 37 (same group); 37, 38 and 39 sound: still three voices
  // Without 37 the first group member would still be sounding.
  Rig rig2(dcSource(), false, bank);
  const auto out2 = rig2.block(2048, {on(0, 36), on(0, 38), on(0, 39)});
  CHECK_NEAR(out2[1500], 3.0, 1e-5);
}

CHOP_TEST(all_notes_off_releases_every_pad_voice) {
  Rig rig(dcSource(), false, makeBank(20000));
  const auto out = rig.block(2048, {on(0, 36), on(0, 37), allOff(800)});
  CHECK_NEAR(out[799], 2.0, 1e-5);
  CHECK(out[800 + 100] == 0.f);
}

CHOP_TEST(pads_sound_with_stopped_transport_and_in_pass_through_playback) {
  Rig pt(dcSource());
  CHECK(peakOf(pt.block(512, {on(10, 36)}, {}, false)) > 0.9f);
  Rig pat(dcSource(), true);
  const auto out = pat.block(512, {on(10, 36)}, {}, false);  // a pattern exists but the host is stopped
  CHECK_NEAR(out[300], 1.0, 1e-5);
}

CHOP_TEST(pad_voices_survive_transport_stop_seek_and_loop_wrap) {
  Rig rig(dcSource(), true, makeBank(30000));
  const double q = 512.0 * 120.0 / (60.0 * kRate);
  rig.block(512, {on(0, 36)}, {}, true, 0.0);
  const auto stopped = rig.block(512, {}, {}, false, q);      // transport stopped: the pattern voice fades, the pad keeps playing
  CHECK_NEAR(stopped[400], 1.0, 1e-5);
  const auto seek = rig.block(512, {}, {}, true, 7.0);        // a seek
  CHECK(seek[400] > 0.99f && seek[400] < 2.01f);
  // The pad reaches its end only when the chop does, not when the transport moves.
  const auto later = rig.block(512, {}, {}, true, 7.0 + q);
  CHECK(later[300] >= 1.0f - 1e-5f);
}

CHOP_TEST(unmapped_notes_and_regions_outside_the_source_are_ignored) {
  auto bank = std::make_shared<PadBank>(*makeBank());
  bank->slots[60].region = {90000, 100000};  // beyond the 96000 frame source
  Rig rig(dcSource(), false, bank);
  const auto out = rig.block(512, {on(0, 20), on(0, 60), on(0, 127), on(0, 36), off(10, 99)});
  CHECK_NEAR(out[300], 1.0, 1e-5);  // only note 36 sounds
  CHECK(rig.r.padUnmapped() == 3);
}

CHOP_TEST(pad_events_are_block_size_independent) {
  const std::vector<PadEvent> all = {on(100, 36), on(700, 37, 90), on(1500, 38, 40), on(1501, 39, 127), off(2500, 38), on(3000, 36)};
  PadParams gate;
  gate.mode = PadMode::Gate;
  std::vector<float> ref;
  for (int block : {64, 333, 512, 2048}) {
    Rig rig(dcSource(), true, makeBank(6000));
    std::vector<float> out;
    int done = 0;
    while (done < 5000) {
      std::vector<PadEvent> mine;
      for (PadEvent e : all)
        if (e.frame >= done && e.frame < done + block) {
          e.frame -= done;
          mine.push_back(e);
        }
      const auto b = rig.block(block, mine, gate, true, static_cast<double>(done) * 120.0 / (60.0 * kRate));
      out.insert(out.end(), b.begin(), b.end());
      done += block;
    }
    out.resize(5000);
    if (ref.empty()) ref = out;
    CHECK(out == ref);
  }
  CHECK(peakOf(ref) > 1.0f);
}

CHOP_TEST(oversize_host_blocks_rebase_pad_frames) {
  const std::vector<PadEvent> ev = {on(50, 36), on(700, 37, 80), on(1300, 38), off(1301, 36)};
  PadParams gate;
  gate.mode = PadMode::Gate;
  Rig small(dcSource(), false, makeBank(6000), 256);
  Rig big(dcSource(), false, makeBank(6000), 2048);
  const auto a = small.block(1500, ev, gate);  // larger than the prepared 256: processed in chunks
  const auto b = big.block(1500, ev, gate);
  CHECK(a == b);
  CHECK(peakOf(a) > 1.5f);
}

CHOP_TEST(output_without_pads_is_bit_identical_to_the_six_argument_process) {
  auto run = [](bool sixArg) {
    Rig rig(dcSource(), true);
    std::vector<float> out;
    for (int b = 0; b < 40; ++b) {
      std::vector<float> in(512, 0.1f), l(512), r(512);
      const float* inp[2] = {in.data(), in.data()};
      float* outp[2] = {l.data(), r.data()};
      TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.ppq = b * 512.0 * 120.0 / (60.0 * kRate);
      RenderParams rp;
      rp.dryMix = 0.3f;
      if (sixArg) rig.r.process(tb, rp, inp, outp, 2, 512);
      else rig.r.process(tb, rp, inp, outp, 2, 512, LiveInput{});
      out.insert(out.end(), l.begin(), l.end());
    }
    return out;
  };
  CHECK(run(true) == run(false));
}

CHOP_TEST(pad_events_beyond_the_per_block_cap_are_counted_not_played) {
  Rig rig(dcSource(), false, makeBank(100));
  std::vector<PadEvent> ev;
  for (int i = 0; i < kMaxPadEventsPerBlock + 20; ++i) ev.push_back(on(0, 36));
  rig.block(512, ev);
  CHECK(rig.r.padDropped() == 20);
}

CHOP_TEST(pads_are_ignored_and_killed_when_the_effect_is_off) {
  Rig rig(dcSource());
  rig.block(512, {on(0, 36)});
  CHECK(rig.r.activePadVoices() == 1);
  std::vector<float> in(256, 0.f), l(256), r(256);
  const float* inp[2] = {in.data(), in.data()};
  float* outp[2] = {l.data(), r.data()};
  RenderParams off2;
  off2.enabled = false;
  const PadEvent e = on(0, 36);
  LiveInput live;
  live.pads = &e;
  live.numPads = 1;
  rig.r.process(TransportBlock{}, off2, inp, outp, 2, 256, live);
  CHECK(peakOf(l) == 0.f);
  CHECK(rig.r.activePadVoices() == 0);
}

CHOP_TEST(reset_and_a_new_source_kill_pad_voices) {
  Rig rig(dcSource(), false, makeBank(30000));
  rig.block(512, {on(0, 36)});
  CHECK(rig.r.activePadVoices() == 1);
  rig.r.reset();
  CHECK(peakOf(rig.block(512)) == 0.f);
  rig.block(512, {on(0, 36)});
  auto other = dcSource(90000);
  auto pt = makePassThroughPlayback(other);
  CHECK(pt.ok());
  rig.r.mailbox().publish(pt.value());
  CHECK(peakOf(rig.block(512)) == 0.f);  // voices pointed into the old source
}

CHOP_TEST(pad_bank_mailbox_handoff_across_threads) {
  Rig rig(dcSource(), true, makeBank(4800));
  std::atomic<bool> stop{false};
  std::thread publisher([&] {
    for (int i = 0; i < 400 && !stop.load(); ++i) {
      rig.r.padMailbox().publish(makeBank(2400 + 100 * (i % 20)));
      std::this_thread::yield();
    }
  });
  bool finite = true;
  for (int b = 0; b < 400; ++b) {
    const auto out = rig.block(256, {on(b % 200, 36 + b % 8), off(200 + b % 50, 36 + b % 8)}, PadParams{PadMode::Gate, VelocityCurve::Linear}, true, b * 256.0 * 120.0 / (60.0 * kRate));
    for (float x : out) finite = finite && std::isfinite(x);
  }
  stop = true;
  publisher.join();
  CHECK(finite);
}

CHOP_TEST(uipad_queue_order_and_overflow) {
  SpscQueue<PadEvent, 128> q;
  for (int i = 0; i < 128; ++i) CHECK(q.tryPush(on(i, i % 100)));
  CHECK(!q.tryPush(on(999, 1)));  // full: nothing is overwritten
  CHECK(q.sizeApprox() == 128);
  for (int i = 0; i < 128; ++i) {
    PadEvent e;
    CHECK(q.tryPop(e));
    CHECK(e.frame == i);
  }
  PadEvent e;
  CHECK(!q.tryPop(e));
  // Wrap-around across many fills.
  for (int round = 0; round < 5; ++round) {
    for (int i = 0; i < 100; ++i) CHECK(q.tryPush(on(round * 1000 + i, 1)));
    for (int i = 0; i < 100; ++i) {
      CHECK(q.tryPop(e));
      CHECK(e.frame == round * 1000 + i);
    }
  }
}

CHOP_TEST(spsc_queue_threaded_producer_consumer_loses_and_duplicates_nothing) {
  SpscQueue<PadEvent, 64> q;
  constexpr int kCount = 20000;
  std::thread producer([&] {
    for (int i = 0; i < kCount;) {
      PadEvent e;
      e.frame = i;
      if (q.tryPush(e)) ++i;
      else std::this_thread::yield();
    }
  });
  int expected = 0;
  bool ordered = true;
  while (expected < kCount) {
    PadEvent e;
    if (q.tryPop(e)) {
      ordered = ordered && e.frame == expected;
      ++expected;
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();
  CHECK(ordered);
}
