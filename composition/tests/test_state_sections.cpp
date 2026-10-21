// F01: the StateSection API (set/get, preserved unknown tags, per-tag caps, decoder failures, hostile sweeps).
#include <chopfractal/composition/project_session.hpp>

#include <iterator>
#include <vector>

#include "chop_test.hpp"
#include "state_fixture.hpp"
#include "v1_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {
StateSection makeSection(std::uint8_t tag, std::size_t n, std::uint8_t fill = 0x5A, std::uint16_t schema = 1) {
  StateSection s;
  s.tag = tag;
  s.schema = schema;
  s.bytes.assign(n, fill);
  return s;
}
int countWarnings(ProjectSession& s, const std::string& text) {
  int n = 0;
  for (const Notice& no : s.takeNotices())
    if (no.level == Notice::Level::Warning && no.text == text) ++n;
  return n;
}
}  // namespace

CHOP_TEST(section_roundtrip_and_absent_when_empty) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  const auto plain = a.saveState(false).value();
  CHECK(plain == std::vector<std::uint8_t>(std::begin(v1fixture::kV1FixtureBytes), std::end(v1fixture::kV1FixtureBytes)));
  CHECK(a.section(25) == nullptr);
  StateSection groove = makeSection(25, 10, 0x21, 3);
  groove.bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  CHECK(a.setSection(groove).ok());
  CHECK(a.section(25) != nullptr && a.section(25)->bytes == groove.bytes && a.section(25)->schema == 3);
  CHECK(a.stateUsesSchemaV2());
  const auto saved = a.saveState(false).value();
  CHECK(saved != plain);

  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), {}).ok());
  CHECK(b.section(25) != nullptr);
  CHECK(b.section(25) && b.section(25)->bytes == groove.bytes && b.section(25)->schema == 3 && b.section(25)->tag == 25);
  CHECK(b.saveState(false).value() == saved);

  // Setting empty bytes removes the section and the next save equals the pre-section save.
  StateSection empty;
  empty.tag = 25;
  CHECK(b.setSection(empty).ok());
  CHECK(b.section(25) == nullptr);
  CHECK(!b.stateUsesSchemaV2());
  CHECK(b.saveState(false).value() == plain);
  // A new project replaces the sections of the old one.
  ProjectSession c;
  CHECK(c.setSection(groove).ok());
  CHECK(c.loadState(plain.data(), plain.size(), {}).ok());
  CHECK(c.section(25) == nullptr);
}

CHOP_TEST(unknown_tag_12_is_preserved_by_a_build_that_does_not_know_it) {
  // Tag 20 and 12 are unknown to this test build (no decoder registered); a load/save cycle keeps them byte for byte.
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  CHECK(a.setSection(makeSection(20, 33, 0x77, 2)).ok());
  CHECK(a.setSection(makeSection(12, 5, 0x12)).ok());
  const auto saved = a.saveState(false).value();
  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), {}).ok());
  CHECK(b.section(20) && b.section(20)->bytes == std::vector<std::uint8_t>(33, 0x77) && b.section(20)->schema == 2);
  CHECK(b.section(12) && b.section(12)->bytes == std::vector<std::uint8_t>(5, 0x12));
  CHECK(b.saveState(false).value() == saved);
  CHECK(b.takeNotices().size() <= 1);  // only the missing-source notice
  // The sections survive a second hop through a session that also changed extras.
  ProjectExtras e;
  e.seed = 9;
  CHECK(b.setExtras(e).ok());
  const auto hop = b.saveState(false).value();
  ProjectSession c;
  CHECK(c.loadState(hop.data(), hop.size(), {}).ok());
  CHECK(c.section(20) && c.section(20)->bytes == std::vector<std::uint8_t>(33, 0x77));
  CHECK_EQ(c.extras().seed, 9u);
}

CHOP_TEST(per_tag_cap_rejects_oversize_and_tags_below_9_are_refused) {
  ProjectSession s;
  CHECK_EQ(sectionByteCap(9), 64u * 1024u);
  CHECK_EQ(sectionByteCap(11), 32u * 1024u * 1024u);
  CHECK_EQ(sectionByteCap(15), 64u);
  CHECK_EQ(sectionByteCap(16), 1024u * 1024u);
  CHECK(s.setSection(makeSection(25, 64 * 1024)).ok());
  Status over = s.setSection(makeSection(25, 64 * 1024 + 1));
  CHECK(!over.ok() && over.error().code == ErrorCode::OutOfRange);
  CHECK(s.section(25) != nullptr && s.section(25)->bytes.size() == 64u * 1024u);  // a refused set changes nothing
  CHECK(!s.setSection(makeSection(15, 65)).ok());
  CHECK(s.setSection(makeSection(15, 64)).ok());
  CHECK(s.setSection(makeSection(16, 1024 * 1024)).ok());
  CHECK(s.setSection(makeSection(11, 32u * 1024u * 1024u)).ok());
  CHECK(!s.setSection(makeSection(11, 32u * 1024u * 1024u + 1)).ok());
  for (int tag : {0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 32, 200, 255}) {
    Status st = s.setSection(makeSection(static_cast<std::uint8_t>(tag), 4));
    CHECK(!st.ok() && st.error().code == ErrorCode::InvalidArgument);
    CHECK(s.section(static_cast<std::uint8_t>(tag)) == nullptr);
  }
  // The largest legal project (32 MiB scenes) saves and loads.
  ProjectSession big;
  CHECK(big.setSection(makeSection(11, 32u * 1024u * 1024u, 0x33)).ok());
  const auto saved = big.saveState(false);
  CHECK(saved.ok());
  ProjectSession back;
  CHECK(saved.ok() && back.loadState(saved.value().data(), saved.value().size(), {}).ok());
  CHECK(back.section(11) && back.section(11)->bytes.size() == 32u * 1024u * 1024u);
  // At most 32 blocks overall: 22 feature tags fit beside the nine F01 blocks.
  ProjectSession many;
  int accepted = 0;
  for (int tag = 9; tag <= 31; ++tag)
    if (tag != 13 && many.setSection(makeSection(static_cast<std::uint8_t>(tag), 1)).ok()) ++accepted;
  CHECK_EQ(accepted, 22);
  ProjectExtras all;
  all.seed = 2;
  CHECK(many.setExtras(all).ok());
  CHECK(many.saveState(false).ok());
}

CHOP_TEST(decoder_failure_drops_only_that_section_with_one_warning) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  CHECK(a.setSection(makeSection(25, 4, 0x01)).ok());
  CHECK(a.setSection(makeSection(10, 4, 0x02)).ok());
  CHECK(a.setSection(makeSection(20, 4, 0x03)).ok());
  const auto saved = a.saveState(false).value();

  ProjectSession b;
  int groove = 0, pads = 0;
  b.registerSectionDecoder(25, "groove", [&](const StateSection& s) -> Status {
    ++groove;
    return makeError(ErrorCode::Corrupt, "nope" + std::to_string(s.bytes.size()));
  });
  b.registerSectionDecoder(10, "pads", [&](const StateSection& s) -> Status {
    ++pads;
    return s.bytes.size() == 4 ? Status{} : Status{makeError(ErrorCode::Corrupt, "size")};
  });
  CHECK(b.loadState(saved.data(), saved.size(), {}).ok());
  CHECK_EQ(groove, 1);
  CHECK_EQ(pads, 1);
  CHECK(b.section(25) == nullptr);
  CHECK(b.section(10) != nullptr && b.section(20) != nullptr);
  CHECK(b.pattern() != nullptr);
  CHECK_EQ(countWarnings(b, "Some groove settings could not be restored."), 1);
  // The dropped section is not written back.
  auto again = b.saveState(false).value();
  ProjectSession c;
  CHECK(c.loadState(again.data(), again.size(), {}).ok());
  CHECK(c.section(25) == nullptr && c.section(10) != nullptr);
  // Decoders do not run for a project without the section, and unregistering stops them.
  ProjectSession d;
  int calls = 0;
  d.registerSectionDecoder(25, "groove", [&](const StateSection&) -> Status { ++calls; return {}; });
  const auto plain = std::vector<std::uint8_t>(std::begin(v1fixture::kV1FixtureBytes), std::end(v1fixture::kV1FixtureBytes));
  CHECK(d.loadState(plain.data(), plain.size(), {}).ok());
  CHECK_EQ(calls, 0);
  d.registerSectionDecoder(25, "groove", nullptr);
  CHECK(d.loadState(saved.data(), saved.size(), {}).ok());
  CHECK_EQ(calls, 0);
  CHECK(d.section(25) != nullptr);
}

CHOP_TEST(extras_total_over_40_mib_is_rejected) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  const auto saved = a.saveState(false).value();
  const auto big = statefix::patchComposition(saved, [](std::vector<std::uint8_t>& p) {
    p.resize(statefix::kExtrasOffset);
    std::vector<std::uint8_t> t(41u * 1024u * 1024u, 0);
    t[0] = 0xE2;
    p.insert(p.end(), t.begin(), t.end());
  });
  CHECK(!big.empty());
  ProjectSession b;
  CHECK(b.loadState(big.data(), big.size(), {}).ok());  // the project loads, the extras do not
  CHECK(b.pattern() != nullptr);
  CHECK_EQ(countWarnings(b, "Some editor settings could not be restored."), 1);
  CHECK(b.extras() == ProjectExtras{});
  // A section just under the cap is fine and counted into the total.
  ProjectSession c;
  CHECK(c.setSection(makeSection(11, 32u * 1024u * 1024u)).ok());
  CHECK(c.setSection(makeSection(16, 1024 * 1024)).ok());
}

CHOP_TEST(section_blocks_survive_truncation_and_byte_flip_sweeps) {
  ProjectSession a;
  CHECK(statefix::fixtureSession(a));
  CHECK(a.setSection(makeSection(25, 24, 0x41)).ok());
  CHECK(a.setSection(makeSection(17, 12, 0x42)).ok());
  const auto saved = a.saveState(false).value();
  const auto payload = statefix::compositionPayload(saved);
  auto run = [&](const std::vector<std::uint8_t>& p) {
    const auto state = statefix::patchComposition(saved, [&](std::vector<std::uint8_t>& q) { q = p; });
    ProjectSession s;
    s.registerSectionDecoder(25, "groove", [](const StateSection& sec) -> Status {
      return sec.bytes.size() == 24 ? Status{} : Status{makeError(ErrorCode::Corrupt, "size")};
    });
    CHECK(s.loadState(state.data(), state.size(), {}).ok());
    CHECK(s.pattern() != nullptr);
    CHECK(s.saveState(false).ok());
  };
  for (std::size_t len = statefix::kExtrasOffset; len <= payload.size(); ++len) run(std::vector<std::uint8_t>(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(len)));
  for (std::size_t pos = statefix::kExtrasOffset; pos < payload.size(); ++pos)
    for (std::uint8_t x : {std::uint8_t{0x01}, std::uint8_t{0xFF}}) {
      auto q = payload;
      q[pos] ^= x;
      run(q);
    }
}
