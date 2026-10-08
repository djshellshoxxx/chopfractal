#pragma once
// Pure analysis helpers: transient detection, even grids, snapping, and source identity. Run these on a
// worker thread, never in the audio callback.
#include <chopfractal/source_chop/chop_map.hpp>

#include <cstdint>
#include <vector>

namespace chopfractal::source {

struct AnalysisParams {
  double sensitivity = 0.5;               // 0 = only the strongest onsets, 1 = very sensitive
  std::int64_t minSpacingFrames = 0;      // 0 = 20 ms at the source rate
  std::int64_t rangeStart = 0;            // analysis range [rangeStart, rangeEnd); rangeEnd < 0 = clip end
  std::int64_t rangeEnd = -1;
};

// Onsets from a combination of amplitude change and local energy. Includes a candidate at the range
// start so the first region begins at the loop start. Does not classify sounds.
Proposal detectTransients(const float* const* channels, int numChannels, std::int64_t frames, std::int32_t sampleRate,
                          const AnalysisParams& params = {});

enum class GridKind { Straight, Triplet, Dotted };
struct GridSpec {
  double loopBeats = 4.0;  // length of the clip in beats (supplied by the user; never inferred silently)
  int division = 8;        // note value 1/division of a whole note
  GridKind kind = GridKind::Straight;
};
// Positions are computed as round(k * cell) from the exact value, so they never accumulate drift.
Proposal evenGrid(std::int64_t frames, const GridSpec& spec);

// Snap helpers; both return `position` unchanged when nothing lies within `radius`.
std::int64_t snapToNearest(std::int64_t position, const std::vector<std::int64_t>& targets, std::int64_t radius);
std::int64_t snapToZeroCrossing(const float* channel, std::int64_t frames, std::int64_t position, std::int64_t radius);

// Content hash identifying a source (FNV-1a over sample bit patterns, rate, channel count). Never 0.
SourceId computeSourceId(const float* const* channels, int numChannels, std::int64_t frames, std::int32_t sampleRate);

}  // namespace chopfractal::source
