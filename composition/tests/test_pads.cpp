// Integration tests for live pads (docs/specs/roadmap/F07-performance-pads.md).
#include <algorithm>
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/state_codec/codec.hpp>
#include <cmath>
#include <memory>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

std::shared_ptr<const render::SourceData> loop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 5000; ++i)
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(i) / 900.f) * 0.8f;
  return d;
}

pattern::Settings cfg(std::uint64_t seed = 1) {
  pattern::Settings s;
  s.bars = 2;
  s.density = 0.4;
  s.seed = seed;
  return s;
}

struct Rig {
  render::Renderer r;
  ProjectSession s;
  Rig() {
    r.prepare(render::Config{});
    s.attachMailbox(&r.mailbox());
    s.attachPadMailbox(&r.padMailbox());
    CHECK(s.loadSource(loop(), "loop", "/music/loop.wav").ok());
    DetectOptions d;
    d.analysis.sensitivity = 0.8;
    CHECK(s.applyChops(d).ok());
    CHECK(s.generate(cfg()).ok());
  }
  // Renders a block with one NoteOn at frame 0 and reports whether anything sounded.
  float playNote(int note) {
    r.reset();  // silence earlier probes
    std::vector<float> in(2048, 0.f), l(2048), rr(2048);
    render::PadEvent e;
    e.type = render::PadEvent::Type::NoteOn;
    e.note = static_cast<std::uint8_t>(note);
    render::LiveInput live;
    live.pads = &e;
    live.numPads = 1;
    render::TransportBlock tb;
    const float* inp[2] = {in.data(), in.data()};
    float* outp[2] = {l.data(), rr.data()};
    r.process(tb, render::RenderParams{}, live, inp, outp, 2, 2048);
    float pk = 0.f;
    for (float x : l) pk = std::max(pk, std::fabs(x));
    return pk;
  }
};

}  // namespace

CHOP_TEST(pad_bank_maps_chops_in_snapshot_order_from_the_base_note_and_follows_edits) {
  Rig rig;
  const std::size_t n = rig.s.chops()->chops.size();
  CHECK_EQ(n, 8u);
  CHECK(rig.playNote(36) > 0.05f);                      // chop 0
  CHECK(rig.playNote(36 + static_cast<int>(n) - 1) > 0.05f);  // last chop
  CHECK(rig.playNote(35) == 0.f);                       // below the base note
  CHECK(rig.playNote(36 + static_cast<int>(n)) == 0.f);  // above the last chop
  PadSettings ps;
  ps.baseNote = 60;
  CHECK(rig.s.setPadSettings(ps).ok());
  CHECK(rig.playNote(60) > 0.05f);
  CHECK(rig.playNote(36) == 0.f);
  ps.baseNote = 111;  // the highest allowed base note still maps all eight chops (111..118)
  CHECK(rig.s.setPadSettings(ps).ok());
  CHECK(rig.playNote(111) > 0.05f && rig.playNote(118) > 0.05f && rig.playNote(119) == 0.f);
  CHECK(!rig.s.setPadSettings([] { PadSettings x; x.baseNote = 112; return x; }()).ok());
  CHECK(!rig.s.setPadSettings([] { PadSettings x; x.midiChannel = 17; return x; }()).ok());
  CHECK(!rig.s.setPadSettings([] { PadSettings x; x.quantizeDivision = 5; return x; }()).ok());
  // disabling a chop drops its pad: the map is rebuilt from the new snapshot
  ps.baseNote = 36;
  CHECK(rig.s.setPadSettings(ps).ok());
  CHECK(rig.s.editMarkers([&](source::ChopMap& m) { return m.setEnabled(m.markers().back().id, false); }).ok());
  CHECK_EQ(rig.s.chops()->chops.size(), 7u);
  CHECK(rig.playNote(36 + 7) == 0.f);
}

CHOP_TEST(hat_role_defaults_to_choke_group_1_and_override_wins) {
  Rig rig;
  const auto& chops = rig.s.chops()->chops;
  CHECK(rig.s.assignRole(chops[0].id, "hat").ok());
  CHECK(rig.s.assignRole(chops[1].id, "hat").ok());
  CHECK(rig.s.assignRole(chops[2].id, "kick").ok());
  // the bank is private: observe choke through audio. Two hats on the same default group choke each other.
  auto sounding = [&](int a, int b) {
    std::vector<float> in(4096, 0.f), l(4096), rr(4096);
    render::PadEvent ev[2];
    ev[0].type = ev[1].type = render::PadEvent::Type::NoteOn;
    ev[0].note = static_cast<std::uint8_t>(a);
    ev[0].frame = 0;
    ev[1].note = static_cast<std::uint8_t>(b);
    ev[1].frame = 1000;
    render::LiveInput live;
    live.pads = ev;
    live.numPads = 2;
    render::TransportBlock tb;
    const float* inp[2] = {in.data(), in.data()};
    float* outp[2] = {l.data(), rr.data()};
    rig.r.reset();
    rig.r.process(tb, render::RenderParams{}, live, inp, outp, 2, 4096);
    return rig.r.activePadVoices();
  };
  CHECK_EQ(sounding(36, 37), 1);  // two hats share the default group: the second chokes the first
  CHECK_EQ(sounding(36, 38), 2);  // a kick is not in the group
  CHECK(rig.s.setChokeGroup(chops[0].id, 0).ok());
  CHECK_EQ(sounding(36, 37), 2);  // the override takes the first hat out of the group
  CHECK_EQ(rig.s.padSettings().chokeOverride.at(chops[0].id.value), 0);
  CHECK(!rig.s.setChokeGroup(chops[0].id, 5).ok());
  CHECK(!rig.s.setChokeGroup(ChopId{9999}, 1).ok());
}

CHOP_TEST(merge_take_quantizes_wraps_and_adds_user_owned_events_in_one_undo_step) {
  Rig rig;
  const auto before = pattern::serialize(*rig.s.pattern());
  const std::size_t events = pattern::eventCount(*rig.s.pattern());
  TakeHit a;
  a.ppqOn = 1.04;  // -> tick 960 (1/16 grid), bar 0
  a.note = 36;
  a.velocity = 127;
  TakeHit b;
  b.ppqOn = 7.95;  // 7632 ticks rounds to 7680 = the loop length: wraps to 0
  b.note = 37;
  b.velocity = 64;
  TakeOptions o;
  o.curve = render::VelocityCurve::Hard;
  auto r = rig.s.mergeTake({a, b}, o);
  CHECK(r.ok());
  CHECK_EQ(r.value().added, 2);
  CHECK_EQ(pattern::eventCount(*rig.s.pattern()), events + 2);
  const ChopId c0 = rig.s.chops()->chops[0].id, c1 = rig.s.chops()->chops[1].id;
  bool foundA = false, foundB = false;
  for (const auto& bt : rig.s.pattern()->bars[0].beats)
    for (const Event& e : bt.events) {
      if (e.userOwned && e.chop == c0 && e.start == 960) {
        foundA = true;
        CHECK_NEAR(e.tx.level, 1.0, 1e-6);
      }
      if (e.userOwned && e.chop == c1 && e.start == 0) {
        foundB = true;
        CHECK_NEAR(e.tx.level, (64.0 / 127.0) * (64.0 / 127.0), 1e-5);  // Hard curve
      }
    }
  CHECK(foundA && foundB);
  CHECK(rig.s.undo());
  CHECK(pattern::serialize(*rig.s.pattern()) == before);
}

CHOP_TEST(merge_take_dedupes_respects_locks_and_cap_and_replace_deletes_the_slot_only) {
  Rig rig;
  TakeHit h;
  h.ppqOn = 0.0;
  h.note = 38;
  TakeOptions o;
  auto first = rig.s.mergeTake({h}, o);
  CHECK(first.ok() && first.value().added + first.value().skippedDuplicate == 1);
  auto again = rig.s.mergeTake({h}, o);
  CHECK(again.ok() && again.value().added == 0 && again.value().skippedDuplicate == 1);  // no second undo entry
  // unmapped notes
  TakeHit stray;
  stray.note = 3;
  auto none = rig.s.mergeTake({stray}, o);
  CHECK(none.ok() && none.value().skippedUnmapped == 1 && none.value().added == 0);
  // locked bar refuses
  CHECK(rig.s.edit([](const pattern::Pattern& p, const ChopSnapshot&) {
    return pattern::setLock(p, pattern::ScopeRef{ScopeLevel::Bar, 0, 0, {}}, true);
  }).ok());
  TakeHit inBar0;
  inBar0.ppqOn = 2.0;
  inBar0.note = 39;
  auto locked = rig.s.mergeTake({inBar0}, o);
  CHECK(locked.ok() && locked.value().skippedLocked == 1);
  // replace mode removes unlocked events in the slot (bar 1) before adding
  TakeHit inBar1;
  inBar1.ppqOn = 4.0;  // bar 1, tick 0
  inBar1.note = 36;
  TakeOptions rep;
  rep.merge = 1;
  auto replaced = rig.s.mergeTake({inBar1}, rep);
  CHECK(replaced.ok() && replaced.value().added == 1);
  int atZero = 0;
  for (const auto& bt : rig.s.pattern()->bars[1].beats)
    for (const Event& e : bt.events) atZero += e.start == 0;
  CHECK_EQ(atZero, 1);
}

CHOP_TEST(merge_take_is_refused_while_scenes_play_and_gate_duration_is_quantized_and_clipped) {
  Rig rig;
  TakeHit h;
  h.ppqOn = 3.5;  // late in bar 0
  h.ppqOff = 40.0;  // an absurdly long gate: clipped to the bar
  h.note = 40;
  TakeOptions o;
  o.mode = render::PadMode::Gate;
  auto r = rig.s.mergeTake({h}, o);
  CHECK(r.ok() && r.value().added == 1);
  for (const auto& bt : rig.s.pattern()->bars[0].beats)
    for (const Event& e : bt.events)
      if (e.userOwned) CHECK(e.start + e.duration <= pattern::barTicks(rig.s.pattern()->settings));
  CHECK(rig.s.captureScene().ok());
  rig.s.onTransportChanged(true);
  CHECK(rig.s.playScenes(0, Quantize::Immediate).ok());
  auto refused = rig.s.mergeTake({h}, o);
  CHECK(!refused.ok());
  ProjectSession empty;
  CHECK(!empty.mergeTake({h}, o).ok());
  CHECK(rig.s.stopScenes().ok());
}

CHOP_TEST(pads_section_roundtrips_is_absent_for_defaults_and_damage_is_rejected) {
  Rig rig;
  auto plain = rig.s.saveState(true);
  CHECK(plain.ok());
  auto dec = codec::decode(plain.value().data(), plain.value().size());
  CHECK(dec.ok() && dec.value().modules.count("pads") == 0);
  PadSettings ps;
  ps.baseNote = 48;
  ps.midiChannel = 3;
  ps.quantizeDivision = 8;
  ps.quantizeTriplet = true;
  ps.merge = 1;
  CHECK(rig.s.setPadSettings(ps).ok());
  CHECK(rig.s.setChokeGroup(rig.s.chops()->chops[2].id, 2).ok());
  auto saved = rig.s.saveState(true);
  CHECK(saved.ok());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(t.padSettings() == rig.s.padSettings());
  auto st = codec::decode(saved.value().data(), saved.value().size());
  CHECK(st.ok());
  for (std::size_t cut = 0; cut + 1 < st.value().modules["pads"].bytes.size(); ++cut) {
    auto copy = st.value();
    copy.modules["pads"].bytes.resize(cut);
    auto enc = codec::encode(copy);
    CHECK(enc.ok());
    ProjectSession u;
    CHECK(!u.loadState(enc.value().data(), enc.value().size(), nullptr).ok());
    CHECK(!u.hasSource());
  }
  auto bad = st.value();
  bad.modules["pads"].bytes[0] = 200;  // base note out of range
  auto enc = codec::encode(bad);
  ProjectSession v;
  CHECK(!v.loadState(enc.value().data(), enc.value().size(), nullptr).ok());
}
