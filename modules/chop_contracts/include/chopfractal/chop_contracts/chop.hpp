#pragma once
// Read-only description of a source's chops, exported by source_chop and consumed by pattern modules.
#include <chopfractal/chop_contracts/ids.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace chopfractal {

// Half-open interval [start, end) of source sample frames in the canonical source buffer.
struct SampleRange {
  std::int64_t start = 0;
  std::int64_t end = 0;
  constexpr std::int64_t length() const { return end - start; }
  constexpr bool empty() const { return end <= start; }
  constexpr bool contains(SampleRange r) const { return r.start >= start && r.end <= end; }
  friend constexpr bool operator==(SampleRange a, SampleRange b) { return a.start == b.start && a.end == b.end; }
  friend constexpr bool operator!=(SampleRange a, SampleRange b) { return !(a == b); }
};

// Drum role of a chop as the break-aware engine (engine v2) and the groove layer see it. Unknown means
// "nobody decided"; the composition root fills it from the user's role map and from Smart Setup analysis.
enum class DrumRole : std::uint8_t { Unknown = 0, Kick, Snare, Ghost, Hat, OpenHat, Cymbal, Perc, Other };

struct ChopInfo {
  ChopId id;
  SampleRange range;                  // already includes any user trim
  std::uint32_t group = 0;            // 0 = ungrouped; chops sharing a non-zero group are interchangeable
  std::uint32_t fadeInFrames = 0;     // 0 = renderer default
  std::uint32_t fadeOutFrames = 0;    // 0 = renderer default
  DrumRole role = DrumRole::Unknown;  // engine v2 / groove only; legacy generation ignores it
  float gainTrim = 1.f;               // engine v2 only: invisible per-chop level match, multiplied into generated levels
};

// Only enabled chops appear in a snapshot, ordered by start position.
struct ChopSnapshot {
  SourceId source;
  std::int64_t sourceFrames = 0;
  std::int32_t sampleRate = 0;
  std::vector<ChopInfo> chops;

  const ChopInfo* find(ChopId id) const {
    for (const ChopInfo& c : chops)
      if (c.id == id) return &c;
    return nullptr;
  }
};

using ChopSnapshotPtr = std::shared_ptr<const ChopSnapshot>;

}  // namespace chopfractal
