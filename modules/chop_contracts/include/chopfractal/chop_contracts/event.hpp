#pragma once
// Nested event model shared by pattern_engine and recursive_hit_zoom, plus the single canonical
// flatten operation that turns a nested tree into the bounded flat list the audio renderer plays.
#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/time.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace chopfractal {

struct EventTransform {
  float level = 1.f;            // linear gain, [0, kMaxLevel]
  float pan = 0.f;              // [-1, 1]
  float pitchSemitones = 0.f;   // resampling pitch, +/- kMaxPitchSemitones
  bool reverse = false;
  std::uint8_t retrigger = 1;   // 1..kMaxRetrigger sub-hits across the event
  friend bool operator==(const EventTransform& a, const EventTransform& b) {
    return a.level == b.level && a.pan == b.pan && a.pitchSemitones == b.pitchSemitones &&
           a.reverse == b.reverse && a.retrigger == b.retrigger;
  }
};

// Per-event effects (see docs/specs/effects-filter-glide-crunch.md). All-default means "no effect" and is
// serialized as absent, so effect-free events keep their exact pre-effect encoding.
enum class FilterType : std::uint8_t { Off = 0, LowPass = 1, HighPass = 2 };
struct EventFx {
  FilterType filter = FilterType::Off;
  float cutoff = 1.f;          // [0, 1], logarithmic 20 Hz .. 20 kHz
  float resonance = 0.f;       // [0, 1]
  float glideSemitones = 0.f;  // +/- kMaxGlideSemitones, exponential rate ramp across the event
  float crush = 0.f;           // [0, 1] bit and sample-rate reduction
  bool active() const { return filter != FilterType::Off || glideSemitones != 0.f || crush != 0.f; }
  friend bool operator==(const EventFx& a, const EventFx& b) {
    return a.filter == b.filter && a.cutoff == b.cutoff && a.resonance == b.resonance &&
           a.glideSemitones == b.glideSemitones && a.crush == b.crush;
  }
};

struct NestedPattern;

// One scheduled playback of a chop. `start` is relative to the enclosing window: the bar for
// top-level events, the parent event's window for events inside a NestedPattern.
struct Event {
  EventId id;
  ChopId chop;
  SampleRange region;          // sub-range of the source to read; empty() means the whole chop
  Ticks start = 0;
  Ticks duration = 0;
  EventTransform tx;
  EventFx fx;
  float probability = 1.f;     // resolved deterministically at flatten time
  bool enabled = true;
  bool locked = false;
  bool userOwned = false;      // set by manual edits; mutation skips unless asked to include edited
  bool sourceOverride = false; // child explicitly replaces the source chop (may leave ancestor bounds)
  bool childActive = true;     // false = parent fallback plays; the child tree is kept
  std::shared_ptr<const NestedPattern> child;
};

struct NestedPattern {
  std::uint64_t seed = 0;
  Ticks windowDuration = 0;    // always equals the parent event's duration
  std::uint8_t depth = 1;      // 1 = child of a top-level event
  bool locked = false;
  std::vector<Event> events;
};

// Output of flatten: absolute positions within the pattern, resolved source ranges.
struct FlatEvent {
  EventId id;
  ChopId chop;
  SampleRange region;
  Ticks start = 0;
  Ticks duration = 0;
  EventTransform tx;
  EventFx fx;
  std::uint32_t fadeInFrames = 0;
  std::uint32_t fadeOutFrames = 0;
  std::uint8_t depth = 0;
};
using FlatEventList = std::vector<FlatEvent>;

struct FlattenLimits {
  std::size_t maxEvents = limits::kMaxEventsPerPattern;
  int maxDepth = limits::kMaxNestedDepth;
};

// Appends events of one window to `out`. `ancestor` is the resolved source range that bounds the
// events (empty for top level, where each event is bounded by its own chop). Fails instead of
// truncating: exceeding limits returns LimitExceeded, malformed input returns OutOfRange/Corrupt.
Status flattenInto(FlatEventList& out, const std::vector<Event>& events, Ticks windowStart, Ticks windowDuration,
                   SampleRange ancestor, const ChopSnapshot& chops, const FlattenLimits& limits,
                   std::uint64_t probabilitySeed, int depth = 0);

void sortFlat(FlatEventList& list);  // by (start, id)

// Range/shape validation of one event and its subtree (no source lookups).
Status validateEvent(const Event& e, int depth = 0);

// Binary codec for one event subtree (recursive, depth and count bounded).
void writeEvent(bytes::Writer& w, const Event& e);
bool readEvent(bytes::Reader& r, Event& out, int depth = 0);

}  // namespace chopfractal
