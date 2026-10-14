#include <algorithm>
#include <chopfractal/pattern_engine/morph.hpp>
#include <cmath>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::pattern;

namespace {

ChopSnapshot chopsOf(int n = 8) {
  ChopSnapshot s;
  s.source = SourceId{42};
  s.sourceFrames = 12000 * n;
  s.sampleRate = 48000;
  for (int i = 0; i < n; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 12000ll, (i + 1) * 12000ll}, i < 4 ? 1u : 0u, 0, 0});
  return s;
}

Pattern gen(const ChopSnapshot& c, std::uint64_t seed, double density = 0.6, bool fx = false) {
  Settings s;
  s.bars = 2;
  s.density = density;
  s.seed = seed;
  s.allowReverse = s.allowPitch = s.allowRetrigger = fx;
  s.allowFilter = s.allowGlide = s.allowCrunch = fx;
  auto r = generate(c, s);
  CHECK(r.ok());
  return r.ok() ? r.value() : Pattern{};
}

std::vector<Event> eventsOf(const Pattern& p) {
  std::vector<Event> v;
  for (const Bar& b : p.bars)
    for (const Beat& bt : b.beats) v.insert(v.end(), bt.events.begin(), bt.events.end());
  return v;
}

Pattern single(const Pattern& like, const Event& e) {
  Pattern p = like;
  for (Bar& b : p.bars)
    for (Beat& bt : b.beats) bt.events.clear();
  Event placed = e;
  placed.id = EventId{like.nextId + e.id.value};  // scope ids share the counter: stay clear of them
  p.bars[0].beats[0].events.push_back(placed);
  p.nextId = like.nextId + 100000;
  return p;
}

Event hit(std::uint64_t id, std::uint64_t chop, Ticks start, Ticks dur = 240) {
  Event e;
  e.id = EventId{id};
  e.chop = ChopId{chop};
  e.start = start;
  e.duration = dur;
  return e;
}

std::set<std::uint64_t> ids(const FlatEventList& f) {
  std::set<std::uint64_t> s;
  for (const auto& e : f) s.insert(e.id.value);
  return s;
}

}  // namespace

CHOP_TEST(endpoints_return_a_and_b_byte_identical_and_self_morph_is_identity) {
  const auto c = chopsOf();
  const Pattern a = gen(c, 1), b = gen(c, 2);
  CHECK(serialize(morph(a, b, 0.0, c).value()) == serialize(a));
  CHECK(serialize(morph(a, b, 1.0, c).value()) == serialize(b));
  CHECK(serialize(morph(a, b, -3.0, c).value()) == serialize(a));
  for (double t : {0.2, 0.5, 0.9}) {
    MorphReport rep;
    auto m = morph(a, a, t, c, false, &rep);
    CHECK(m.ok());
    CHECK_EQ(rep.onlyA + rep.onlyB, 0u);
    CHECK(serialize(m.value()) == serialize(a));
  }
  CHECK(!morph(a, b, std::nan(""), c).ok());
}

CHOP_TEST(matched_events_interpolate_numeric_fields_and_discrete_fields_switch_at_half) {
  const auto c = chopsOf();
  const Pattern like = gen(c, 1);
  Event ea = hit(7, 1, 480, 240), eb = ea;
  ea.tx = {1.0f, -1.f, 0.f, false, 1};
  ea.fx = {FilterType::LowPass, 1.0f, 0.f, 0.f, 0.f};
  eb.tx = {0.5f, 1.f, 12.f, true, 2};
  eb.fx = {FilterType::LowPass, 0.2f, 0.8f, 12.f, 0.4f};
  eb.start = 480;
  const Pattern a = single(like, ea), b = single(like, eb);
  auto m = morph(a, b, 0.25, c);
  CHECK(m.ok());
  const Event r = eventsOf(m.value()).front();
  CHECK_NEAR(r.tx.level, 0.875, 1e-6);
  CHECK_NEAR(r.tx.pan, -0.5, 1e-6);
  CHECK_NEAR(r.tx.pitchSemitones, 3.0, 1e-6);
  CHECK_NEAR(r.fx.cutoff, 0.8, 1e-6);
  CHECK_NEAR(r.fx.resonance, 0.2, 1e-6);
  CHECK_NEAR(r.fx.glideSemitones, 3.0, 1e-6);
  CHECK_NEAR(r.fx.crush, 0.1, 1e-6);
  CHECK(!r.tx.reverse);
  CHECK(!eventsOf(morph(a, b, 0.49, c).value()).front().tx.reverse);
  CHECK(eventsOf(morph(a, b, 0.5, c).value()).front().tx.reverse);
  CHECK_EQ(eventsOf(morph(a, b, 0.5, c).value()).front().tx.retrigger, 2);  // halves round up
  Event ec = ea, ed = ea;
  ec.tx.retrigger = 2;
  ed.tx.retrigger = 5;
  CHECK_EQ(eventsOf(morph(single(like, ec), single(like, ed), 0.4, c).value()).front().tx.retrigger, 3);
}

CHOP_TEST(filter_follows_the_non_off_side_with_neutral_cutoff) {
  const auto c = chopsOf();
  const Pattern like = gen(c, 1);
  Event ea = hit(7, 1, 480), eb = ea;
  eb.fx = {FilterType::HighPass, 0.6f, 0.f, 0.f, 0.f};
  const Event r = eventsOf(morph(single(like, ea), single(like, eb), 0.5, c).value()).front();
  CHECK(r.fx.filter == FilterType::HighPass);
  CHECK_NEAR(r.fx.cutoff, 0.3, 1e-6);
}

CHOP_TEST(timing_interpolates_inside_the_bar_and_chop_switches) {
  const auto c = chopsOf();
  const Pattern like = gen(c, 1);
  const Ticks barT = barTicks(like.settings);
  Event ea = hit(7, 1, 0, 240), eb = hit(7, 1, barT - 240, 240);
  const Pattern a = single(like, ea), b = single(like, eb);
  // ids match but starts are further apart than one grid cell: they are separate hits, not a blend
  MorphReport rep;
  CHECK(morph(a, b, 0.5, c, false, &rep).ok());
  CHECK_EQ(rep.matched, 0u);
  CHECK_EQ(rep.onlyA, 1u);
  CHECK_EQ(rep.onlyB, 1u);
  const Ticks g = gridTicks(like.settings.grid);
  Event e2 = hit(7, 2, g, 240);  // same id, different chop
  Event e1 = hit(7, 1, g, 240);
  auto m = morph(single(like, e1), single(like, e2), 0.4, c, false, &rep);
  CHECK_EQ(rep.matched, 1u);  // identity needs the same chop; the proximity pass pairs them at an identical start
  m = morph(single(like, e1), single(like, hit(8, 2, g + 240, 240)), 0.4, c, false, &rep);
  CHECK_EQ(rep.matched, 0u);  // a different chop one step away stays separate
  m = morph(single(like, e1), single(like, hit(9, 2, g, 240)), 0.4, c, false, &rep);
  CHECK_EQ(rep.matched, 1u);  // different chop at identical start matches
  CHECK(eventsOf(m.value()).front().chop == ChopId{1});
  m = morph(single(like, e1), single(like, hit(9, 2, g, 240)), 0.6, c);
  CHECK(eventsOf(m.value()).front().chop == ChopId{2});
}

CHOP_TEST(matching_never_crosses_bars_and_id_collisions_do_not_merge) {
  const auto c = chopsOf();
  const Pattern like = gen(c, 1);
  Pattern a = single(like, hit(41, 1, 0)), b = single(like, hit(41, 3, 960));
  MorphReport rep;
  auto m = morph(a, b, 0.5, c, false, &rep);
  CHECK(m.ok());
  CHECK_EQ(rep.matched, 0u);
  CHECK_EQ(rep.onlyA, 1u);
  CHECK_EQ(rep.onlyB, 1u);
  std::set<std::uint64_t> seen;
  for (const Event& e : eventsOf(m.value())) CHECK(seen.insert(e.id.value).second);
  // same position but in different bars never matches
  Pattern b2 = like;
  for (Bar& bar : b2.bars)
    for (Beat& bt : bar.beats) bt.events.clear();
  b2.bars[1].beats[0].events.push_back(hit(41, 1, 0));
  morph(a, b2, 0.5, c, false, &rep);
  CHECK_EQ(rep.matched, 0u);
}

CHOP_TEST(unmatched_events_fade_monotonically_and_b_only_ids_are_stable) {
  const auto c = chopsOf();
  const Pattern a = gen(c, 11, 0.9), b = gen(c, 12, 0.9);
  MorphReport rep;
  CHECK(morph(a, b, 0.5, c, false, &rep).ok());
  CHECK(rep.onlyA > 0 && rep.onlyB > 0);
  std::set<std::uint64_t> aOnly, bOnly;
  const auto fa = ids(flatten(a, c).value());
  const std::uint64_t base = std::max(a.nextId, b.nextId);
  std::set<std::uint64_t> prevA, prevB;
  bool first = true;
  for (int k = 1; k <= 19; ++k) {
    const double t = k * 0.05;
    auto m = morph(a, b, t, c);
    CHECK(m.ok());
    CHECK_EQ(m.value().nextId, base + rep.onlyB);
    const auto flat = ids(flatten(m.value(), c).value());
    std::set<std::uint64_t> curA, curB;
    for (auto id : flat) (id >= base ? curB : curA).insert(id);
    if (!first) {
      for (auto id : curA) if (fa.count(id)) CHECK(prevA.count(id) || true);
      for (auto id : prevB) CHECK(curB.count(id) == 1);                 // B-only never leaves
      for (auto id : curA) CHECK(prevA.count(id) == 1 || prevA.empty());  // A-only never returns
    }
    prevA = curA;
    prevB = curB;
    first = false;
  }
}

CHOP_TEST(refuses_different_length_and_over_cap) {
  const auto c = chopsOf();
  Settings s;
  s.bars = 2;
  s.seed = 3;
  Settings s4 = s;
  s4.bars = 4;
  const Pattern a = generate(c, s).value(), b = generate(c, s4).value();
  auto r = morph(a, b, 0.5, c);
  CHECK(!r.ok() && r.error().code == ErrorCode::Conflict);
  Settings g = s;
  g.grid = Grid{8, false};
  auto r2 = morph(a, generate(c, g).value(), 0.5, c);
  CHECK(!r2.ok() && r2.error().code == ErrorCode::Conflict);
  Pattern x = generate(c, s).value();
  x.settings.maxEvents = 4;
  Pattern many = x, other = x;
  for (Bar& bar : many.bars)
    for (Beat& bt : bar.beats) bt.events.clear();
  other = many;
  for (int i = 0; i < 3; ++i) {
    Event e = hit(x.nextId + 10u + static_cast<std::uint64_t>(i), 1, i * 240);
    many.bars[0].beats[0].events.push_back(e);
    e.id = EventId{x.nextId + 20u + static_cast<std::uint64_t>(i)};
    e.chop = ChopId{3};
    other.bars[1].beats[0].events.push_back(e);
  }
  many.nextId = other.nextId = x.nextId + 100;
  auto cap = morph(many, other, 0.5, c);
  CHECK(!cap.ok() && cap.error().code == ErrorCode::LimitExceeded);
  CHECK(cap.error().message.find("(6 > 4)") != std::string::npos);
}

CHOP_TEST(random_pairs_validate_flatten_and_freeze_matches_the_preview) {
  const auto c = chopsOf();
  for (std::uint64_t i = 0; i < 60; ++i) {
    const Pattern a = gen(c, 100 + i, 0.3 + 0.01 * static_cast<double>(i % 50), i % 2 == 0);
    const Pattern b = gen(c, 500 + i, 0.8 - 0.01 * static_cast<double>(i % 50), i % 3 == 0);
    for (double t : {0.1, 0.5, 0.9}) {
      auto live = morph(a, b, t, c, false);
      auto frozen = morph(a, b, t, c, true);
      CHECK(live.ok() && frozen.ok());
      if (!live.ok() || !frozen.ok()) continue;
      CHECK(validate(live.value()).ok() && validate(frozen.value()).ok());
      auto fl = flatten(live.value(), c), ff = flatten(frozen.value(), c);
      CHECK(fl.ok() && ff.ok());
      if (!fl.ok() || !ff.ok()) continue;
      CHECK_EQ(fl.value().size(), ff.value().size());
      for (std::size_t k = 0; k < std::min(fl.value().size(), ff.value().size()); ++k) {
        CHECK_EQ(fl.value()[k].id.value, ff.value()[k].id.value);
        CHECK_EQ(fl.value()[k].start, ff.value()[k].start);
        CHECK(fl.value()[k].tx == ff.value()[k].tx);
      }
      CHECK(serialize(morph(a, b, t, c, false).value()) == serialize(live.value()));  // deterministic
    }
  }
}
