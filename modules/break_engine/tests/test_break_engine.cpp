#include <chopfractal/break_engine/break_engine.hpp>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::breakeng;

CHOP_TEST(smoke_plan_has_anchors) {
  PlanSettings s;
  const auto plans = planPattern(s);
  CHECK_EQ(plans.size(), 4u);
  CHECK(!plans[0].hits.empty());
}
