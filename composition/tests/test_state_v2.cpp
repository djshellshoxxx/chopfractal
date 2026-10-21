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
