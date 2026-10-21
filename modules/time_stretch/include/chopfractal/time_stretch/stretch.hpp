#pragma once
// Offline time stretching of a loop with chop-anchored regions (WSOLA, time domain, no third-party code).
// Pure functions over immutable inputs: no global state, no I/O, no JUCE. Pitch follows the source; only the
// duration changes. Marker positions are scaled with an exact integer rule (mapPosition) so chop boundaries
// land on their scaled positions with zero frame error and never accumulate drift.
//
// Threading: non-real-time. Call from a worker thread; never from the audio thread. Instances share no state,
// so concurrent calls are safe.
#include <chopfractal/chop_contracts/result.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace chopfractal::stretch {

constexpr std::uint32_t kSchemaVersion = 0;  // stores no state
constexpr double kMinLengthRatio = 0.5;      // outFrames / inFrames, inclusive (1e-9 slack)
constexpr double kMaxLengthRatio = 2.0;
// Largest frame count either side of a plan may have; keeps 2*p*out inside int64.
constexpr std::int64_t kMaxPlanFrames = 2000000000ll;

enum class Quality : std::uint8_t { Draft = 0, Standard = 1, High = 2 };

struct QualityParams {
  double windowMs;
  double toleranceMs;
  int searchDecimation;
};
QualityParams qualityParams(Quality q);  // Draft {20, 5, 8}, Standard {40, 10, 4}, High {60, 15, 1}

// A region [inBounds[i], inBounds[i+1]) of the source becomes [outBounds[i], outBounds[i+1]) of the result.
struct Plan {
  std::int64_t inFrames = 0;
  std::int64_t outFrames = 0;
  std::vector<std::int64_t> inBounds;   // strictly increasing, first 0, last inFrames
  std::vector<std::int64_t> outBounds;  // strictly increasing, first 0, last outFrames
};

// Marker scaling rule: floor((2*p*out + in) / (2*in)), always computed from the ORIGINAL position, so there is
// no accumulated drift. Maps 0 -> 0 and in -> out, is monotonic non-decreasing and stays within 0.5 frame of the
// exact ratio. `pos` is clamped to [0, inFrames]. Requires 0 < in,out <= kMaxPlanFrames (otherwise returns 0).
std::int64_t mapPosition(std::int64_t pos, std::int64_t inFrames, std::int64_t outFrames);
// Same arithmetic for lengths (trims, fades): a length of 0 stays 0.
std::int64_t mapLength(std::int64_t length, std::int64_t inFrames, std::int64_t outFrames);

double lengthRatio(double sourceBpm, double targetBpm);  // = sourceBpm / targetBpm
Status checkRatio(double ratio);                          // OutOfRange outside [kMinLengthRatio, kMaxLengthRatio]

// Region plan from source anchor positions. 0 and inFrames are added, anchors outside (0, inFrames) are ignored,
// the list is sorted and de-duplicated, and each anchor is placed at mapPosition(anchor). An anchor that would
// land on the same output frame as its predecessor is dropped.
Result<Plan> makePlan(std::int64_t inFrames, std::int64_t outFrames, const std::vector<std::int64_t>& anchorsIn);

struct Input {
  const float* const* channels = nullptr;  // planar, `numChannels` pointers to `frames` samples each
  int numChannels = 1;                      // 1 or 2
  std::int64_t frames = 0;
  std::int32_t sampleRate = 48000;
};

// Return false to cancel. Called at the start, at least every 50 ms of work, and at the end.
using Progress = std::function<bool(double fraction)>;

// Planar result: numChannels * plan.outFrames floats (all of channel 0, then channel 1). Returns Cancelled when the
// progress callback asked to stop, LimitExceeded ("Not enough memory to stretch this source.") on allocation failure.
// Non-finite input samples are read as 0.
Result<std::vector<float>> stretch(const Input& in, const Plan& plan, Quality q, const Progress& progress = {});

}  // namespace chopfractal::stretch
