// Audit follow-ups: Replace keeps manual markers, Smart Setup merges with existing markers, the family tree follows
// Undo/Redo/recall, and A/B recall repairs references to deleted chops.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>

#include "chop_test.hpp"
#include "session_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;
using fixture::loopAudio;
using fixture::settings;

namespace {
bool sawNotice(ProjectSession& s, const std::string& part) {
  bool found = false;
  for (const Notice& n : s.takeNotices()) found = found || n.text.find(part) != std::string::npos;
  return found;
}
std::size_t manualCount(const ProjectSession& s) {
  return static_cast<std::size_t>(std::count_if(s.chopMap()->markers().begin(), s.chopMap()->markers().end(), [](const source::Marker& m) { return m.manual; }));
}
}  // namespace

CHOP_TEST(detect_replace_keeps_manual_markers_and_smart_setup_merges_with_existing_markers) {
  ProjectSession s;
  CHECK(s.loadSource(loopAudio(), "loop", "").ok());
  ChopId mine;
  CHECK(s.editMarkers([&](source::ChopMap& m) {
    auto r = m.addMarker(30000, true);
    if (r.ok()) mine = r.value();
    return r.ok() ? Status{} : Status{r.error()};
  }).ok());
  DetectOptions d;  // Replace by default
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  CHECK(s.chopMap()->find(mine) != nullptr && s.chopMap()->find(mine)->manual);
  CHECK(s.chopMap()->markers().size() > 1);
  const auto previewed = s.previewChops(d);
  CHECK(previewed.ok() && std::any_of(previewed.value().begin(), previewed.value().end(), [&](const source::Marker& m) { return m.id == mine; }));
  CHECK(s.applyChops(d).ok());  // a repeat drops the old automatic markers only
  CHECK_EQ(manualCount(s), 1u);

  // Smart Setup with only a manual marker present still detects, and keeps the marker.
  ProjectSession t;
  CHECK(t.loadSource(loopAudio(), "loop", "").ok());
  CHECK(t.editMarkers([&](source::ChopMap& m) { return m.addMarker(30000, true).ok() ? Status{} : Status{makeError(ErrorCode::Conflict, "x")}; }).ok());
  CHECK_EQ(t.chopMap()->markers().size(), 1u);
  auto setup = t.smartSetup(120.0);
  CHECK(setup.ok());
  CHECK(t.chopMap()->markers().size() > 1);
  CHECK_EQ(manualCount(t), 1u);
  CHECK(std::any_of(t.chopMap()->markers().begin(), t.chopMap()->markers().end(), [](const source::Marker& m) { return m.position == 30000 && m.manual; }));
}

CHOP_TEST(undo_redo_and_recall_move_the_family_tree_with_the_pattern) {
  ProjectSession s;
  CHECK(fixture::ready(s, false));
  CHECK(s.generate(settings(1)).ok());
  const auto n1 = s.history().active();
  CHECK(s.generate(settings(2)).ok());
  const auto n2 = s.history().active();
  CHECK(s.generate(settings(3)).ok());
  const auto n3 = s.history().active();
  CHECK(n1 != n2 && n2 != n3);
  CHECK(s.storeSnapshot(0).ok());  // holds seed 3
  CHECK(s.undo());
  CHECK_EQ(s.history().active(), n2);
  CHECK_EQ(s.pattern()->settings.seed, 2u);
  CHECK(s.undo());
  CHECK_EQ(s.history().active(), n1);
  CHECK(s.redo());
  CHECK_EQ(s.history().active(), n2);
  CHECK(s.redo());
  CHECK_EQ(s.history().active(), n3);
  CHECK(s.undo() && s.recallSnapshot(0).ok());
  CHECK_EQ(s.history().active(), n3);
  // New work after Undo branches from the node the pattern is on.
  CHECK(s.undo() && s.undo());
  CHECK_EQ(s.history().active(), n1);
  CHECK(s.generate(settings(9)).ok());
  const auto n4 = s.history().active();
  CHECK(s.history().ancestors(n4).back() == n1);
}

CHOP_TEST(recall_repairs_references_to_deleted_chops_instead_of_failing) {
  ProjectSession s;
  CHECK(fixture::ready(s, false));
  CHECK(s.generate(settings(5, 4)).ok());
  const ChopId victim = s.chops()->chops[3].id;
  bool used = false;
  for (const auto& b : s.pattern()->bars)
    for (const auto& bt : b.beats)
      for (const auto& e : bt.events) used = used || e.chop == victim;
  CHECK(used);
  CHECK(s.storeSnapshot(0).ok());
  CHECK(s.generate(settings(6, 4)).ok());
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(victim); }).ok());
  s.takeNotices();
  CHECK(s.recallSnapshot(0).ok());
  CHECK(pattern::flatten(*s.pattern(), *s.chops()).ok());
  for (const auto& b : s.pattern()->bars)
    for (const auto& bt : b.beats)
      for (const auto& e : bt.events) CHECK(e.chop != victim);
  CHECK(sawNotice(s, "Snapshot A used chops that no longer exist"));
  CHECK(s.undo());  // the recall was one undo step
}

CHOP_TEST(redo_returns_to_the_node_recorded_with_an_identical_pattern) {
  ProjectSession s;
  CHECK(fixture::ready(s, false));
  CHECK(s.generate(settings(1)).ok());
  const auto n1 = s.history().active();
  pattern::MutateOptions mo;
  CHECK(s.mutate(mo).ok());
  const auto n2 = s.history().active();
  CHECK(s.generate(settings(1)).ok());
  const auto n3 = s.history().active();
  CHECK(n1 != n2 && n2 != n3 && n1 != n3);
  CHECK(s.undo());
  CHECK_EQ(s.history().active(), n2);
  CHECK(s.redo());
  CHECK_EQ(s.history().active(), n3);  // not the ancestor n1 with the same bytes
}
