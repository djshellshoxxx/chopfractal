// F10 A/B morph: matching, interpolation table, probability crossfade, freeze equality and the refusal cases.
#include <algorithm>
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/pattern_engine/morph.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>
#include <cmath>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::pattern;

namespace {

ChopSnapshot makeChops(int n = 8) {
  ChopSnapshot s;
  s.source = SourceId{42};
  s.sourceFrames = 12000 * n;
  s.sampleRate = 48000;
  for (int i = 0; i < n; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 12000ll, (i + 1) * 12000ll}, 0, 0, 0});
  return s;
}

// A pattern with the given bars and no events, ready for hand placement.
Pattern emptyPattern(const ChopSnapshot& c, int bars = 1, std::uint64_t seed = 5) {
  Settings s;
  s.bars = bars;
  s.seed = seed;
  s.density = 0.5;
  Pattern p = generate(c, s).value();
  for (int b = 0; b < bars; ++b) p = setBarEvents(p, b, {}, c).value();
  return p;
}

EventId add(Pattern& p, int bar, std::uint64_t chop, Ticks start, Ticks dur = 240) {
  Event e;
  e.id = EventId{p.nextId++};
  e.chop = ChopId{chop};
  e.start = start;
  e.duration = dur;
  auto& beats = p.bars[static_cast<std::size_t>(bar)].beats;
  const std::size_t idx = std::min<std::size_t>(beats.size() - 1, static_cast<std::size_t>(start / ticksPerBeat(p.settings.timeSignature)));
  beats[idx].events.push_back(e);
  return e.id;
}

Event* mut(Pattern& p, EventId id) {
  for (Bar& b : p.bars)
    for (Beat& bt : b.beats)
      for (Event& e : bt.events)
        if (e.id == id) return &e;
  return nullptr;
}

std::vector<Event> all(const Pattern& p) {
  std::vector<Event> out;
  for (const Bar& b : p.bars)
    for (const Beat& bt : b.beats) out.insert(out.end(), bt.events.begin(), bt.events.end());
  return out;
}

Pattern morphOk(const Pattern& a, const Pattern& b, double t, const ChopSnapshot& c, bool freeze = false, MorphReport* r = nullptr) {
  auto m = morph(a, b, t, c, freeze, r);
  CHECK(m.ok());
  return m.ok() ? m.value() : Pattern{};
}

const Event* find(const std::vector<Event>& v, EventId id) {
  for (const Event& e : v)
    if (e.id == id) return &e;
  return nullptr;
}

Pattern generated(const ChopSnapshot& c, std::uint64_t seed, double density, bool fx) {
  Settings s;
  s.bars = 2;
  s.seed = seed;
  s.density = density;
  s.variation = {0.6, 0.6, 0.6, 0.7};
  s.allowReverse = s.allowPitch = s.allowRetrigger = fx;
  s.allowFilter = s.allowGlide = s.allowCrunch = fx;
  return generate(c, s).value();
}

}  // namespace

CHOP_TEST(endpoints_return_a_and_b_byte_identical) {
  const ChopSnapshot c = makeChops();
  const Pattern a = generated(c, 1, 0.6, true), b = generated(c, 2, 0.5, true);
  CHECK(serialize(morphOk(a, b, 0.0, c)) == serialize(a));
  CHECK(serialize(morphOk(a, b, 1.0, c)) == serialize(b));
  CHECK(serialize(morphOk(a, b, -3.0, c)) == serialize(a));
  CHECK(serialize(morphOk(a, b, 7.0, c)) == serialize(b));
  CHECK(!morph(a, b, std::nan(""), c).ok());
}

CHOP_TEST(morphing_a_pattern_with_itself_is_identity_for_any_t) {
  const ChopSnapshot c = makeChops();
  for (std::uint64_t seed = 1; seed <= 6; ++seed) {
    const Pattern a = generated(c, seed, 0.6, seed % 2 == 0);
    for (double t : {0.1, 0.5, 0.9}) {
      MorphReport r;
      const Pattern m = morphOk(a, a, t, c, false, &r);
      CHECK(serialize(m) == serialize(a));
      CHECK(r.onlyA == 0 && r.onlyB == 0 && r.matched == eventCount(a));
    }
  }
}

CHOP_TEST(matched_events_interpolate_numeric_fields) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c);
  const EventId id = add(a, 0, 1, 0, 480);
  Event* ea = mut(a, id);
  ea->tx.level = 1.0f;
  ea->tx.pan = -1.f;
  ea->tx.pitchSemitones = 0.f;
  ea->fx.filter = FilterType::LowPass;
  ea->fx.cutoff = 1.0f;
  ea->fx.resonance = 0.f;
  ea->fx.glideSemitones = 0.f;
  ea->fx.crush = 0.f;
  Pattern b = a;
  Event* eb = mut(b, id);
  eb->tx.level = 0.5f;
  eb->tx.pan = 1.f;
  eb->tx.pitchSemitones = 12.f;
  eb->fx.cutoff = 0.2f;
  eb->fx.resonance = 0.8f;
  eb->fx.glideSemitones = 12.f;
  eb->fx.crush = 0.4f;
  MorphReport r;
  const Pattern m = morphOk(a, b, 0.25, c, false, &r);
  CHECK(r.matched == 1 && r.onlyA == 0 && r.onlyB == 0);
  const Event* e = find(all(m), id);
  CHECK(e != nullptr);
  if (!e) return;
  CHECK_NEAR(e->tx.level, 0.875, 1e-6);
  CHECK_NEAR(e->tx.pan, -0.5, 1e-6);
  CHECK_NEAR(e->tx.pitchSemitones, 3.0, 1e-6);
  CHECK_NEAR(e->fx.cutoff, 0.8, 1e-6);
  CHECK_NEAR(e->fx.resonance, 0.2, 1e-6);
  CHECK_NEAR(e->fx.glideSemitones, 3.0, 1e-6);
  CHECK_NEAR(e->fx.crush, 0.1, 1e-6);
  CHECK(e->fx.filter == FilterType::LowPass);
}

CHOP_TEST(discrete_fields_switch_at_half) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c);
  const EventId id = add(a, 0, 1, 0, 480);
  Pattern b = a;
  Event* eb = mut(b, id);
  eb->tx.reverse = true;
  eb->chop = ChopId{1};  // same chop: the chop does not move
  eb->enabled = false;
  eb->echo = EchoFx{2, 1, 50};
  for (double t : {0.1, 0.49}) {
    const Event* e = find(all(morphOk(a, b, t, c)), id);
    CHECK(e && !e->tx.reverse && e->enabled && !e->echo);
  }
  for (double t : {0.5, 0.9}) {
    const Event* e = find(all(morphOk(a, b, t, c)), id);
    CHECK(e && e->tx.reverse && !e->enabled && e->echo && e->echo->repeats == 2);
  }
  // a different chop at identical start still matches and switches at half
  Pattern b2 = a;
  mut(b2, id)->chop = ChopId{3};
  CHECK(find(all(morphOk(a, b2, 0.49, c)), id)->chop == ChopId{1});
  CHECK(find(all(morphOk(a, b2, 0.5, c)), id)->chop == ChopId{3});
}

CHOP_TEST(retrigger_rounds_halves_up) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c);
  const EventId id = add(a, 0, 1, 0, 480);
  Pattern b = a;
  mut(a, id)->tx.retrigger = 1;
  mut(b, id)->tx.retrigger = 2;
  CHECK(find(all(morphOk(a, b, 0.5, c)), id)->tx.retrigger == 2);
  mut(a, id)->tx.retrigger = 2;
  mut(b, id)->tx.retrigger = 5;
  CHECK(find(all(morphOk(a, b, 0.4, c)), id)->tx.retrigger == 3);
  mut(a, id)->tx.retrigger = 1;
  mut(b, id)->tx.retrigger = 8;
  CHECK(find(all(morphOk(a, b, 0.99, c)), id)->tx.retrigger == 8);
}

CHOP_TEST(filter_type_follows_the_non_off_side_with_neutral_cutoff) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c);
  const EventId id = add(a, 0, 1, 0, 480);
  Pattern b = a;
  mut(b, id)->fx.filter = FilterType::HighPass;
  mut(b, id)->fx.cutoff = 0.6f;
  const Event* e = find(all(morphOk(a, b, 0.5, c)), id);
  CHECK(e && e->fx.filter == FilterType::HighPass);
  if (e) CHECK_NEAR(e->fx.cutoff, 0.3, 1e-6);
  // B off, A low-pass 0.4: low-pass, neutral 1.0 on the B side
  Pattern a2 = a, b2 = a;
  mut(a2, id)->fx.filter = FilterType::LowPass;
  mut(a2, id)->fx.cutoff = 0.4f;
  e = find(all(morphOk(a2, b2, 0.5, c)), id);
  CHECK(e && e->fx.filter == FilterType::LowPass);
  if (e) CHECK_NEAR(e->fx.cutoff, 0.7, 1e-6);
  // two different filters switch
  Pattern a3 = a2, b3 = a2;
  mut(b3, id)->fx.filter = FilterType::HighPass;
  mut(b3, id)->fx.cutoff = 0.9f;
  CHECK(find(all(morphOk(a3, b3, 0.49, c)), id)->fx.filter == FilterType::LowPass);
  CHECK(find(all(morphOk(a3, b3, 0.5, c)), id)->fx.filter == FilterType::HighPass);
}

CHOP_TEST(start_and_duration_interpolate_and_stay_inside_the_bar) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c);
  const EventId id = add(a, 0, 1, 0, 480);
  Pattern b = a;
  mut(b, id)->start = 240;  // a matched hit moves at most one grid cell
  mut(b, id)->duration = 840;
  const Event* e = find(all(morphOk(a, b, 0.5, c)), id);
  CHECK(e && e->start == 120 && e->duration == 660);
  const Pattern m = morphOk(a, b, 0.99, c);
  CHECK(validate(m).ok());
  for (const Event& x : all(m)) CHECK(x.start >= 0 && x.duration >= 1 && x.start + x.duration <= barTicks(m.settings));
}

CHOP_TEST(region_interpolates_only_for_the_same_chop_and_stays_in_range) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c);
  const EventId id = add(a, 0, 1, 0, 480);
  Pattern b = a;
  mut(a, id)->region = {0, 6000};
  mut(b, id)->region = {2000, 12000};
  const Event* e = find(all(morphOk(a, b, 0.5, c)), id);
  CHECK(e && e->region.start == 1000 && e->region.end == 9000);
  mut(b, id)->region = {};  // only one side set: the chosen side's region
  CHECK(find(all(morphOk(a, b, 0.25, c)), id)->region == (SampleRange{0, 6000}));
  CHECK(find(all(morphOk(a, b, 0.75, c)), id)->region.empty());
  mut(a, id)->region = {};
  CHECK(find(all(morphOk(a, b, 0.5, c)), id)->region.empty());  // both empty
  // different chops: the chosen side's region
  Pattern a3 = a, b3 = a;
  mut(a3, id)->region = {100, 5000};
  mut(b3, id)->chop = ChopId{2};
  mut(b3, id)->region = {12100, 13000};
  CHECK(find(all(morphOk(a3, b3, 0.4, c)), id)->region == (SampleRange{100, 5000}));
  CHECK(find(all(morphOk(a3, b3, 0.6, c)), id)->region == (SampleRange{12100, 13000}));
}

CHOP_TEST(matching_prefers_identical_id_then_nearest_start_and_never_crosses_bars) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c, 2);
  Pattern b = emptyPattern(c, 2);
  const EventId x = add(a, 0, 1, 0);
  const EventId y = add(a, 1, 1, 0);  // same position in bar 2
  b.nextId = a.nextId;
  Event ex;
  ex.id = x;  // same id, same chop, 100 ticks away: identity pass
  ex.chop = ChopId{1};
  ex.start = 100;
  ex.duration = 240;
  b.bars[0].beats[0].events.push_back(ex);
  Event other;  // unrelated id close to A's event of bar 2 but in bar 1 of B: never matches across bars
  other.id = EventId{b.nextId++};
  other.chop = ChopId{1};
  other.start = 0;
  other.duration = 240;
  b.bars[0].beats[0].events.push_back(other);
  MorphReport r;
  const Pattern m = morphOk(a, b, 0.5, c, false, &r);
  CHECK(r.matched == 1 && r.onlyA == 1 && r.onlyB == 1);
  const Event* e = find(all(m), x);
  CHECK(e && e->start == 50);
  CHECK(find(all(m), y) != nullptr);
  // proximity pass: no shared ids, one grid cell apart
  Pattern a2 = emptyPattern(c), b2 = emptyPattern(c);
  add(a2, 0, 1, 480);
  add(b2, 0, 1, 480 + 240);  // exactly one 16th away: within the tolerance
  add(b2, 0, 1, 480 + 480);  // two away: too far
  MorphReport r2;
  morphOk(a2, b2, 0.5, c, false, &r2);
  CHECK(r2.matched == 1 && r2.onlyB == 1 && r2.onlyA == 0);
}

CHOP_TEST(different_chop_matches_only_at_identical_start) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c), b = emptyPattern(c);
  add(a, 0, 1, 480);
  add(b, 0, 2, 480 + 10);
  MorphReport r;
  morphOk(a, b, 0.5, c, false, &r);
  CHECK(r.matched == 0 && r.onlyA == 1 && r.onlyB == 1);
  Pattern b2 = emptyPattern(c);
  add(b2, 0, 2, 480);
  MorphReport r2;
  morphOk(a, b2, 0.5, c, false, &r2);
  CHECK(r2.matched == 1);
}

CHOP_TEST(id_collisions_between_branches_do_not_merge_different_events) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c), b = emptyPattern(c);
  a.nextId = b.nextId = 41;
  add(a, 0, 1, 0);       // id 41
  add(b, 0, 5, 1920);    // also id 41, other chop and position
  MorphReport r;
  const Pattern m = morphOk(a, b, 0.5, c, false, &r);
  CHECK(r.matched == 0 && r.onlyA == 1 && r.onlyB == 1);
  std::set<std::uint64_t> ids;
  for (const Event& e : all(m)) ids.insert(e.id.value);
  CHECK(ids.size() == 2 && validate(m).ok());
}

namespace {
struct Fade {
  Pattern a, b;
  std::set<std::uint64_t> aIds;
};
Fade fadePair(const ChopSnapshot& c) {
  Fade f;
  f.a = emptyPattern(c, 8);
  f.b = emptyPattern(c, 8);
  f.b.nextId = f.a.nextId = 10000;
  for (int bar = 0; bar < 8; ++bar)
    for (int k = 0; k < 12; ++k) {
      f.aIds.insert(add(f.a, bar, 1, k * 240 * 1 + (k % 2) * 0).value);
      add(f.b, bar, 2, k * 240 + 120 + 1920 * 0);  // different chop, half a step off: cost 120 + 240 > one cell
    }
  return f;
}
}  // namespace

CHOP_TEST(unmatched_events_fade_monotonically) {
  const ChopSnapshot c = makeChops();
  Fade f = fadePair(c);
  std::size_t prevA = 1000, prevB = 0;
  std::size_t atHalfA = 0;
  for (int i = 0; i <= 19; ++i) {  // t = 1 returns B itself, with B's own ids
    const double t = i / 20.0;
    const Pattern m = morphOk(f.a, f.b, t, c);
    const auto flat = flatten(m, c).value();
    std::size_t ca = 0, cb = 0;
    for (const FlatEvent& e : flat) (f.aIds.count(e.id.value) ? ca : cb)++;
    CHECK(ca <= prevA && cb >= prevB);
    prevA = ca;
    prevB = cb;
    if (i == 10) atHalfA = ca;
  }
  const double n = 96, p = 0.5, sigma = std::sqrt(n * p * (1 - p));
  CHECK(std::fabs(static_cast<double>(atHalfA) - n * p) <= 3 * sigma);
  CHECK(prevA < 15 && prevB > 80);  // at t = 0.95
}

CHOP_TEST(b_only_ids_are_stable_across_t) {
  const ChopSnapshot c = makeChops();
  Fade f = fadePair(c);
  auto idsAt = [&](double t) {
    std::set<std::uint64_t> ids;
    for (const Event& e : all(morphOk(f.a, f.b, t, c)))
      if (!f.aIds.count(e.id.value)) ids.insert(e.id.value);
    return ids;
  };
  const auto at3 = idsAt(0.3), at8 = idsAt(0.8);
  CHECK(at3 == at8 && at3.size() == 96);
  CHECK(*at3.begin() >= 10000);
}

CHOP_TEST(refuses_different_length_meter_or_grid_with_conflict) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c, 1);
  const Pattern longer = emptyPattern(c, 2);
  auto r = morph(a, longer, 0.5, c);
  CHECK(!r.ok() && r.error().code == ErrorCode::Conflict);
  CHECK(r.error().message == "A and B differ in pattern length, meter or grid, so they cannot be morphed.");
  Settings s3;
  s3.bars = 1;
  s3.timeSignature = {3, 4};
  const Pattern waltz = generate(c, s3).value();
  CHECK(!morph(a, waltz, 0.5, c).ok());
  Settings sg;
  sg.bars = 1;
  sg.grid = Grid{8, false};
  const Pattern eighths = generate(c, sg).value();
  CHECK(!morph(a, eighths, 0.5, c).ok());
}

CHOP_TEST(refuses_over_cap_with_limit_exceeded_and_exact_numbers) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c), b = emptyPattern(c);
  a.settings.maxEvents = b.settings.maxEvents = 10;
  for (int k = 0; k < 6; ++k) {
    add(a, 0, 1, k * 480);
    add(b, 0, 2, k * 480 + 240);
  }
  auto r = morph(a, b, 0.5, c);
  CHECK(!r.ok() && r.error().code == ErrorCode::LimitExceeded);
  CHECK(r.error().message == "A and B have too many different hits to morph (12 > 10).");
}

namespace {
std::shared_ptr<NestedPattern> twoChildren(const Event& parent, std::uint64_t firstDerived) {
  auto child = std::make_shared<NestedPattern>();
  child->windowDuration = parent.duration;
  Event a;
  a.id = EventId{kDerivedIdBit | firstDerived};
  a.chop = parent.chop;
  a.start = 0;
  a.duration = parent.duration / 2;
  Event b = a;
  b.id = EventId{kDerivedIdBit | (firstDerived + 1)};
  b.start = parent.duration / 2;
  b.duration = parent.duration - parent.duration / 2;
  child->events = {a, b};
  return child;
}
}  // namespace

CHOP_TEST(child_events_take_the_chosen_sides_timing_and_child) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c);
  const EventId id = add(a, 0, 1, 0, 480);
  Pattern b = a;
  mut(b, id)->start = 240;
  mut(b, id)->duration = 960;
  const Pattern withChild = setChild(b, id, twoChildren(*mut(b, id), 1), c).value();
  const Event* e = find(all(morphOk(a, withChild, 0.25, c)), id);
  CHECK(e && e->child == nullptr && e->start == 60 && e->duration == 600);
  e = find(all(morphOk(a, withChild, 0.5, c)), id);
  CHECK(e && e->child != nullptr && e->start == 240 && e->duration == 960);
  CHECK(validate(morphOk(a, withChild, 0.5, c)).ok());
}

CHOP_TEST(duplicate_derived_child_ids_fall_back_to_dropping_children) {
  const ChopSnapshot c = makeChops();
  Pattern a = emptyPattern(c), b = emptyPattern(c);
  const EventId ida = add(a, 0, 1, 0, 480);
  const EventId idb = add(b, 0, 2, 1920, 480);
  const Pattern a2 = setChild(a, ida, twoChildren(*mut(a, ida), 1), c).value();
  const Pattern b2 = setChild(b, idb, twoChildren(*mut(b, idb), 1), c).value();  // the same derived ids
  MorphReport r;
  const Pattern m = morphOk(a2, b2, 0.3, c, false, &r);
  CHECK(r.childrenDropped && validate(m).ok());
  CHECK(r.onlyA == 1 && r.onlyB == 1);
  for (const Event& e : all(m))
    if (e.id.value != ida.value) CHECK(e.child == nullptr);
}

CHOP_TEST(result_validates_and_flattens_for_random_pairs) {
  const ChopSnapshot c = makeChops();
  for (std::uint64_t i = 1; i <= 60; ++i) {
    const Pattern a = generated(c, i, 0.3 + 0.1 * static_cast<double>(i % 6), i % 2 == 0);
    const Pattern b = generated(c, i + 1000, 0.3 + 0.1 * static_cast<double>((i + 3) % 6), i % 3 == 0);
    for (double t : {0.1, 0.5, 0.9}) {
      auto m = morph(a, b, t, c);
      CHECK(m.ok());
      if (!m.ok()) continue;
      CHECK(validate(m.value()).ok());
      CHECK(flatten(m.value(), c).ok());
    }
  }
}

CHOP_TEST(freeze_matches_preview_flatten) {
  const ChopSnapshot c = makeChops();
  for (std::uint64_t i = 1; i <= 50; ++i) {
    const Pattern a = generated(c, i, 0.5, true);
    const Pattern b = generated(c, i + 500, 0.6, true);
    const double t = 0.2 + 0.1 * static_cast<double>(i % 7);
    const Pattern live = morphOk(a, b, t, c, false);
    const Pattern frozen = morphOk(a, b, t, c, true);
    const auto fl = flatten(live, c).value();
    const auto ff = flatten(frozen, c).value();
    CHECK(fl.size() == ff.size());
    for (std::size_t k = 0; k < std::min(fl.size(), ff.size()); ++k) CHECK(fl[k].id == ff[k].id && fl[k].start == ff[k].start && fl[k].tx.level == ff[k].tx.level);
    for (const Event& e : all(frozen)) CHECK(e.probability >= 0.f && e.probability <= 1.f);
  }
}

CHOP_TEST(freeze_restores_base_probabilities_of_unmatched_hits) {
  const ChopSnapshot c = makeChops();
  Fade f = fadePair(c);
  for (Bar& b : f.a.bars)
    for (Beat& bt : b.beats)
      for (Event& e : bt.events) e.probability = 0.9f;
  const Pattern frozen = morphOk(f.a, f.b, 0.3, c, true);
  for (const Event& e : all(frozen)) CHECK(f.aIds.count(e.id.value) ? e.probability == 0.9f : e.probability == 1.f);
}

CHOP_TEST(morph_is_deterministic_run_to_run) {
  const ChopSnapshot c = makeChops();
  const Pattern a = generated(c, 3, 0.6, true), b = generated(c, 4, 0.5, true);
  CHECK(serialize(morphOk(a, b, 0.37, c)) == serialize(morphOk(a, b, 0.37, c)));
  CHECK(serialize(morphOk(a, b, 0.37, c, true)) == serialize(morphOk(a, b, 0.37, c, true)));
}

CHOP_TEST(settings_blend_and_the_result_keeps_a_identity) {
  const ChopSnapshot c = makeChops();
  Pattern a = generated(c, 3, 0.2, false), b = generated(c, 4, 0.8, true);
  a.settings.maxEvents = 300;
  const Pattern m = morphOk(a, b, 0.25, c);
  CHECK_NEAR(m.settings.density, 0.35, 1e-9);
  CHECK(m.settings.seed == a.settings.seed && m.settings.maxEvents == 300);
  CHECK(m.settings.allowReverse == (a.settings.allowReverse || b.settings.allowReverse));
  CHECK(m.phraseId == a.phraseId && m.bars.size() == a.bars.size());
}
