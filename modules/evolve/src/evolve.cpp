#include <chopfractal/evolve/evolve.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::evolve {
namespace {
std::uint64_t mix(std::uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
bool in01(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }
}  // namespace

bool valid(const Settings& s) {
  const int n = s.everyLoops;
  return (n == 1 || n == 2 || n == 4 || n == 8 || n == 16) && in01(s.amount) && in01(s.ramp) && in01(s.rampCeiling);
}

std::uint64_t seedForStep(std::uint64_t startSeed, std::uint64_t stepIndex) { return mix(startSeed ^ mix(stepIndex + 1)); }

double amountForStep(const Settings& s, std::uint64_t stepIndex) {
  const double cap = std::max(s.amount, s.rampCeiling);
  return std::min(cap, s.amount + s.ramp * static_cast<double>(stepIndex));
}

bool Controller::start(const Settings& s) {
  stop();
  reason_ = StopReason::None;
  steps_ = loops_ = 0;
  failures_ = 0;
  settings_ = s;
  if (!s.enabled || !valid(s)) return false;
  running_ = true;
  return true;
}

void Controller::stop() {
  if (running_) reason_ = StopReason::Stopped;
  running_ = false;
}

std::optional<Step> Controller::onLoopMidpoint() {
  if (!running_) return std::nullopt;
  ++loops_;
  if (loops_ % static_cast<std::uint64_t>(settings_.everyLoops) != 0) return std::nullopt;
  Step st;
  st.index = steps_++;
  st.seed = seedForStep(settings_.startSeed, st.index);
  st.amount = amountForStep(settings_, st.index);
  return st;
}

void Controller::reportFailure() {
  if (++failures_ >= kMaxFailures && running_) {
    running_ = false;
    reason_ = StopReason::Failures;
  }
}

}  // namespace chopfractal::evolve
