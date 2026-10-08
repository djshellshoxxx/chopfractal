#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <cmath>
#include <map>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;

namespace {
ChopSnapshot makeChops(int n = 4) {
  ChopSnapshot s;
  s.sourceFrames = 12000 * n;
  s.sampleRate = 48000;
  for (int i = 0; i < n; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 12000ll, (i + 1) * 12000ll}, 0, 0, 0});
  return s;
}
pattern::Pattern makePattern(const ChopSnapshot& c) {
  pattern::Settings s;
  s.bars = 2;
  s.density = 0.8;
  return pattern::generate(c, s).value();
}
}  // namespace

CHOP_TEST(peaks_report_min_and_max_per_bin) {
  std::vector<float> x(1000);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = i < 500 ? 0.5f : -0.25f;
  const float* ch[1] = {x.data()};
  PeakBins p = computePeaks(ch, 1, 1000, {0, 1000}, 2);
  CHECK(p.min.size() == 2 && p.max.size() == 2);
  CHECK_NEAR(p.max[0], 0.5, 0.0);
  CHECK_NEAR(p.min[0], 0.5, 0.0);
  CHECK_NEAR(p.min[1], -0.25, 0.0);
  PeakBins zoomed = computePeaks(ch, 1, 1000, {490, 510}, 2);  // straddles the step
  CHECK_NEAR(zoomed.max[0], 0.5, 0.0);
  CHECK_NEAR(zoomed.min[1], -0.25, 0.0);
  PeakBins more = computePeaks(ch, 1, 1000, {0, 3}, 10);  // more bins than frames
  CHECK_EQ(more.max.size(), 10u);
  const float* two[2] = {x.data(), x.data()};
  CHECK_NEAR(computePeaks(two, 2, 1000, {0, 1000}, 1).max[0], 0.5, 0.0);
  CHECK(computePeaks(nullptr, 1, 10, {0, 10}, 4).min.empty());
  CHECK(computePeaks(ch, 1, 1000, {2000, 3000}, 4).min.empty());
  CHECK(computePeaks(ch, 1, 1000, {-50, 20}, 4).min.size() == 4);  // clamped to the clip
}

CHOP_TEST(time_mapping_does_not_drift_under_repeated_zoom_and_scroll) {
  const std::int64_t total = 48000 * 60;
  ViewWindow w{0, total, 1200.0};
  const std::vector<std::int64_t> probes{0, 1, 123456, total - 1};
  for (std::int64_t f : probes) {
    const double x = frameToX(w, f);
    CHECK(std::llabs(xToFrame(w, x) - f) <= total / 1200 + 1);
  }
  // Zoom in on an anchor many times, then back out: the anchor keeps its screen position.
  ViewWindow z = w;
  const std::int64_t anchor = 1000000;
  for (int i = 0; i < 10; ++i) z = zoomAround(z, anchor, 1.5, total);
  CHECK(z.endFrame - z.startFrame < total / 50);
  CHECK(z.startFrame >= 0 && z.endFrame <= total);
  CHECK(anchor >= z.startFrame && anchor < z.endFrame);  // the anchored frame stays on screen
  for (int i = 0; i < 40; ++i) z = zoomAround(z, anchor, 1.0 / 1.5, total);
  CHECK(z.startFrame == 0 && z.endFrame == total);  // fully zoomed out lands exactly on the clip
  ViewWindow s = scrolledBy(ViewWindow{100, 1100, 1000.0}, -5000.0, total);
  CHECK(s.startFrame == 0 && s.endFrame == 1000);
  s = scrolledBy(ViewWindow{100, 1100, 1000.0}, 1e12, total);
  CHECK(s.endFrame == total && s.endFrame - s.startFrame == 1000);
  CHECK(zoomAround(w, 5, -2.0, total).endFrame == w.endFrame);  // invalid factors are ignored
}

CHOP_TEST(marker_views_flag_visibility_and_state) {
  source::SourceInfo info;
  info.sampleRate = 48000;
  info.frames = 48000;
  auto map = source::ChopMap::create(info).value();
  auto a = map.addMarker(0).value();
  auto b = map.addMarker(30000).value();
  map.setLabel(a, "kick");
  map.setEnabled(b, false);
  auto views = buildMarkerViews(map, ViewWindow{0, 24000, 480.0});
  CHECK_EQ(views.size(), 2u);
  CHECK(views[0].visible && views[0].label == "kick" && views[0].x == 0.0);
  CHECK(!views[1].visible && !views[1].enabled);
  CHECK_NEAR(views[1].x, 600.0, 1e-9);
}

CHOP_TEST(pattern_view_matches_the_flattened_events) {
  const ChopSnapshot c = makeChops();
  const pattern::Pattern p = makePattern(c);
  PatternView v = buildPatternView(p, c, [](ChopId id) { return id.value == 1 ? std::string("kick") : std::string(); });
  auto flat = pattern::flatten(p, c).value();
  CHECK_EQ(v.rects.size(), flat.size());
  CHECK_EQ(v.barLines.size(), 2u);
  CHECK_EQ(v.beatLines.size(), 8u);
  CHECK_NEAR(v.barLines[1], 0.5, 1e-12);
  std::set<std::uint64_t> flatIds;
  for (const FlatEvent& f : flat) flatIds.insert(f.id.value);
  const double len = static_cast<double>(v.lengthTicks);
  for (const EventRect& r : v.rects) {
    CHECK(flatIds.count(r.id.value) == 1);
    CHECK(r.x0 >= 0.0 && r.x1 <= 1.0 + 1e-12 && r.x1 > r.x0);
    for (const FlatEvent& f : flat)
      if (f.id == r.id) CHECK_NEAR(r.x0 * len, static_cast<double>(f.start), 1e-6);
    CHECK(!r.description.empty());
    CHECK(r.chopIndex >= 0);
  }
  bool sawRole = false;
  for (const EventRect& r : v.rects) sawRole = sawRole || r.description.find("(kick)") != std::string::npos;
  CHECK(sawRole);
}

CHOP_TEST(nested_hits_appear_inside_their_parent_and_breadcrumbs_name_the_path) {
  const ChopSnapshot c = makeChops();
  pattern::Pattern p = makePattern(c);
  const Event* parent = nullptr;
  for (const auto& bt : p.bars[0].beats)
    if (!bt.events.empty()) {
      parent = &bt.events.front();
      break;
    }
  CHECK(parent != nullptr);
  if (!parent) return;
  auto child = std::make_shared<NestedPattern>();
  child->windowDuration = parent->duration;
  for (int i = 0; i < 2; ++i) {
    Event e;
    e.id = EventId{9000 + static_cast<std::uint64_t>(i)};
    e.chop = parent->chop;
    e.start = parent->duration / 2 * i;
    e.duration = parent->duration / 2;
    child->events.push_back(e);
  }
  p.nextId = 10000;
  auto zoomed = pattern::setChild(p, parent->id, child, c);
  CHECK(zoomed.ok());
  if (!zoomed.ok()) return;
  PatternView v = buildPatternView(zoomed.value(), c);
  const EventRect* pr = nullptr;
  int nested = 0;
  for (const EventRect& r : v.rects)
    if (r.id == parent->id) pr = &r;
  CHECK(pr != nullptr && pr->hasChild && pr->childActive);
  for (const EventRect& r : v.rects)
    if (r.depth == 1) {
      ++nested;
      CHECK(r.parent == parent->id && pr && r.x0 >= pr->x0 - 1e-12 && r.x1 <= pr->x1 + 1e-12);
    }
  CHECK_EQ(nested, 2);
  const auto crumbs = breadcrumbFor(zoomed.value(), EventId{9001});
  CHECK(crumbs.size() == 5 && crumbs[0] == "Phrase" && crumbs[1].rfind("Bar ", 0) == 0 && crumbs[3].rfind("Hit ", 0) == 0 && crumbs[4] == "Child 2");
  CHECK(breadcrumbFor(zoomed.value(), EventId{424242}).empty());
  CHECK(breadcrumbFor(zoomed.value(), parent->id).size() == 4);
}

CHOP_TEST(history_layout_gives_leaves_columns_and_parents_depth) {
  using history::NodeSummary;
  auto node = [](history::NodeId id, history::NodeId parent, std::uint64_t order, const char* label) {
    NodeSummary n;
    n.id = id;
    n.parent = parent;
    n.order = order;
    n.label = label;
    return n;
  };
  // root(1) -> a(2) -> c(4); root -> b(3); second root(5)
  std::vector<NodeSummary> nodes{node(1, 0, 1, "root"), node(2, 1, 2, "a"), node(3, 1, 3, "b"), node(4, 2, 4, "c"), node(5, 0, 5, "other")};
  nodes[2].favorite = true;
  auto layout = layoutHistoryTree(nodes, 4);
  CHECK_EQ(layout.size(), 5u);
  std::map<history::NodeId, TreeNodeView> by;
  for (const auto& n : layout) by[n.id] = n;
  CHECK(by[1].depth == 0 && by[2].depth == 1 && by[4].depth == 2 && by[5].depth == 0);
  CHECK(by[4].active && !by[1].active && by[3].favorite);
  CHECK(by[4].column == 0 && by[3].column == 1 && by[5].column == 2);  // leaves in order
  CHECK(by[1].column == by[4].column && by[2].column == by[4].column);  // parents above their first child
  CHECK(layoutHistoryTree({}, 0).empty());
  // An orphan whose parent is missing is shown as a root rather than dropped.
  CHECK_EQ(layoutHistoryTree({node(9, 77, 1, "orphan")}, 0).size(), 1u);
}
