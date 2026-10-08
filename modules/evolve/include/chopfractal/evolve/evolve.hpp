#pragma once
// Deterministic scheduler for Evolve mode. No dependencies; performs no mutation itself.
#include <cstdint>
#include <optional>

namespace chopfractal::evolve {

constexpr int kMaxFailures = 3;

struct Settings {
  bool enabled = false;
  int everyLoops = 4;          // 1, 2, 4, 8 or 16
  double amount = 0.2;         // [0, 1] share of the unlocked pattern each step may change
  double ramp = 0.0;           // [0, 1] added to the amount per step (a build-up)
  double rampCeiling = 0.8;    // [0, 1] the amount never ramps above max(amount, ceiling)
  std::uint64_t startSeed = 1;
};

bool valid(const Settings& s);

// Pure and deterministic; independent of time and of how often the controller is polled.
std::uint64_t seedForStep(std::uint64_t startSeed, std::uint64_t stepIndex);
double amountForStep(const Settings& s, std::uint64_t stepIndex);

struct Step {
  std::uint64_t index = 0;
  std::uint64_t seed = 0;
  double amount = 0.0;
};

enum class StopReason : std::uint8_t { None, Stopped, Failures };

class Controller {
 public:
  // Returns false (and stays stopped) for invalid settings or when `enabled` is false.
  bool start(const Settings& s);
  void stop();
  bool running() const { return running_; }
  // Call once per loop at its midpoint. Returns a step on every everyLoops-th call.
  std::optional<Step> onLoopMidpoint();
  void reportSuccess() { failures_ = 0; }
  void reportFailure();
  std::uint64_t stepsTaken() const { return steps_; }
  int failures() const { return failures_; }
  StopReason stopReason() const { return reason_; }
  const Settings& settings() const { return settings_; }

 private:
  Settings settings_;
  bool running_ = false;
  std::uint64_t steps_ = 0;
  std::uint64_t loops_ = 0;
  int failures_ = 0;
  StopReason reason_ = StopReason::None;
};

}  // namespace chopfractal::evolve
