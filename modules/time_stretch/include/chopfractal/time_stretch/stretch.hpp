#pragma once
// Offline time stretching (WSOLA) with chop-anchored regions: the audio is stretched region by region so every chop
// boundary lands on its scaled position exactly. Non-real-time, pure functions, no third-party code.
#include <chopfractal/chop_contracts/result.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace chopfractal::stretch {

enum class Quality : std::uint8_t { Draft = 0, Standard = 1, High = 2 };
struct QualityParams {
  double windowMs;
  double toleranceMs;
  int searchDecimation;
};
QualityParams paramsFor(Quality q);  // Draft {20, 5, 8}, Standard {40, 10, 4}, High {60, 15, 1}

constexpr double kMinLengthRatio = 0.5;
constexpr double kMaxLengthRatio = 2.0;

struct Plan {
  std::int64_t inFrames = 0, outFrames = 0;
  std::vector<std::int64_t> inBounds, outBounds;  // strictly increasing; first 0, last inFrames / outFrames
};

// floor((2*p*out + in) / (2*in)), always computed from the original position (no accumulated drift).
std::int64_t mapPosition(std::int64_t pos, std::int64_t inFrames, std::int64_t outFrames);
inline std::int64_t mapLength(std::int64_t len, std::int64_t inFrames, std::int64_t outFrames) { return mapPosition(len, inFrames, outFrames); }

double lengthRatio(double sourceBpm, double targetBpm);  // sourceBpm / targetBpm = outFrames / inFrames
Status checkRatio(double ratio);                         // OutOfRange outside [0.5, 2.0] (1e-9 slack)

// Adds 0 and inFrames, maps every anchor, and drops anchors that collapse onto a neighbour.
Result<Plan> makePlan(std::int64_t inFrames, std::int64_t outFrames, const std::vector<std::int64_t>& anchorsIn);

struct Input {
  const float* const* channels = nullptr;
  int numChannels = 0;  // 1 or 2
  std::int64_t frames = 0;
  std::int32_t sampleRate = 48000;
};
using Progress = std::function<bool(double fraction)>;  // return false to cancel

// Planar output, numChannels * plan.outFrames floats. ErrorCode::Cancelled when `progress` returns false.
Result<std::vector<float>> stretch(const Input& in, const Plan& plan, Quality quality, const Progress& progress = {});

}  // namespace chopfractal::stretch
