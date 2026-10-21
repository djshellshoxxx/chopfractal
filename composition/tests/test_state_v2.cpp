// F01: project state v2. Pins the v1 bytes first (captured from the build before any production change), then
// covers extras, A/B snapshots, sections, migration, deferred resolve and the export job.
#include <cstdio>
#include <chopfractal/composition/export_job.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define cf_getpid _getpid
#else
#include <unistd.h>
#define cf_getpid getpid
#endif

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
  e.exportUi = {0, 3, 4, {}};
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
  sec.tag = 25;
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

// ---- deferred source resolve ----

CHOP_TEST(deferred_source_resolve_keeps_a_usable_session_and_relink_restores_audio) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  const auto saved = a.saveState(false).value();
  int calls = 0;
  SourceResolver resolver = [&](const source::SourceInfo&) -> std::shared_ptr<const render::SourceData> {
    ++calls;
    return statefix::drumLoop();
  };
  // Default: the resolver runs and the audio comes back immediately (old behaviour).
  ProjectSession eager;
  CHECK(eager.loadState(saved.data(), saved.size(), resolver).ok());
  CHECK_EQ(calls, 1);
  CHECK(!eager.sourceMissing());

  // Deferred: never calls the resolver and queues no "not found" notice; the session is already usable.
  calls = 0;
  ProjectSession b;
  LoadOptions opt;
  opt.deferSourceResolve = true;
  CHECK(b.loadState(saved.data(), saved.size(), resolver, opt).ok());
  CHECK_EQ(calls, 0);
  CHECK(b.sourceMissing());
  CHECK(b.pattern() != nullptr && b.chopMap() != nullptr && b.chops() != nullptr);
  CHECK(b.sourceData() == nullptr);
  CHECK(b.takeNotices().empty());
  CHECK(b.playback() != nullptr);
  // Relink restores the audio and the pattern plays.
  CHECK(b.relinkSource(statefix::drumLoop()).ok());
  CHECK(!b.sourceMissing());
  CHECK(b.playback() && !b.playback()->passThrough && !b.playback()->events.empty());
  auto rendered = b.renderOffline(render::OfflineSettings{});
  CHECK(rendered.ok());

  // Wrong audio is refused and the caller reports it with the existing wording.
  ProjectSession c;
  CHECK(c.loadState(saved.data(), saved.size(), resolver, opt).ok());
  auto other = std::make_shared<render::SourceData>(*statefix::drumLoop());
  other->samples[10] += 0.5f;
  Status wrong = c.relinkSource(other);
  CHECK(!wrong.ok() && wrong.error().code == ErrorCode::Conflict);
  c.reportSourceUnavailable(true);
  c.reportSourceUnavailable(false);
  auto notices = c.takeNotices();
  CHECK_EQ(notices.size(), 2u);
  if (notices.size() == 2) {
    CHECK(notices[0].level == Notice::Level::Warning);
    CHECK(notices[0].text == "The file for \"loop\" is not the audio this project was saved with. Markers and pattern are kept; relink the original file.");
    CHECK(notices[1].text == "The source \"loop\" was not found at /music/loop.wav. Markers and pattern are kept; relink the file to hear playback.");
  }
  // Embedded audio is never deferred away.
  const auto embedded = a.saveState(true).value();
  ProjectSession d;
  CHECK(d.loadState(embedded.data(), embedded.size(), resolver, opt).ok());
  CHECK(!d.sourceMissing());
}

// ---- export job ----

namespace {
namespace fs = std::filesystem;

struct TempDir {
  fs::path path;
  TempDir() {
    static int counter = 0;
    path = fs::temp_directory_path() / ("cf_f01_" + std::to_string(cf_getpid()) + "_" + std::to_string(counter++));
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

std::vector<std::uint8_t> readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::set<std::string> listing(const fs::path& dir) {
  std::set<std::string> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec)) out.insert(e.path().filename().string());
  return out;
}
}  // namespace

CHOP_TEST(export_job_planning_validates_like_the_synchronous_export) {
  ProjectSession empty;
  TempDir tmp;
  ExportWavOptions o;
  auto none = ExportJob::planWav(empty, (tmp.path / "x.wav").string(), o);
  CHECK(!none.ok() && none.error().code == ErrorCode::InvalidArgument && none.error().message == "there is no pattern to render");
  ProjectSession s;
  CHECK(statefix::fixtureSession(s));
  o.loops = 0;
  auto badLoops = ExportJob::planWav(s, (tmp.path / "x.wav").string(), o);
  CHECK(!badLoops.ok() && badLoops.error().code == ErrorCode::OutOfRange && badLoops.error().message == "loops must be 1 to 16");
  o.loops = 1;
  o.tailSeconds = 31;
  CHECK(!ExportJob::planWav(s, (tmp.path / "x.wav").string(), o).ok());
  o.tailSeconds = 0.5;
  auto ok = ExportJob::planWav(s, (tmp.path / "my.wav").string(), o);
  CHECK(ok.ok());
  CHECK(ok.ok() && ok.value().displayName() == "my.wav" && ok.value().kind() == ExportJob::Kind::Wav);
  CHECK(!fs::exists(tmp.path / "my.wav"));  // planning writes nothing
  { std::ofstream f(tmp.path / "there.wav", std::ios::binary); f << "x"; }
  auto exists = ExportJob::planWav(s, (tmp.path / "there.wav").string(), o);
  CHECK(!exists.ok() && exists.error().code == ErrorCode::Conflict);
  o.overwrite = true;
  CHECK(ExportJob::planWav(s, (tmp.path / "there.wav").string(), o).ok());
  KitOptions k;
  auto kitNone = ExportJob::planKit(empty, tmp.path.string(), k);
  CHECK(!kitNone.ok() && kitNone.error().message == "load a source with chops first");
  k.baseNote = 127;
  auto kitBase = ExportJob::planKit(s, tmp.path.string(), k);
  CHECK(!kitBase.ok() && kitBase.error().code == ErrorCode::OutOfRange);
  k.baseNote = 36;
  auto kit = ExportJob::planKit(s, (tmp.path / "kit").string() + "/", k);
  CHECK(kit.ok() && kit.value().displayName() == "kit" && kit.value().kind() == ExportJob::Kind::Kit);
}

CHOP_TEST(export_job_output_equals_the_synchronous_export) {
  ProjectSession s;
  CHECK(statefix::fixtureSession(s, 2));
  TempDir tmp;
  ExportWavOptions o;
  o.loops = 2;
  o.wav.format = wav::Format::Pcm24;
  const std::string viaSession = (tmp.path / "session.wav").string(), viaJob = (tmp.path / "job.wav").string();
  CHECK(s.exportWav(viaSession, o).ok());
  auto job = ExportJob::planWav(s, viaJob, o);
  CHECK(job.ok());
  ExportProgress progress;
  std::vector<float> seen;
  progress.observer = [&](float f) { seen.push_back(f); };
  auto rep = job.value().run(progress);
  CHECK(rep.ok());
  CHECK(readFile(viaSession) == readFile(viaJob));
  auto direct = s.renderWavBytes(o);  // independent path: renderOffline + encode
  CHECK(direct.ok() && direct.value() == readFile(viaJob));
  CHECK(!seen.empty() && seen.back() == 1.f && std::is_sorted(seen.begin(), seen.end()));
  CHECK_EQ(progress.fraction.load(), 1.f);
  CHECK(rep.ok() && rep.value().files.size() == 1 && rep.value().frames > 0);
  // The job is immutable: the same job runs again with the same result and may overwrite (it was planned without).
  ExportProgress again;
  auto second = job.value().run(again);
  CHECK(!second.ok() && second.error().code == ErrorCode::Conflict);  // run re-checks the target before the atomic write

  // Kit: same file set and identical bytes whether it runs through the session or the job.
  KitOptions k;
  k.loops = 2;
  const fs::path dirA = tmp.path / "a", dirB = tmp.path / "b";
  auto repA = s.exportKit(dirA.string(), k);
  CHECK(repA.ok());
  auto kitJob = ExportJob::planKit(s, dirB.string(), k);
  CHECK(kitJob.ok());
  ExportProgress kp;
  std::vector<float> kseen;
  kp.observer = [&](float f) { kseen.push_back(f); };
  auto repB = kitJob.value().run(kp);
  CHECK(repB.ok());
  CHECK(listing(dirA) == listing(dirB));
  CHECK_EQ(listing(dirA).size(), repA.value().files.size());
  for (const std::string& name : listing(dirA)) CHECK(readFile(dirA / name) == readFile(dirB / name));
  CHECK(repA.value().notes == repB.value().notes && repA.value().notes > 0);
  CHECK(readFile(dirA / "chopfractal.mid") == s.renderMidi(k).value());
  CHECK(std::is_sorted(kseen.begin(), kseen.end()) && !kseen.empty() && kseen.back() == 1.f);
  CHECK_EQ(kseen.size(), repB.value().files.size() + 1);
}

CHOP_TEST(export_job_cancel_leaves_no_partial_files) {
  ProjectSession s;
  CHECK(statefix::fixtureSession(s));
  TempDir tmp;
  // Cancel pre-set: Cancelled and no file for a WAV, nothing for a kit.
  {
    ExportWavOptions o;
    const std::string path = (tmp.path / "pre.wav").string();
    auto job = ExportJob::planWav(s, path, o);
    CHECK(job.ok());
    ExportProgress p;
    p.cancel = true;
    auto r = job.value().run(p);
    CHECK(!r.ok() && r.error().code == ErrorCode::Cancelled);
    CHECK(!fs::exists(path));
  }
  // Cancel from the progress callback during a WAV render (before the write) and just before the write.
  for (float at : {0.0f, 0.5f, 0.95f}) {
    ExportWavOptions o;
    o.loops = 4;
    const std::string path = (tmp.path / ("mid" + std::to_string(static_cast<int>(at * 100)) + ".wav")).string();
    auto job = ExportJob::planWav(s, path, o);
    CHECK(job.ok());
    ExportProgress p;
    p.observer = [&](float f) {
      if (f >= at) p.cancel = true;
    };
    auto r = job.value().run(p);
    CHECK(!r.ok() && r.error().code == ErrorCode::Cancelled);
    CHECK(!fs::exists(path));
    CHECK(listing(tmp.path).count(fs::path(path).filename().string() + ".cf-tmp0") == 0);
  }
  // Kit: cancel after a few files; the directory holds no new file.
  {
    KitOptions k;
    const fs::path dir = tmp.path / "kit";
    auto job = ExportJob::planKit(s, dir.string(), k);
    CHECK(job.ok());
    ExportProgress p;
    int events = 0;
    p.observer = [&](float) {
      if (++events == 3) p.cancel = true;
    };
    auto r = job.value().run(p);
    CHECK(!r.ok() && r.error().code == ErrorCode::Cancelled);
    CHECK(listing(dir).empty());
    CHECK(events >= 3);
  }
  // Kit with overwrite: files that already existed stay (their content was replaced), files this run created go.
  {
    KitOptions k;
    k.overwrite = true;
    const fs::path dir = tmp.path / "kit2";
    fs::create_directories(dir);
    { std::ofstream f(dir / "kit.txt", std::ios::binary); f << "old"; }
    auto job = ExportJob::planKit(s, dir.string(), k);
    CHECK(job.ok());
    ExportProgress p;
    const float beforeMap = static_cast<float>(job.value().fileCount() - 1) / static_cast<float>(job.value().fileCount()) - 1e-4f;
    p.observer = [&](float f) {
      if (f >= beforeMap) p.cancel = true;  // after every slice and the midi file, before kit.txt
    };
    auto r = job.value().run(p);
    CHECK(!r.ok() && r.error().code == ErrorCode::Cancelled);
    CHECK(listing(dir) == (std::set<std::string>{"kit.txt"}));
    CHECK(readFile(dir / "kit.txt") == (std::vector<std::uint8_t>{'o', 'l', 'd'}));
  }
  // After a cancelled run a fresh job on the same targets succeeds.
  {
    KitOptions k;
    const fs::path dir = tmp.path / "kit";
    auto job = ExportJob::planKit(s, dir.string(), k);
    CHECK(job.ok());
    ExportProgress p;
    CHECK(job.value().run(p).ok());
    CHECK(!listing(dir).empty());
  }
}
