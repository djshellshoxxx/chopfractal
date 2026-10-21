#include <chopfractal/plugin_ui_adapter/edit_geometry.hpp>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;

CHOP_TEST(snap_tick_to_grid_rounds_to_nearest_and_ties_up) {
  CHECK(snapTickToGrid(479, 240, true) == 480);
  CHECK(snapTickToGrid(120, 240, true) == 240);  // tie rounds up
  CHECK(snapTickToGrid(119, 240, true) == 0);
  CHECK(snapTickToGrid(479, 240, false) == 479);
  CHECK(snapTickToGrid(479, 0, true) == 479);
  for (Ticks t = 0; t < 3840; t += 7) {
    const Ticks s = snapTickToGrid(t, 240, true);
    CHECK(s % 240 == 0 && std::llabs(s - t) <= 120);
  }
}

CHOP_TEST(snap_marker_frame_finds_the_transient_and_the_zero_crossing) {
  std::vector<float> click(24000, 0.f);
  for (int i = 0; i < 400; ++i) click[static_cast<std::size_t>(12000 + i)] = (i % 2 ? 0.8f : -0.8f);
  const float* ch[1] = {click.data()};
  const std::int64_t t = snapMarkerFrame(ch, 1, 24000, 11950, SnapMode::Transient, 100);
  CHECK(t >= 11936 && t <= 12000);
  CHECK(std::llabs(t - 11950) <= 100);
  CHECK(snapMarkerFrame(ch, 1, 24000, 11950, SnapMode::Off, 100) == 11950);
  CHECK(snapMarkerFrame(ch, 1, 24000, 11950, SnapMode::Transient, 0) == 11950);
  CHECK(snapMarkerFrame(ch, 1, 24000, 3000, SnapMode::Transient, 50) == 3000);  // silence: nothing to snap to
  CHECK(snapMarkerFrame(ch, 1, 0, 5, SnapMode::Transient, 50) == 5);
  CHECK(snapMarkerFrame(nullptr, 1, 100, 5, SnapMode::ZeroCrossing, 50) == 5);

  std::vector<float> sine(4800);
  for (std::size_t i = 0; i < sine.size(); ++i) sine[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 100.0 * static_cast<double>(i) / 48000.0));
  const float* sc[1] = {sine.data()};
  const std::int64_t z = snapMarkerFrame(sc, 1, 4800, 1003, SnapMode::ZeroCrossing, 200);
  CHECK(z != 1003);
  CHECK(sine[static_cast<std::size_t>(z - 1)] * sine[static_cast<std::size_t>(z)] <= 0.f);
  for (std::int64_t k = 1003 - 200; k <= 1003 + 200; ++k) {  // nothing closer
    if (k < 1 || k >= 4800) continue;
    const bool cross = (sine[static_cast<std::size_t>(k - 1)] <= 0.f && sine[static_cast<std::size_t>(k)] > 0.f) || (sine[static_cast<std::size_t>(k - 1)] >= 0.f && sine[static_cast<std::size_t>(k)] < 0.f);
    if (cross) CHECK(std::llabs(k - 1003) >= std::llabs(z - 1003));
  }
  for (int i = 0; i < 1000; ++i) CHECK(snapMarkerFrame(sc, 1, 4800, 1003, SnapMode::ZeroCrossing, 200) == z);  // deterministic
  CHECK(snapRadiusFrames(1.0) == 12 && snapRadiusFrames(1000.0) == 2048 && snapRadiusFrames(0.0) == 1);
}

CHOP_TEST(clamp_marker_frame_never_crosses_a_neighbour_or_shrinks_a_region_below_the_minimum) {
  for (std::int64_t f = -500; f < 5000; f += 13) {
    const std::int64_t c = clampMarkerFrame(f, 1000, 3000, 64);
    CHECK(c >= 1064 && c <= 2936);
  }
  CHECK(clampMarkerFrame(2000, 1000, 3000, 64) == 2000);
  const std::int64_t tight = clampMarkerFrame(5000, 1000, 1100, 64);  // no legal room: stays between the neighbours
  CHECK(tight >= 1000 && tight <= 1100);
}

CHOP_TEST(lane_geometry_fits_or_scrolls_and_maps_y_to_lanes) {
  const LaneGeometry fit = laneGeometry(200.0, 4, 24.0, 50.0);
  CHECK(fit.laneHeight == 50.0 && !fit.scrollable() && fit.scroll == 0.0);
  CHECK(laneAtScrolledY(fit, 0.0) == 0 && laneAtScrolledY(fit, 199.0) == 3 && laneAtScrolledY(fit, 999.0) == 3 && laneAtScrolledY(fit, -9.0) == 0);
  const LaneGeometry sc = laneGeometry(120.0, 10, 24.0, 60.0);
  CHECK(sc.laneHeight == 24.0 && sc.scrollable() && sc.maxScroll() == 120.0 && sc.scroll == 60.0);
  CHECK(laneAtScrolledY(sc, 0.0) == 2 && laneAtScrolledY(sc, 11.0) == 2 && laneAtScrolledY(sc, 12.0) == 3 && laneAtScrolledY(sc, 36.0) == 4);
  CHECK(laneTop(sc, 2) == -12.0 && laneTop(sc, 3) == 12.0);
  CHECK(clampLaneScroll(sc, 9999.0) == 120.0 && clampLaneScroll(sc, -5.0) == 0.0);
  CHECK(laneGeometry(120.0, 10, 24.0, 9999.0).scroll == 120.0);
  CHECK(laneGeometry(120.0, 0, 24.0, 5.0).lanes == 0 && laneAtScrolledY(laneGeometry(120.0, 0, 24.0, 5.0), 3.0) == -1);
  // revealing a lane
  CHECK(scrollToRevealLane(sc, 9) == 120.0);
  CHECK(scrollToRevealLane(sc, 0) == 0.0);
  CHECK(scrollToRevealLane(sc, 4) == 60.0);  // already fully visible (96..120 is inside 60..180)
  const LaneGeometry top = laneGeometry(120.0, 10, 24.0, 0.0);
  const double revealed = scrollToRevealLane(top, 7);
  CHECK(revealed == 72.0);  // lane 7 spans 168..192; a 120 px view ends there at scroll 72
  // thumb round trip
  const Thumb t = laneScrollThumb(sc);
  CHECK(t.height >= 16.0 && t.top > 0.0 && t.top + t.height <= 120.0 + 1e-9);
  CHECK(std::fabs(scrollForThumbTop(sc, t.top) - sc.scroll) < 1e-6);
  CHECK(laneScrollThumb(fit).height == 0.0);
}

CHOP_TEST(plan_hit_move_snaps_to_the_grid_changes_lane_and_stays_inside_the_pattern) {
  const LaneGeometry lanes = laneGeometry(100.0, 4, 24.0, 0.0);  // 25 px lanes
  HitMoveInput in;
  in.barTicks = 3840;
  in.barCount = 2;
  in.gridTicks = 240;
  in.hitDuration = 240;
  in.widthPx = 768.0;  // 10 px per 100 ticks... 7680 ticks over 768 px = 10 ticks per px
  in.pointerX = 100.0;
  in.pointerY = 10.0;
  in.originalLane = 1;
  HitMove m = planHitMove(in, lanes);
  CHECK(m.bar == 0 && m.startInBar == 960 + 0 * 1 && m.lane == 0);  // x 100 -> tick 1000 -> 960
  in.pointerX = 400.0;  // tick 4000 -> bar 1, 160 into it -> snapped 4080-3840=240? nearest multiple of 240 to 4000 is 4080
  m = planHitMove(in, lanes);
  CHECK(m.bar == 1 && m.startAbs % 240 == 0 && m.startAbs == 3840 + m.startInBar);
  in.pointerX = 99999.0;  // far right: the last slot that still holds a hit
  m = planHitMove(in, lanes);
  CHECK(m.startAbs <= 7680 - 240 && m.startAbs % 240 == 0 && m.bar == 1);
  in.pointerX = -50.0;
  CHECK(planHitMove(in, lanes).startAbs == 0);
  in.snap = false;
  in.pointerX = 101.5;  // 10 ticks per px
  CHECK(planHitMove(in, lanes).startAbs == 1015);
  in.grabOffsetTicks = 100;
  CHECK(planHitMove(in, lanes).startAbs == 915);
  in.pointerY = 80.0;
  CHECK(planHitMove(in, lanes).lane == 3);
  // half a lane down from lane 1 stays lane 1 until the pointer crosses the lane edge
  in.pointerY = 49.0;
  CHECK(planHitMove(in, lanes).lane == 1);
  in.pointerY = 51.0;
  CHECK(planHitMove(in, lanes).lane == 2);
  HitMoveInput bad;
  CHECK(planHitMove(bad, lanes).startAbs == 0);
}

CHOP_TEST(plan_hit_resize_respects_the_minimum_and_the_bar_end_and_lands_on_the_grid) {
  // 2 bars of 3840 over 768 px: 10 ticks per px
  CHECK(planHitResize(3840, 2, 240, true, 0, 0, 0.0, 768.0) == 240);              // never below one cell while snapping
  CHECK(planHitResize(3840, 2, 240, false, 0, 0, 0.0, 768.0) == kMinHitTicks);    // free resize bottoms out at 1/64
  CHECK(planHitResize(3840, 2, 240, true, 0, 0, 100.0, 768.0) == 960);
  CHECK(planHitResize(3840, 2, 240, true, 3600, 0, 700.0, 768.0) == 240);         // clipped to the bar end (3840 - 3600)
  CHECK(planHitResize(3840, 2, 240, true, 240, 1, 380.0, 768.0) % 240 == 0);
  CHECK(planHitResize(3840, 2, 240, true, 3780, 0, 400.0, 768.0) == 60);          // only 60 ticks of room left
  CHECK(planHitResize(0, 2, 240, true, 0, 0, 5.0, 768.0) == kMinHitTicks);
  for (double x = 0.0; x < 768.0; x += 3.7) {  // snapped ends are multiples of the grid
    const Ticks d = planHitResize(3840, 2, 240, true, 480, 0, x, 768.0);
    CHECK(d >= 240 && 480 + d <= 3840 && (480 + d) % 240 == 0);
  }
}
