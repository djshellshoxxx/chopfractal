// Orbit editing geometry: angle, ring and tick of a pointer position, the inverses of what the view draws.
#include <chopfractal/plugin_ui_adapter/orbit.hpp>

#include <cmath>
#include <limits>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;

CHOP_TEST(orbit_angle_of_the_four_axes) {
  const double pi = 3.14159265358979323846;
  CHECK_NEAR(pointerAngle(0, -10), 0.0, 1e-12);       // 12 o'clock
  CHECK_NEAR(pointerAngle(10, 0), pi / 2, 1e-12);     // 3 o'clock
  CHECK_NEAR(pointerAngle(0, 10), pi, 1e-12);         // 6 o'clock
  CHECK_NEAR(pointerAngle(-10, 0), 1.5 * pi, 1e-12);  // 9 o'clock
  CHECK_EQ(pointerAngle(std::numeric_limits<double>::quiet_NaN(), 1.0), 0.0);
  CHECK_EQ(pointerAngle(0, 0), 0.0);
}

CHOP_TEST(orbit_ring_at_matches_the_rings_the_view_draws) {
  OrbitView v;
  v.rings = 4;
  v.lengthTicks = 3840;
  const double R = 100.0;
  for (int ring = 0; ring < 4; ++ring) {
    OrbitArc a;
    a.ring = ring;
    const Radii rr = arcRadii(v, a, R);
    const double mid = (rr.inner + rr.outer) / 2.0;
    CHECK_EQ(ringAt(v, 0.0, -mid, R), ring);
    CHECK_EQ(ringAt(v, mid, 0.0, R), ring);
  }
  CHECK_EQ(ringAt(v, 0.0, -150.0, R), 0);  // past the rim: the outer ring
  CHECK_EQ(ringAt(v, 0.0, -2.0, R), 3);    // inside the hole: the inner ring
  CHECK_EQ(ringAt(v, std::nan(""), 0.0, R), -1);
  v.rings = 0;
  CHECK_EQ(ringAt(v, 0.0, -50.0, R), -1);
}

CHOP_TEST(orbit_angle_to_tick_inverts_the_playhead_angle) {
  for (std::int64_t len : {3840, 7680, 15360}) {
    for (double q : {0.0, 0.5, 1.0, 3.25, 7.0}) {
      if (q * 960.0 >= static_cast<double>(len)) continue;
      CHECK(std::llabs(angleToTick(playheadAngle(q, len), len) - static_cast<std::int64_t>(std::llround(q * 960.0))) <= 1);
    }
  }
  CHECK_EQ(angleToTick(6.2831853071795864, 3840), 0);  // a full turn wraps to the start
  CHECK_EQ(angleToTick(-1.0, 0), 0);
  CHECK_EQ(angleToTick(std::nan(""), 3840), 0);
  CHECK(angleToTick(-0.01, 3840) >= 0);
}
