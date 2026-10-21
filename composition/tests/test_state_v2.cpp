// F01: project state v2. Pins the v1 bytes first (captured from the build before any production change), then
// covers extras, A/B snapshots, sections, migration, deferred resolve and the export job.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <vector>

#include "chop_test.hpp"
#include "state_fixture.hpp"
#include "v1_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

std::vector<std::uint8_t> v1Bytes() { return std::vector<std::uint8_t>(std::begin(v1fixture::kV1FixtureBytes), std::end(v1fixture::kV1FixtureBytes)); }

std::uint32_t compositionSchema(const std::vector<std::uint8_t>& state) {
  auto dec = codec::decode(state.data(), state.size());
  CHECK(dec.ok());
  if (!dec.ok()) return 0;
  auto it = dec.value().modules.find("composition");
  CHECK(it != dec.value().modules.end());
  return it == dec.value().modules.end() ? 0 : it->second.schemaVersion;
}

}  // namespace

CHOP_TEST(default_session_state_bytes_are_unchanged_from_v1) {
  ProjectSession s;
  CHECK(statefix::fixtureSession(s));
  auto saved = s.saveState(false);
  CHECK(saved.ok());
  CHECK_EQ(statefix::fnv1a64(saved.value()), v1fixture::kV1FixtureHash);
  CHECK(saved.value() == v1Bytes());
  CHECK_EQ(compositionSchema(saved.value()), 1u);
}

CHOP_TEST(v1_state_loads_and_resaves_identically) {
  const std::vector<std::uint8_t> v1 = v1Bytes();
  ProjectSession a;
  CHECK(a.loadState(v1.data(), v1.size(), {}).ok());
  CHECK(a.pattern() != nullptr);
  CHECK(a.chopMap() != nullptr);
  auto again = a.saveState(false);
  CHECK(again.ok());
  CHECK(again.value() == v1);
}

namespace {

ProjectExtras nonDefaultExtras() {
  ProjectExtras e;
  e.seed = 987654321;
  e.motif = "xa.x";
  e.fractalDepth = 3;
  e.roleIndex = 4;
  e.detectMode = 3;
  e.gridDivision = 8;
  e.gridTriplet = true;
  e.exportUi = {0, 3, 4};
  e.fractalMutation = 0.4;
  e.fractalAccentDecay = 0.6;
  e.ui.viewMode = 0;
  e.ui.scalePercent = 150;
  e.ui.width = 1200;
  e.ui.height = 700;
  e.ui.editHits = true;
  e.ui.markerSnap = 2;
  e.ui.gridSnap = false;
  e.ui.snapDivision = 16;
  e.ui.favoritesOnly = true;
  e.ui.search = "snare";
  return e;
}

std::vector<std::uint8_t> savedWithExtras(const ProjectExtras& e, ProjectSession& s) {
  CHECK(statefix::fixtureSession(s));
  CHECK(s.setExtras(e).ok());
  auto saved = s.saveState(false);
  CHECK(saved.ok());
  return saved.ok() ? saved.value() : std::vector<std::uint8_t>{};
}

std::string lastWarning(ProjectSession& s) {
  std::string text;
  for (const Notice& n : s.takeNotices())
    if (n.level == Notice::Level::Warning) text = n.text;
  return text;
}

}  // namespace

CHOP_TEST(v1_state_loads_through_the_registered_migration) {
  const std::vector<std::uint8_t> v1 = v1Bytes();
  ProjectSession fresh;
  CHECK(statefix::fixtureSession(fresh));
  ProjectSession loaded;
  CHECK(loaded.loadState(v1.data(), v1.size(), {}).ok());
  CHECK(pattern::serialize(*loaded.pattern()) == pattern::serialize(*fresh.pattern()));
  CHECK(loaded.chopMap()->serialize() == fresh.chopMap()->serialize());
  CHECK(loaded.history().serialize() == fresh.history().serialize());
  CHECK(loaded.extras() == ProjectExtras{});
  CHECK(!loaded.hasSnapshot(0) && !loaded.hasSnapshot(1));
  CHECK(!loaded.stateUsesSchemaV2());
  for (const Notice& n : loaded.takeNotices()) CHECK(n.text.find("settings") == std::string::npos);  // only the missing-source notice
}

CHOP_TEST(extras_round_trip_and_force_schema_2) {
  ProjectSession a;
  const ProjectExtras e = nonDefaultExtras();
  CHECK(validate(e).ok());
  const auto saved = savedWithExtras(e, a);
  CHECK_EQ(compositionSchema(saved), 2u);
  CHECK(a.stateUsesSchemaV2());
  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), {}).ok());
  CHECK(b.extras() == e);
  CHECK(b.pattern() != nullptr);
  auto again = b.saveState(false);
  CHECK(again.ok() && again.value() == saved);  // deterministic: no timestamps, ordered blocks
  // Putting every field back to its default returns to the exact v1 bytes.
  CHECK(b.setExtras(ProjectExtras{}).ok());
  auto back = b.saveState(false);
  CHECK(back.ok() && back.value() == v1Bytes());
  // Each field on its own forces schema 2 and survives.
  const ProjectExtras d;
  std::vector<ProjectExtras> singles(10, d);
  singles[0].seed = 5;
  singles[1].motif = "x";
  singles[2].fractalDepth = 1;
  singles[3].roleIndex = 8;
  singles[4].detectMode = 2;
  singles[5].gridTriplet = true;
  singles[6].exportUi.loopsIndex = 0;
  singles[7].ui.search = "k";
  singles[8].fractalAccentDecay = 1.0;
  singles[9].ui.editHits = true;
  for (const ProjectExtras& one : singles) {
    ProjectSession s;
    const auto bytes = savedWithExtras(one, s);
    CHECK_EQ(compositionSchema(bytes), 2u);
    ProjectSession t;
    CHECK(t.loadState(bytes.data(), bytes.size(), {}).ok());
    CHECK(t.extras() == one);
  }
}

CHOP_TEST(extras_validation_names_the_field_and_rejects_out_of_range_values) {
  ProjectSession s;
  const ProjectExtras d;
  auto expectBad = [&](ProjectExtras e, const char* field) {
    Status st = validate(e);
    CHECK(!st.ok());
    CHECK(!st.ok() && st.error().message.find(field) != std::string::npos);
    CHECK(!s.setExtras(e).ok());
    CHECK(s.extras() == d);  // a refused setExtras changes nothing
  };
  ProjectExtras e = d;
  e.seed = 1000000000000ull;
  expectBad(e, "seed");
  e = d; e.motif = "xb"; expectBad(e, "motif");
  e = d; e.motif = ""; expectBad(e, "motif");
  e = d; e.motif = "xxxxxxxxx"; expectBad(e, "motif");
  e = d; e.fractalDepth = 4; expectBad(e, "fractalDepth");
  e = d; e.roleIndex = 9; expectBad(e, "roleIndex");
  e = d; e.detectMode = 4; expectBad(e, "detectMode");
  e = d; e.gridDivision = 3; expectBad(e, "gridDivision");
  e = d; e.fractalMutation = 1.5; expectBad(e, "fractalMutation");
  e = d; e.fractalAccentDecay = 0.0; expectBad(e, "fractalAccentDecay");
  e = d; e.exportUi.rateIndex = 4; expectBad(e, "exportUi.rateIndex");
  e = d; e.ui.scalePercent = 49; expectBad(e, "ui.scalePercent");
  e = d; e.ui.width = 639; expectBad(e, "ui.width");
  e = d; e.ui.snapDivision = 3; expectBad(e, "ui.snapDivision");
  e = d; e.ui.search = std::string(33, 'a'); expectBad(e, "ui.search");
  CHECK(validate(d).ok());
  CHECK(s.setExtras(nonDefaultExtras()).ok());
  CHECK(s.extras() == nonDefaultExtras());
  // setExtras adds no undo step and no history node.
  ProjectSession h;
  CHECK(statefix::fixtureSession(h));
  const auto nodes = h.history().size();
  const auto before = pattern::serialize(*h.pattern());
  CHECK(h.setExtras(nonDefaultExtras()).ok());
  CHECK_EQ(h.history().size(), nodes);
  CHECK(pattern::serialize(*h.pattern()) == before);
}

CHOP_TEST(out_of_range_stored_values_fall_back_to_their_own_default) {
  ProjectSession s;
  ProjectExtras e;
  e.seed = 77;
  e.fractalDepth = 3;
  const auto saved = savedWithExtras(e, s);
  // Patch the depth byte of the Fractal block (tag 2) to 9 and the seed stays honoured.
  const auto bad = statefix::patchComposition(saved, [](std::vector<std::uint8_t>& p) {
    std::size_t pos = statefix::kExtrasOffset + 3;
    while (pos < p.size() && p[pos] != 2) pos += 5 + (p[pos + 1] | (p[pos + 2] << 8));
    CHECK(pos < p.size() && p[pos] == 2);
    p[pos + 5] = 9;
  });
  ProjectSession t;
  CHECK(t.loadState(bad.data(), bad.size(), {}).ok());
  CHECK_EQ(t.extras().seed, 77u);
  CHECK_EQ(t.extras().fractalDepth, 2);
  CHECK(lastWarning(t).find("editor settings") == std::string::npos);
}

CHOP_TEST(ab_snapshots_round_trip_and_are_dropped_individually_when_invalid) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  CHECK(a.storeSnapshot(0).ok());
  const auto aBytes = pattern::serialize(*a.pattern());
  pattern::MutateOptions mo;
  mo.seed = 99;
  mo.amount = 1.0;
  CHECK(a.mutate(mo).ok());
  CHECK(a.storeSnapshot(1).ok());
  const auto bBytes = pattern::serialize(*a.pattern());
  CHECK(aBytes != bBytes);
  CHECK(a.stateUsesSchemaV2());
  const auto saved = a.saveState(false).value();
  CHECK_EQ(compositionSchema(saved), 2u);

  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), {}).ok());
  CHECK(b.hasSnapshot(0) && b.hasSnapshot(1));
  CHECK(b.recallSnapshot(0).ok());
  CHECK(pattern::serialize(*b.pattern()) == aBytes);
  CHECK(b.recallSnapshot(1).ok());
  CHECK(pattern::serialize(*b.pattern()) == bBytes);
  auto again = b.saveState(false);
  CHECK(again.ok() && again.value().size() == saved.size());

  // Corrupt the bytes of slot B only.
  const auto corrupt = statefix::patchComposition(saved, [&](std::vector<std::uint8_t>& p) {
    std::size_t pos = statefix::kExtrasOffset + 3;
    while (pos < p.size() && p[pos] != 8) pos += 5 + (p[pos + 1] | (p[pos + 2] << 8) | (p[pos + 3] << 16));
    CHECK(pos < p.size() && p[pos] == 8);
    const std::size_t block = pos + 5;
    CHECK_EQ(p[block], 3);  // both slots
    const std::size_t lenA = p[block + 1] | (p[block + 2] << 8) | (p[block + 3] << 16);
    const std::size_t bStart = block + 1 + 4 + lenA + 4;
    for (std::size_t i = bStart; i < p.size(); ++i) p[i] = 0xFF;
  });
  ProjectSession c;
  CHECK(c.loadState(corrupt.data(), corrupt.size(), {}).ok());
  CHECK(c.hasSnapshot(0) && !c.hasSnapshot(1));
  int warnings = 0;
  bool namesB = false;
  for (const Notice& n : c.takeNotices())
    if (n.level == Notice::Level::Warning && n.text.find("Snapshot") != std::string::npos) {
      ++warnings;
      namesB = n.text.find("Snapshot B") != std::string::npos;
    }
  CHECK_EQ(warnings, 1);
  CHECK(namesB);
  CHECK(c.recallSnapshot(0).ok());
  CHECK(pattern::serialize(*c.pattern()) == aBytes);

  // A project with only snapshot A still saves as schema 2 (and without any slot: back to v1 bytes).
  ProjectSession d;
  CHECK(statefix::fixtureSession(d));
  CHECK(d.storeSnapshot(0).ok());
  CHECK(d.stateUsesSchemaV2());
}

CHOP_TEST(malformed_extras_never_fail_the_load) {
  ProjectSession base;
  ProjectExtras e;
  e.seed = 4242;
  const auto saved = savedWithExtras(e, base);
  const auto head = statefix::v1Head(saved);
  const std::vector<std::uint8_t> seed4242{0x92, 0x10, 0, 0, 0, 0, 0, 0};
  auto withTail = [&](const std::vector<std::uint8_t>& tail) {
    return statefix::patchComposition(saved, [&](std::vector<std::uint8_t>& p) {
      p = head;
      p.insert(p.end(), tail.begin(), tail.end());
    });
  };
  auto expectDefaultsWithOneWarning = [&](const std::vector<std::uint8_t>& state, const char* what) {
    ProjectSession s;
    const Status st = s.loadState(state.data(), state.size(), {});
    CHECK(st.ok());
    CHECK(s.pattern() != nullptr);  // the rest of the project is honoured
    CHECK(s.extras() == ProjectExtras{});
    int n = 0;
    for (const Notice& no : s.takeNotices())
      if (no.text == "Some editor settings could not be restored.") ++n;
    if (n != 1) std::fprintf(stderr, "case: %s\n", what);
    CHECK_EQ(n, 1);
  };
  // truncated section
  {
    auto p = statefix::compositionPayload(saved);
    const auto cut = statefix::patchComposition(saved, [](std::vector<std::uint8_t>& q) { q.resize(q.size() - 3); });
    expectDefaultsWithOneWarning(cut, "truncated");
  }
  expectDefaultsWithOneWarning(withTail(statefix::rawExtras({{1, seed4242}, {1, seed4242}})), "duplicate tag");
  expectDefaultsWithOneWarning(withTail([&] {
    auto t = statefix::rawExtras({{1, seed4242}});
    t[3 + 1] = 0xF0;  // length low byte -> far past the end
    t[3 + 4] = 0x7F;
    return t;
  }()), "oversize length");
  expectDefaultsWithOneWarning(withTail(statefix::rawExtras({{1, {1, 2, 3}}})), "seed block of the wrong shape");
  expectDefaultsWithOneWarning(withTail(statefix::rawExtras({{1, seed4242}}, 33)), "block count over 32");
  expectDefaultsWithOneWarning(withTail({0x5A, 0, 0}), "wrong marker");
  expectDefaultsWithOneWarning(withTail([&] {
    auto t = statefix::rawExtras({{1, seed4242}});
    t.push_back(0);
    return t;
  }()), "trailing junk");
  expectDefaultsWithOneWarning(withTail([&] {
    auto t = statefix::rawExtras({{200, std::vector<std::uint8_t>(64 * 1024 + 3, 7)}});
    return t;
  }()), "unknown tag over its cap");
  // 41 MiB of extras
  expectDefaultsWithOneWarning(withTail([&] {
    std::vector<std::uint8_t> t(41u * 1024u * 1024u, 0x11);
    t[0] = 0xE2;
    return t;
  }()), "41 MiB");

  // unknown tag 200: kept, re-written, the rest honoured
  const std::vector<std::uint8_t> blob{9, 8, 7, 6, 5};
  std::vector<std::uint8_t> sec{1, 0};
  sec.insert(sec.end(), blob.begin(), blob.end());
  const auto withUnknown = withTail(statefix::rawExtras({{1, seed4242}, {200, sec}}));
  ProjectSession u;
  CHECK(u.loadState(withUnknown.data(), withUnknown.size(), {}).ok());
  CHECK_EQ(u.extras().seed, 4242u);
  CHECK(u.section(200) != nullptr && u.section(200)->bytes == blob && u.section(200)->schema == 1);
  int warnings = 0;
  for (const Notice& no : u.takeNotices())
    if (no.level == Notice::Level::Warning && no.text.find("settings") != std::string::npos) ++warnings;
  CHECK_EQ(warnings, 0);
  auto resaved = u.saveState(false);
  CHECK(resaved.ok());
  CHECK(resaved.value() == withUnknown);
  // unknown tag below 9 is skipped
  const auto lowUnknown = withTail(statefix::rawExtras({{1, seed4242}, {5 + 0, {1}}, {0, {1, 2}}}));
  ProjectSession lo;
  CHECK(lo.loadState(lowUnknown.data(), lowUnknown.size(), {}).ok());
  CHECK_EQ(lo.extras().seed, 4242u);
  CHECK_EQ(lo.extras().detectMode, 1);
  CHECK(lo.section(0) == nullptr);
}

CHOP_TEST(extras_decoding_survives_truncation_and_byte_flip_sweeps) {
  ProjectSession a;
  ProjectExtras e = nonDefaultExtras();
  CHECK(statefix::fixtureSession(a));
  CHECK(a.storeSnapshot(0).ok());
  CHECK(a.setExtras(e).ok());
  StateSection sec;
  sec.tag = 14;
  sec.bytes = {1, 2, 3, 4, 5, 6};
  CHECK(a.setSection(sec).ok());
  const auto saved = a.saveState(false).value();
  const auto payload = statefix::compositionPayload(saved);
  auto loads = [&](const std::vector<std::uint8_t>& p) {
    const auto state = statefix::patchComposition(saved, [&](std::vector<std::uint8_t>& q) { q = p; });
    ProjectSession s;
    const Status st = s.loadState(state.data(), state.size(), {});
    CHECK(st.ok());  // a bad extras section never fails the load
    CHECK(s.pattern() != nullptr);
    CHECK(validate(s.extras()).ok());  // whatever survived is in range
    // and whatever was loaded can be saved again and loaded again
    auto resaved = s.saveState(false);
    CHECK(resaved.ok());
    if (resaved.ok()) {
      ProjectSession t;
      CHECK(t.loadState(resaved.value().data(), resaved.value().size(), {}).ok());
      CHECK(t.extras() == s.extras());
    }
  };
  for (std::size_t len = 0; len < payload.size(); ++len) loads(std::vector<std::uint8_t>(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(len)));
  const std::uint8_t xors[] = {0x01, 0x80, 0xFF};
  // Flip every byte of the extras section (skip the long pattern payload stride to keep the sweep fast).
  for (std::size_t pos = statefix::kExtrasOffset; pos < payload.size(); ++pos)
    for (std::uint8_t x : xors) {
      auto q = payload;
      q[pos] ^= x;
      loads(q);
    }
}

CHOP_TEST(evolve_settings_manual_bpm_and_embed_flag_round_trip) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  evolve::Settings es;
  es.enabled = true;
  es.everyLoops = 4;
  es.amount = 0.4;
  es.startSeed = 31337;
  CHECK(a.setEvolve(es).ok());
  CHECK(a.evolveRunning());
  const auto saved = a.saveState(true).value();
  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), {}).ok());
  CHECK(!b.evolveRunning());  // enabled is never persisted
  CHECK_EQ(b.evolveSettings().everyLoops, 4);
  CHECK_NEAR(b.evolveSettings().amount, 0.4, 1e-12);
  CHECK_EQ(b.evolveSettings().startSeed, 31337u);
  CHECK(b.wasEmbedded());
  CHECK(b.sourceData() != nullptr);
  // Evolve settings do not by themselves force schema 2: only extras do.
  CHECK_EQ(compositionSchema(saved), 1u);
  // Evolve settings coexist with extras.
  CHECK(b.setExtras(nonDefaultExtras()).ok());
  const auto both = b.saveState(true).value();
  ProjectSession c;
  CHECK(c.loadState(both.data(), both.size(), {}).ok());
  CHECK_EQ(c.evolveSettings().everyLoops, 4);
  CHECK(c.extras() == nonDefaultExtras());
}

CHOP_TEST(state_schema_versions_have_migration_paths) {
  ProjectSession s;
  const codec::MigrationRegistry& reg = s.migrations();
  CHECK_EQ(reg.size(), 1u);  // exactly one registered step: composition 1 -> 2
  struct Mod {
    const char* id;
    std::uint32_t current;
  };
  const Mod mods[] = {{"composition", kSessionSchemaVersion}, {"source_chop", source::kSchemaVersion}, {"pattern_engine", pattern::kSchemaVersion},
                      {"chop_roles_grammar", roles::kSchemaVersion}, {"variation_history", history::kSchemaVersion}, {"source_audio", kSessionAudioSchema}};
  for (const Mod& m : mods) {
    auto r = reg.migrate(m.id, {1, {1, 2, 3}}, m.current);
    CHECK(r.ok());  // fails when a constant was bumped without registering a step
    if (r.ok()) {
      CHECK_EQ(r.value().schemaVersion, m.current);
      CHECK(r.value().bytes == (std::vector<std::uint8_t>{1, 2, 3}));
    }
  }
  CHECK_EQ(pattern::kSchemaVersion, 1u);
  // A payload newer than this build is refused with the existing message path.
  auto newer = reg.migrate("composition", {kSessionSchemaVersion + 1, {}}, kSessionSchemaVersion);
  CHECK(!newer.ok() && static_cast<int>(newer.error().code) == static_cast<int>(ErrorCode::UnsupportedVersion));
}
