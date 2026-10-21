// F07: pad bank publication, pad settings section and take merge.
#include <chopfractal/composition/project_session.hpp>

#include <vector>

#include "chop_test.hpp"
#include "state_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {
// Detected chops and a two-bar pattern emptied of events, so every hit lands on a known grid position.
bool emptySession(ProjectSession& s, int maxEvents = 512, int bars = 2) {
  if (!statefix::loadAndDetect(s)) return false;
  pattern::Settings ps = statefix::patternSettings(42, bars);
  ps.maxEvents = maxEvents;
  if (!s.generate(ps).ok()) return false;
  return s
      .edit([](const pattern::Pattern& p, const ChopSnapshot&) -> Result<pattern::Pattern> {
        pattern::Pattern q = p;
        for (pattern::Bar& b : q.bars)
          for (pattern::Beat& beat : b.beats) beat.events.clear();
        return q;
      })
      .ok();
}

std::vector<const Event*> allEvents(const pattern::Pattern& p) {
  std::vector<const Event*> out;
  for (const pattern::Bar& b : p.bars)
    for (const pattern::Beat& beat : b.beats)
      for (const Event& e : beat.events) out.push_back(&e);
  return out;
}

const Event* eventAt(const pattern::Pattern& p, int bar, Ticks start) {
  for (const pattern::Beat& beat : p.bars[static_cast<std::size_t>(bar)].beats)
    for (const Event& e : beat.events)
      if (e.start == start) return &e;
  return nullptr;
}

TakeHit hit(double ppq, std::uint8_t note, std::uint8_t vel = 100, double off = -1.0) {
  TakeHit h;
  h.ppqOn = ppq;
  h.ppqOff = off;
  h.note = note;
  h.velocity = vel;
  return h;
}
}  // namespace

CHOP_TEST(pad_bank_maps_chops_in_snapshot_order_from_the_base_note) {
  ProjectSession s;
  CHECK(statefix::loadAndDetect(s));
  const auto n = static_cast<int>(s.chops()->chops.size());
  CHECK(n >= 3);
  auto bank = s.padBank();
  CHECK(bank != nullptr);
  if (!bank) return;
  for (int i = 0; i < render::kPadNotes; ++i) {
    const bool mapped = i >= 36 && i < 36 + n;
    CHECK_EQ(!bank->slots[static_cast<std::size_t>(i)].region.empty(), mapped);
    if (mapped) CHECK(bank->slots[static_cast<std::size_t>(i)].region.start == s.chops()->chops[static_cast<std::size_t>(i - 36)].range.start);
  }
  PadSettings ps;
  ps.baseNote = 111;  // room for 17 notes: every chop that fits is mapped, none beyond 127
  CHECK(s.setPadSettings(ps).ok());
  bank = s.padBank();
  int mapped = 0;
  for (int i = 0; i < render::kPadNotes; ++i) mapped += bank->slots[static_cast<std::size_t>(i)].region.empty() ? 0 : 1;
  CHECK_EQ(mapped, std::min(n, 128 - 111));
}

CHOP_TEST(bank_is_republished_after_marker_edits_roles_and_settings) {
  ProjectSession s;
  render::Mailbox<render::PadBank> box;
  s.attachPadMailbox(&box);
  CHECK(statefix::loadAndDetect(s));
  const render::PadBank* b = box.acquire();
  CHECK(b != nullptr);
  if (!b) return;
  CHECK(!b->slots[36].region.empty());

  PadSettings ps;
  ps.baseNote = 40;
  CHECK(s.setPadSettings(ps).ok());
  b = box.acquire();
  CHECK(b && b->slots[36].region.empty() && !b->slots[40].region.empty());

  // A marker edit changes the chop layout; the mailbox carries the new regions.
  const auto before = s.chops()->chops.size();
  CHECK(!s.chopMap()->markers().empty());
  const auto firstMarker = s.chopMap()->markers().front().id;
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(firstMarker); }).ok());
  CHECK(s.chops()->chops.size() < before);
  b = box.acquire();
  int mapped = 0;
  for (int i = 0; i < render::kPadNotes; ++i) mapped += b->slots[static_cast<std::size_t>(i)].region.empty() ? 0 : 1;
  CHECK_EQ(mapped, static_cast<int>(s.chops()->chops.size()));

  // A role assignment reaches the bank only through a publish: the next playback publish carries it.
  CHECK(s.assignRole(s.chops()->chops.front().id, "hat").ok());
  CHECK(s.generate(statefix::patternSettings(7, 1)).ok());
  b = box.acquire();
  CHECK_EQ(static_cast<int>(b->slots[40].chokeGroup), 1);
}

CHOP_TEST(hat_role_defaults_to_choke_group_1_and_override_wins) {
  ProjectSession s;
  CHECK(statefix::loadAndDetect(s));
  const ChopId first = s.chops()->chops.front().id;
  CHECK(s.assignRole(first, "hat").ok());
  CHECK(s.setPadSettings(s.padSettings()).ok());
  CHECK(s.generate(statefix::patternSettings(7, 1)).ok());  // publish: the role is applied
  CHECK_EQ(static_cast<int>(s.padBank()->slots[36].chokeGroup), 1);

  CHECK(s.setChokeGroup(first, 3).ok());
  CHECK_EQ(static_cast<int>(s.padBank()->slots[36].chokeGroup), 3);
  CHECK(s.section(kTagPads) != nullptr);

  CHECK(s.setChokeGroup(first, 0).ok());  // an explicit "off" overrides the hat default
  CHECK_EQ(static_cast<int>(s.padBank()->slots[36].chokeGroup), 0);

  CHECK(s.setChokeGroup(first, 1).ok());  // equal to the default again: the override is dropped
  CHECK(s.padSettings().chokeOverride.empty());
  CHECK(s.section(kTagPads) == nullptr);
  CHECK(!s.setChokeGroup(first, 5).ok());
  CHECK(!s.setChokeGroup(ChopId{999999}, 1).ok());
}

CHOP_TEST(merge_take_quantizes_wraps_and_adds_user_owned_events_in_one_undo_step) {
  ProjectSession s;
  CHECK(emptySession(s));
  CHECK(pattern::lengthTicks(*s.pattern()) == 7680);
  const ChopId c0 = s.chops()->chops[0].id;
  TakeOptions opt;
  auto r = s.mergeTake({hit(1.04, 36), hit(7.95, 36)}, opt);
  CHECK(r.ok());
  if (!r.ok()) return;
  CHECK_EQ(r.value().added, 2);
  const pattern::Pattern& p = *s.pattern();
  const Event* a = eventAt(p, 0, 960);
  const Event* b = eventAt(p, 0, 0);
  CHECK(a != nullptr && b != nullptr);
  if (!a || !b) return;
  CHECK(a->userOwned && b->userOwned && a->chop == c0 && b->chop == c0);
  CHECK_EQ(a->duration, 240);
  CHECK_EQ(allEvents(p).size(), static_cast<std::size_t>(2));
  CHECK(s.undo());
  CHECK_EQ(allEvents(*s.pattern()).size(), static_cast<std::size_t>(0));
}

CHOP_TEST(merge_take_levels_follow_the_velocity_curve) {
  ProjectSession s;
  CHECK(emptySession(s));
  TakeOptions opt;
  opt.curve = render::VelocityCurve::Hard;
  CHECK(s.mergeTake({hit(0.0, 36, 64), hit(1.0, 37, 127)}, opt).ok());
  const Event* a = eventAt(*s.pattern(), 0, 0);
  const Event* b = eventAt(*s.pattern(), 0, 960);
  CHECK(a && b);
  if (!a || !b) return;
  CHECK_NEAR(a->tx.level, render::velocityGain(render::VelocityCurve::Hard, 64), 1e-6);
  CHECK_NEAR(b->tx.level, 1.0, 1e-6);
}

CHOP_TEST(merge_take_respects_locks_cap_and_dedupes) {
  {
    ProjectSession s;
    CHECK(emptySession(s, 4));
    TakeOptions opt;
    std::vector<TakeHit> hits;
    for (int i = 0; i < 6; ++i) hits.push_back(hit(0.25 * i + 0.0, 36));  // six distinct 1/16 slots, cap is 4
    auto r = s.mergeTake(hits, opt);
    CHECK(r.ok());
    if (r.ok()) {
      CHECK_EQ(r.value().added, 4);
      CHECK_EQ(r.value().skippedCap, 2);
    }
    CHECK_EQ(allEvents(*s.pattern()).size(), static_cast<std::size_t>(4));
  }
  {
    ProjectSession s;
    CHECK(emptySession(s));
    TakeOptions opt;
    CHECK(s.mergeTake({hit(0.0, 36)}, opt).ok());
    auto again = s.mergeTake({hit(0.02, 36), hit(0.0, 99)}, opt);  // same chop and slot; a note with no chop
    CHECK(again.ok());
    if (again.ok()) {
      CHECK_EQ(again.value().added, 0);
      CHECK_EQ(again.value().skippedDuplicate, 1);
      CHECK_EQ(again.value().skippedUnmapped, 1);
    }
    CHECK_EQ(allEvents(*s.pattern()).size(), static_cast<std::size_t>(1));
  }
  {
    ProjectSession s;
    CHECK(emptySession(s));
    CHECK(s.edit([](const pattern::Pattern& p, const ChopSnapshot&) {
               pattern::ScopeRef ref;
               ref.level = ScopeLevel::Bar;
               ref.bar = 0;
               return pattern::setLock(p, ref, true);
             })
              .ok());
    TakeOptions opt;
    auto r = s.mergeTake({hit(0.0, 36), hit(4.0, 36)}, opt);  // bar 0 is locked, bar 1 is free
    CHECK(r.ok());
    if (r.ok()) {
      CHECK_EQ(r.value().skippedLocked, 1);
      CHECK_EQ(r.value().added, 1);
    }
    CHECK(eventAt(*s.pattern(), 1, 0) != nullptr);
  }
}

CHOP_TEST(replace_mode_deletes_unlocked_events_in_the_same_slot_only) {
  ProjectSession s;
  CHECK(emptySession(s));
  TakeOptions add;
  CHECK(s.mergeTake({hit(1.0, 36), hit(1.25, 36)}, add).ok());  // 960 and 1200
  CHECK_EQ(allEvents(*s.pattern()).size(), static_cast<std::size_t>(2));
  TakeOptions rep;
  rep.merge = 1;
  auto r = s.mergeTake({hit(1.0, 37)}, rep);
  CHECK(r.ok());
  if (!r.ok()) return;
  CHECK_EQ(r.value().replaced, 1);
  CHECK_EQ(r.value().added, 1);
  const Event* at960 = eventAt(*s.pattern(), 0, 960);
  const Event* at1200 = eventAt(*s.pattern(), 0, 1200);
  CHECK(at960 && at1200);
  if (at960) CHECK(at960->chop == s.chops()->chops[1].id);
  if (at1200) CHECK(at1200->chop == s.chops()->chops[0].id);
}

CHOP_TEST(gate_duration_is_quantized_and_clipped_to_the_bar) {
  ProjectSession s;
  CHECK(emptySession(s));
  TakeOptions opt;
  opt.mode = render::PadMode::Gate;
  // 0.3 quarter = 288 ticks: rounded up to two 1/16 cells. The second hit starts 240 ticks before the bar end.
  CHECK(s.mergeTake({hit(0.0, 36, 100, 0.3), hit(3.75, 37, 100, 4.5)}, opt).ok());
  const Event* a = eventAt(*s.pattern(), 0, 0);
  const Event* b = eventAt(*s.pattern(), 0, 3600);
  CHECK(a && b);
  if (a) CHECK_EQ(a->duration, 480);
  if (b) CHECK_EQ(b->duration, 240);
}

CHOP_TEST(empty_or_unchanged_merge_creates_no_undo_entry) {
  ProjectSession s;
  CHECK(emptySession(s));
  TakeOptions opt;
  CHECK(s.mergeTake({hit(0.0, 36)}, opt).ok());
  CHECK(s.mergeTake({}, opt).ok());
  CHECK(s.mergeTake({hit(0.0, 36)}, opt).ok());  // a duplicate: nothing changes
  CHECK(s.undo());
  CHECK_EQ(allEvents(*s.pattern()).size(), static_cast<std::size_t>(0));  // one undo reverts the only real merge

  ProjectSession none;
  CHECK(!none.mergeTake({hit(0.0, 36)}, opt).ok());  // no pattern yet
}

CHOP_TEST(pads_section_roundtrips_rejects_bad_ranges_and_is_absent_for_defaults) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  const auto plain = a.saveState(false).value();
  CHECK(a.section(kTagPads) == nullptr);

  PadSettings ps;
  ps.baseNote = 48;
  ps.midiChannel = 3;
  ps.quantizeDivision = 8;
  ps.quantizeTriplet = true;
  ps.merge = 1;
  ps.chokeOverride[a.chops()->chops[1].id.value] = 2;
  CHECK(a.setPadSettings(ps).ok());
  CHECK(a.section(kTagPads) != nullptr);
  const auto saved = a.saveState(false).value();
  CHECK(saved != plain);

  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), [&](const source::SourceInfo&) { return statefix::drumLoop(); }).ok());
  CHECK(b.padSettings() == ps);
  CHECK_EQ(static_cast<int>(b.padBank()->slots[49].chokeGroup), 2);
  CHECK(b.saveState(false).value() == saved);

  // Going back to the defaults removes the section and restores the original bytes.
  CHECK(a.setPadSettings(PadSettings{}).ok());
  CHECK(a.section(kTagPads) == nullptr);
  CHECK(a.saveState(false).value() == plain);

  // Range checks.
  PadSettings bad;
  bad.baseNote = 112;
  CHECK(!a.setPadSettings(bad).ok());
  bad = PadSettings{};
  bad.midiChannel = 17;
  CHECK(!a.setPadSettings(bad).ok());
  bad = PadSettings{};
  bad.quantizeDivision = 12;
  CHECK(!a.setPadSettings(bad).ok());
  bad = PadSettings{};
  bad.chokeOverride[1] = 5;
  CHECK(!a.setPadSettings(bad).ok());
  CHECK(a.padSettings() == PadSettings{});
}

CHOP_TEST(hostile_pads_sections_are_dropped_with_a_warning_and_leave_defaults) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  PadSettings ps;
  ps.baseNote = 48;
  ps.chokeOverride[5] = 2;
  ps.chokeOverride[9] = 4;
  CHECK(a.setPadSettings(ps).ok());
  const StateSection good = *a.section(kTagPads);
  auto resolver = [](const source::SourceInfo&) { return statefix::drumLoop(); };

  auto tryLoad = [&](const StateSection& sec) {
    ProjectSession w;
    CHECK(statefix::fixtureSession(w));
    CHECK(w.setSection(sec).ok());
    const auto bytes = w.saveState(false).value();
    ProjectSession r;
    CHECK(r.loadState(bytes.data(), bytes.size(), resolver).ok());
    return std::pair<bool, PadSettings>(r.section(kTagPads) != nullptr, r.padSettings());
  };

  // Every truncation (including empty trailing and extra bytes) is refused.
  for (std::size_t n = 1; n < good.bytes.size(); ++n) {
    StateSection t = good;
    t.bytes.resize(n);
    auto res = tryLoad(t);
    CHECK(!res.first);
    CHECK(res.second == PadSettings{});
  }
  StateSection extra = good;
  extra.bytes.push_back(0);
  CHECK(!tryLoad(extra).first);
  StateSection schema = good;
  schema.schema = 9;
  CHECK(!tryLoad(schema).first);

  // Byte flips either decode to valid settings or are dropped; nothing is half applied.
  for (std::size_t i = 0; i < good.bytes.size(); ++i) {
    StateSection t = good;
    t.bytes[i] = 0xFF;
    auto res = tryLoad(t);
    CHECK(res.first || res.second == PadSettings{});
  }
  // The good section still loads.
  auto res = tryLoad(good);
  CHECK(res.first && res.second == ps);
}
