#include <cstdlib>
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/source_chop/analysis.hpp>
#include <chopfractal/source_chop/chop_map.hpp>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::source;

namespace {
SourceInfo info(std::int64_t frames = 48000) {
  SourceInfo i;
  i.id = SourceId{99};
  i.name = "loop";
  i.channels = 1;
  i.sampleRate = 48000;
  i.frames = frames;
  return i;
}

ChopMap makeMap(std::int64_t frames = 48000) {
  auto m = ChopMap::create(info(frames));
  return m.value();
}

// Decaying noise bursts at known positions over silence.
std::vector<float> drumLoop(const std::vector<std::int64_t>& hits, std::int64_t frames) {
  std::vector<float> x(static_cast<std::size_t>(frames), 0.f);
  Rng rng(5);
  for (std::int64_t h : hits)
    for (std::int64_t i = 0; i < 4000 && h + i < frames; ++i) {
      const float env = std::exp(-static_cast<float>(i) / 600.f);
      x[static_cast<std::size_t>(h + i)] += env * static_cast<float>(rng.uniform01() * 2.0 - 1.0);
    }
  return x;
}
}  // namespace

CHOP_TEST(create_rejects_invalid_sources) {
  SourceInfo bad = info();
  bad.frames = 0;
  CHECK(!ChopMap::create(bad).ok());
  bad = info();
  bad.channels = 3;
  CHECK(!ChopMap::create(bad).ok());
  bad = info();
  bad.sampleRate = 100;
  CHECK(!ChopMap::create(bad).ok());
  CHECK(!ChopMap::create(info(), 0).ok());
  CHECK(!ChopMap::create(info(100), 80).ok());
  CHECK(ChopMap::create(info()).ok());
}

CHOP_TEST(marker_editing_enforces_bounds_spacing_and_order) {
  ChopMap m = makeMap();
  auto a = m.addMarker(0);
  auto b = m.addMarker(12000);
  auto c = m.addMarker(24000);
  CHECK(a.ok() && b.ok() && c.ok());
  CHECK(!m.addMarker(-1).ok());
  CHECK(!m.addMarker(48000).ok());            // would leave less than the minimum region
  CHECK(!m.addMarker(12010).ok());            // closer than the minimum region length
  CHECK(!m.addMarker(12000).ok());            // duplicate position
  CHECK(m.moveMarker(b.value(), 13000).ok());
  CHECK(!m.moveMarker(b.value(), 25000).ok());  // would cross a neighbour
  CHECK(!m.moveMarker(b.value(), 24030).ok());  // too close to the next marker
  CHECK(!m.moveMarker(ChopId{777}, 5).ok());
  CHECK_EQ(m.markers().size(), 3u);
  CHECK_EQ(m.markers()[1].position, 13000);
  CHECK(m.deleteMarker(c.value()).ok());
  CHECK(!m.deleteMarker(c.value()).ok());
}

CHOP_TEST(regions_follow_enabled_markers_to_the_clip_end) {
  ChopMap m = makeMap();
  auto a = m.addMarker(0).value();
  auto b = m.addMarker(10000).value();
  auto c = m.addMarker(30000).value();
  auto regs = m.regions();
  CHECK_EQ(regs.size(), 3u);
  CHECK(regs[0].range == (SampleRange{0, 10000}));
  CHECK(regs[2].range == (SampleRange{30000, 48000}));
  CHECK(m.setEnabled(b, false).ok());
  regs = m.regions();
  CHECK_EQ(regs.size(), 2u);
  CHECK(regs[0].id == a && regs[0].range == (SampleRange{0, 30000}));
  CHECK(regs[1].id == c);
}

CHOP_TEST(undo_redo_and_failed_edits_leave_state_untouched) {
  ChopMap m = makeMap();
  CHECK(!m.canUndo());
  m.addMarker(0);
  m.addMarker(10000);
  CHECK(!m.addMarker(10001).ok());  // failed edit must not create an undo step
  CHECK(m.undo());
  CHECK_EQ(m.markers().size(), 1u);
  CHECK(m.redo());
  CHECK_EQ(m.markers().size(), 2u);
  CHECK(m.undo() && m.undo());
  CHECK(!m.undo());
  CHECK(m.markers().empty());
}

CHOP_TEST(snapshot_has_enabled_chops_with_trims_groups_and_fades) {
  ChopMap m = makeMap();
  auto a = m.addMarker(0).value();
  auto b = m.addMarker(10000).value();
  auto c = m.addMarker(20000).value();
  CHECK(m.setTrim(a, 100, 200).ok());
  CHECK(m.setGroup(c, 7).ok());
  CHECK(m.setFades(c, 12, 34).ok());
  CHECK(m.setEnabled(b, false).ok());
  // With b disabled, a's region is 20000 frames; a trim of 20000 would leave less than the minimum.
  CHECK(!m.setTrim(a, 10000, 10000).ok());
  auto snap = m.snapshot();
  CHECK_EQ(snap->chops.size(), 2u);
  CHECK(snap->chops[0].range == (SampleRange{100, 20000 - 200}));
  CHECK_EQ(snap->chops[1].group, 7u);
  CHECK_EQ(snap->chops[1].fadeInFrames, 12u);
  CHECK_EQ(snap->sourceFrames, 48000);
  CHECK(snap->find(c) != nullptr && snap->find(b) == nullptr);
}

CHOP_TEST(at_least_128_chops_can_be_represented) {
  ChopMap m = makeMap(48000);
  for (int i = 0; i < 200; ++i) CHECK(m.addMarker(i * 200).ok());
  CHECK_EQ(m.snapshot()->chops.size(), 200u);
}

CHOP_TEST(transient_detection_finds_known_hits) {
  const std::vector<std::int64_t> hits{0, 12000, 24000, 36000, 60000, 84000};
  const std::int64_t frames = 96000;
  auto x = drumLoop(hits, frames);
  const float* ch[1] = {x.data()};
  Proposal p = detectTransients(ch, 1, frames, 48000);
  CHECK_EQ(p.markers.size(), hits.size());
  for (std::size_t i = 0; i < hits.size() && i < p.markers.size(); ++i) {
    CHECK(std::llabs(p.markers[i].position - hits[i]) < 300);
    CHECK(p.markers[i].strength > 0.f && p.markers[i].strength <= 1.f);
  }
}

CHOP_TEST(sensitivity_and_spacing_control_detection_density) {
  std::vector<std::int64_t> hits;
  for (int i = 0; i < 16; ++i) hits.push_back(i * 6000);
  auto x = drumLoop(hits, 96000);
  const float* ch[1] = {x.data()};
  AnalysisParams lo, hi;
  lo.sensitivity = 0.0;
  hi.sensitivity = 1.0;
  CHECK(detectTransients(ch, 1, 96000, 48000, hi).markers.size() >= detectTransients(ch, 1, 96000, 48000, lo).markers.size());
  AnalysisParams wide = hi;
  wide.minSpacingFrames = 20000;
  auto spaced = detectTransients(ch, 1, 96000, 48000, wide);
  for (std::size_t i = 1; i < spaced.markers.size(); ++i) CHECK(spaced.markers[i].position - spaced.markers[i - 1].position >= 20000);
}

CHOP_TEST(analysis_is_safe_on_silence_and_invalid_input) {
  std::vector<float> silence(48000, 0.f);
  const float* ch[1] = {silence.data()};
  Proposal p = detectTransients(ch, 1, 48000, 48000);
  CHECK_EQ(p.markers.size(), 1u);
  CHECK(detectTransients(nullptr, 1, 100, 48000).markers.empty());
  CHECK(detectTransients(ch, 0, 100, 48000).markers.empty());
  CHECK(detectTransients(ch, 1, 0, 48000).markers.empty());
  CHECK_EQ(detectTransients(ch, 1, 100, 48000).markers.size(), 1u);  // shorter than one window
}

CHOP_TEST(even_grid_straight_triplet_dotted_without_drift) {
  CHECK_EQ(evenGrid(48000, {4.0, 8, GridKind::Straight}).markers.size(), 8u);
  CHECK_EQ(evenGrid(48000, {4.0, 8, GridKind::Triplet}).markers.size(), 12u);
  CHECK_EQ(evenGrid(48000, {4.0, 8, GridKind::Dotted}).markers.size(), 5u);
  const std::int64_t big = 48000ll * 60;
  auto g = evenGrid(big, {64.0, 16, GridKind::Triplet});
  for (std::size_t k = 0; k < g.markers.size(); ++k) {
    const double exact = static_cast<double>(k) * static_cast<double>(big) * (4.0 / 16.0 * 2.0 / 3.0) / 64.0;
    CHECK(std::fabs(static_cast<double>(g.markers[k].position) - exact) <= 0.5);
  }
  CHECK(evenGrid(0, {}).markers.empty());
  CHECK(evenGrid(100, {0.0, 8, GridKind::Straight}).markers.empty());
}

CHOP_TEST(proposals_preview_replace_merge_and_undo_in_one_step) {
  ChopMap m = makeMap();
  auto manual = m.addMarker(5000).value();
  Proposal p;
  p.markers = {{0, 1.f}, {5010, 0.5f}, {20000, 0.9f}, {47990, 0.4f}};
  auto merged = m.preview(p, MergeMode::Merge);
  CHECK(merged.ok());
  CHECK_EQ(merged.value().size(), 3u);  // 5010 and 47990 are rejected, manual 5000 stays
  CHECK_EQ(m.markers().size(), 1u);     // preview applies nothing
  CHECK(m.applyProposal(p, MergeMode::Merge).ok());
  CHECK_EQ(m.markers().size(), 3u);
  CHECK(m.find(manual) != nullptr && m.find(manual)->manual);
  CHECK(m.undo());
  CHECK_EQ(m.markers().size(), 1u);
  CHECK(m.applyProposal(p, MergeMode::Replace).ok());
  CHECK(m.find(manual) == nullptr);
  // Replace clears the manual marker, so 5010 no longer conflicts; 47990 is too close to the end.
  CHECK_EQ(m.markers().size(), 3u);  // 0, 5010, 20000
}

CHOP_TEST(snap_helpers) {
  const std::vector<std::int64_t> targets{1000, 2000, 3000};
  CHECK_EQ(snapToNearest(2040, targets, 100), 2000);
  CHECK_EQ(snapToNearest(2500, targets, 100), 2500);
  std::vector<float> sine(1000);
  for (std::size_t i = 0; i < sine.size(); ++i) sine[i] = std::sin(static_cast<float>(i) * 0.1f + 0.5f);
  const std::int64_t z = snapToZeroCrossing(sine.data(), 1000, 100, 50);
  CHECK(std::fabs(sine[static_cast<std::size_t>(z)]) < 0.06f);
  CHECK_EQ(snapToZeroCrossing(nullptr, 0, 5, 5), 5);
}

CHOP_TEST(source_identity_is_deterministic_and_content_sensitive) {
  std::vector<float> a(1000, 0.25f);
  const float* ch[1] = {a.data()};
  const SourceId id1 = computeSourceId(ch, 1, 1000, 48000);
  CHECK(id1 == computeSourceId(ch, 1, 1000, 48000));
  a[500] = 0.3f;
  CHECK(id1 != computeSourceId(ch, 1, 1000, 48000));
  CHECK(id1 != computeSourceId(ch, 1, 1000, 44100));
  CHECK(id1.valid());
}

CHOP_TEST(state_roundtrips_and_rejects_malformed_input) {
  ChopMap m = makeMap();
  auto a = m.addMarker(0).value();
  m.addMarker(10000);
  m.setLabel(a, "kick");
  m.setGroup(a, 3);
  m.setTrim(a, 10, 20);
  const auto blob = m.serialize();
  auto back = ChopMap::deserialize(blob.data(), blob.size());
  CHECK(back.ok());
  CHECK_EQ(back.value().markers().size(), 2u);
  CHECK(back.value().markers()[0].label == "kick");
  CHECK_EQ(back.value().markers()[0].group, 3u);
  CHECK_EQ(back.value().info().frames, 48000);
  CHECK(back.value().serialize() == blob);
  // New ids continue after restore.
  auto next = back.value().addMarker(30000);
  CHECK(next.ok() && next.value().value > 2);
  for (std::size_t cut = 0; cut < blob.size(); ++cut) CHECK(!ChopMap::deserialize(blob.data(), cut).ok());
  auto bad = blob;
  bad[blob.size() - 3] ^= 0xFF;
  (void)ChopMap::deserialize(bad.data(), bad.size());  // must not crash
}
