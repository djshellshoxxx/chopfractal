#include <chopfractal/evolve/evolve.hpp>
#include <cmath>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal::evolve;

namespace {
Settings on(int every = 2) {
  Settings s;
  s.enabled = true;
  s.everyLoops = every;
  s.startSeed = 42;
  return s;
}
}  // namespace

CHOP_TEST(steps_follow_the_cadence_and_are_reproducible) {
  for (int every : {1, 2, 4, 8, 16}) {
    Controller c;
    CHECK(c.start(on(every)));
    int steps = 0;
    for (int loop = 1; loop <= 64; ++loop) {
      auto st = c.onLoopMidpoint();
      CHECK(st.has_value() == (loop % every == 0));
      if (st) {
        CHECK_EQ(st->index, static_cast<std::uint64_t>(steps));
        ++steps;
      }
    }
    CHECK_EQ(steps, 64 / every);
  }
  Controller a, b;
  a.start(on());
  b.start(on());
  for (int i = 0; i < 40; ++i) {
    auto x = a.onLoopMidpoint(), y = b.onLoopMidpoint();
    CHECK(x.has_value() == y.has_value());
    if (x && y) CHECK(x->seed == y->seed && x->amount == y->amount);
  }
  CHECK(seedForStep(1, 0) != seedForStep(1, 1) && seedForStep(1, 0) != seedForStep(2, 0));
}

CHOP_TEST(disabled_or_invalid_settings_never_step) {
  Controller c;
  Settings s = on();
  s.enabled = false;
  CHECK(!c.start(s));
  for (int i = 0; i < 10; ++i) CHECK(!c.onLoopMidpoint());
  for (int bad : {0, 3, 5, 32}) {
    s = on();
    s.everyLoops = bad;
    CHECK(!valid(s) && !c.start(s));
  }
  s = on();
  s.amount = 1.5;
  CHECK(!valid(s));
  s = on();
  s.ramp = std::nan("");
  CHECK(!valid(s));
}

CHOP_TEST(ramp_is_monotonic_and_capped) {
  Settings s = on();
  s.amount = 0.1;
  s.ramp = 0.1;
  s.rampCeiling = 0.5;
  double last = -1;
  for (std::uint64_t i = 0; i < 20; ++i) {
    const double a = amountForStep(s, i);
    CHECK(a >= last && a <= 0.5 + 1e-12);
    last = a;
  }
  CHECK_NEAR(amountForStep(s, 0), 0.1, 1e-12);
  CHECK_NEAR(amountForStep(s, 100), 0.5, 1e-12);
  s.amount = 0.9;  // an amount above the ceiling is never reduced by the ramp
  CHECK_NEAR(amountForStep(s, 50), 0.9, 1e-12);
}

CHOP_TEST(three_consecutive_failures_stop_evolve_and_success_resets) {
  Controller c;
  c.start(on(1));
  c.reportFailure();
  c.reportFailure();
  c.reportSuccess();
  c.reportFailure();
  c.reportFailure();
  CHECK(c.running());
  c.reportFailure();
  CHECK(!c.running() && c.stopReason() == StopReason::Failures);
  CHECK(!c.onLoopMidpoint());
  CHECK(c.start(on(1)) && c.running() && c.failures() == 0 && c.stepsTaken() == 0);
  c.stop();
  CHECK(!c.running() && c.stopReason() == StopReason::Stopped);
}
