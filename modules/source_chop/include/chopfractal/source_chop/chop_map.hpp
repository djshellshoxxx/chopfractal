#pragma once
// Source metadata, chop markers, and marker editing commands. No audio decoding, file dialogs,
// waveform drawing, or host capture lives here: adapters hand in decoded sample views.
// Threading: UI/worker thread only. The exported ChopSnapshot is immutable and safe to share.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/undo.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::source {

constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::int64_t kDefaultMinRegionFrames = 64;
constexpr std::int64_t kMaxSourceFrames = 60ll * 192000ll;  // 60 s at 192 kHz (implementation constant)

struct SourceInfo {
  SourceId id;  // content hash, see computeSourceId
  std::string name;
  std::int32_t channels = 1;
  std::int32_t sampleRate = 44100;
  std::int64_t frames = 0;
  std::string path;  // optional external reference
};

// Marker positions are integer frames in the canonical source buffer (its own sample rate), so
// they never drift when the host sample rate changes.
struct Marker {
  ChopId id;
  std::int64_t position = 0;
  bool enabled = true;
  bool manual = true;  // false = created by analysis, still editable
  std::string label;
  std::uint32_t color = 0;
  std::uint32_t group = 0;
  std::uint32_t trimStart = 0;  // frames removed from the region start
  std::uint32_t trimEnd = 0;    // frames removed from the region end
  std::uint32_t fadeIn = 0;
  std::uint32_t fadeOut = 0;
};

struct Region {
  ChopId id;
  SampleRange range;  // untrimmed
};

struct ProposedMarker {
  std::int64_t position = 0;
  float strength = 1.f;  // candidate strength so the UI can show weak vs strong detections
};
struct Proposal {
  std::vector<ProposedMarker> markers;
};

enum class MergeMode { Replace, Merge };

class ChopMap {
 public:
  static Result<ChopMap> create(SourceInfo info, std::int64_t minRegionFrames = kDefaultMinRegionFrames);

  const SourceInfo& info() const { return info_; }
  std::int64_t minRegionFrames() const { return minRegion_; }
  const std::vector<Marker>& markers() const { return history_.current().markers; }
  const Marker* find(ChopId id) const;

  // Regions between adjacent enabled markers, the last one running to the clip end.
  std::vector<Region> regions() const;
  // Enabled chops with trims applied, ordered by position. Immutable.
  ChopSnapshotPtr snapshot() const;

  // Every edit is validated and is one undo step. Failed edits change nothing.
  Result<ChopId> addMarker(std::int64_t position, bool manual = true);
  Status moveMarker(ChopId id, std::int64_t position);  // may not cross a neighbour
  Status deleteMarker(ChopId id);
  Status setEnabled(ChopId id, bool enabled);
  Status setLabel(ChopId id, std::string label);
  Status setColor(ChopId id, std::uint32_t color);
  Status setGroup(ChopId id, std::uint32_t group);
  Status setTrim(ChopId id, std::uint32_t trimStart, std::uint32_t trimEnd);
  Status setFades(ChopId id, std::uint32_t fadeIn, std::uint32_t fadeOut);
  Status clearMarkers();

  // Re-running detection yields a proposal; it never overwrites silently. preview() shows the result
  // of Replace/Merge without applying it; applyProposal() commits it as one undo step.
  Result<std::vector<Marker>> preview(const Proposal& proposal, MergeMode mode) const;
  Status applyProposal(const Proposal& proposal, MergeMode mode);

  bool canUndo() const { return history_.canUndo(); }
  bool canRedo() const { return history_.canRedo(); }
  bool undo() { return history_.undo(); }
  bool redo() { return history_.redo(); }

  // Module-owned versioned state (schema kSchemaVersion).
  std::vector<std::uint8_t> serialize() const;
  static Result<ChopMap> deserialize(const std::uint8_t* data, std::size_t size);

 private:
  struct State {
    std::vector<Marker> markers;  // sorted by position
    std::uint64_t nextId = 1;
  };
  ChopMap(SourceInfo info, std::int64_t minRegion) : info_(std::move(info)), minRegion_(minRegion), history_(State{}) {}

  Status validate(const State& s) const;
  Status commit(State next);
  Result<State> computeProposal(const Proposal& proposal, MergeMode mode) const;
  Marker* findIn(State& s, ChopId id) const;

  SourceInfo info_;
  std::int64_t minRegion_;
  SnapshotStack<State> history_;
};

}  // namespace chopfractal::source
