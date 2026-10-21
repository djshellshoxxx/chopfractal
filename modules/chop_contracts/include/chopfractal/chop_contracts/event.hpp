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
#include <optional>
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

// ---- gesture effects (docs/specs/roadmap/E01, E02, E03, E04) ----
// Each is an optional per-event block. The flags byte of an event is full (bits 1..32 as before, 64 Echo,
// 128 Tremolo-Pan), so the Stutter and Roll blocks are signalled in the extension byte that follows it (the old
// "has child" byte: bit 0 child, bit 1 Stutter, bit 2 Roll; bits 3..7 are reserved and rejected on read).
// An event without any of these serializes exactly as before.
constexpr std::uint8_t kEventHasEcho = 64;
constexpr std::uint8_t kEventHasTremPan = 128;
constexpr std::uint8_t kExtHasChild = 1;
constexpr std::uint8_t kExtHasStutter = 2;
constexpr std::uint8_t kExtHasRoll = 4;
constexpr std::uint8_t kExtKnownBits = kExtHasChild | kExtHasStutter | kExtHasRoll;

// E01 Echo. repeats 0..5 (0 with the block present = user forced off). rateIndex 0..3 (1/16, 1/8, dotted 1/8, 1/4)
// or 255 = global. level: 0..100 per-event decay in percent, 255 = global; the value plus 128 marks a hit the user
// forced (the effect pass leaves it alone).
struct EchoFx {
  std::uint8_t repeats = 0;
  std::uint8_t rateIndex = 255;
  std::uint8_t level = 255;
  friend bool operator==(const EchoFx& a, const EchoFx& b) { return a.repeats == b.repeats && a.rateIndex == b.rateIndex && a.level == b.level; }
};
// E02 Tremolo-Pan. mode 0 Tremolo, 1 Pan, 2 Both, 255 global, 254 user forced off. rateIndex 0..3 (1/16, 1/8, 1/4, 1/2 per
// LFO cycle) or 255 global. depth 0..100, 255 global, 128 + depth = user forced.
struct TremPanFx {
  std::uint8_t mode = 255;
  std::uint8_t rateIndex = 255;
  std::uint8_t depth = 255;
  friend bool operator==(const TremPanFx& a, const TremPanFx& b) { return a.mode == b.mode && a.rateIndex == b.rateIndex && a.depth == b.depth; }
};
// E03 Stutter (buffer repeat of the start of the hit). rate / rateEnd index kStutterDivisions (coarse to fine); equal
// means a constant rate, otherwise the rate walks one division at a time from rate to rateEnd across the hit and ends
// on the grid. gate 10..100 % of each repeat is audible, tail 0..100 % of that audible part is the release (low = choppy,
// high = washy). decay is the per-repeat gain slope in 0.1 dB (negative decays, positive is a crescendo), pitch the total
// pitch ramp over the hit in 0.1 semitone, probability the chance each repeat after the first plays, pan the width of the
// left/right alternation of the repeats.
struct StutterFx {
  std::uint8_t rate = 4;
  std::uint8_t rateEnd = 4;
  std::uint8_t gate = 80;
  std::uint8_t tail = 30;
  std::int8_t decay = -15;
  std::int8_t pitch = 0;
  std::uint8_t probability = 100;
  std::uint8_t pan = 0;
  friend bool operator==(const StutterFx& a, const StutterFx& b) {
    return a.rate == b.rate && a.rateEnd == b.rateEnd && a.gate == b.gate && a.tail == b.tail && a.decay == b.decay &&
           a.pitch == b.pitch && a.probability == b.probability && a.pan == b.pan;
  }
};
// E04 Roll (beat-repeat style freeze and roll with a speed ramp). The hit is frozen at freeze (0..100 % into the chop)
// and that grab is repeated, speeding up from rate to rateEnd (flags bit 1: smooth geometric acceleration, else stepped
// through the divisions), with the repeat level rising from velStart to velEnd (percent of the hit's level). flags bit 0:
// palindrome (every other repeat plays backwards). pitch: total ramp in 0.1 semitone.
struct RollFx {
  std::uint8_t rate = 4;
  std::uint8_t rateEnd = 8;
  std::uint8_t velStart = 50;
  std::uint8_t velEnd = 100;
  std::uint8_t freeze = 0;
  std::uint8_t flags = 0;
  std::int8_t pitch = 0;
  friend bool operator==(const RollFx& a, const RollFx& b) {
    return a.rate == b.rate && a.rateEnd == b.rateEnd && a.velStart == b.velStart && a.velEnd == b.velEnd && a.freeze == b.freeze &&
           a.flags == b.flags && a.pitch == b.pitch;
  }
};
constexpr std::uint8_t kRollPalindrome = 1;
constexpr std::uint8_t kRollSmooth = 2;

// Note values in ticks (960 per quarter), coarse to fine: 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, 1/64T.
constexpr int kStutterDivisions = 10;
constexpr Ticks kStutterDivisionTicks[kStutterDivisions] = {960, 640, 480, 320, 240, 160, 120, 80, 60, 40};
constexpr Ticks kEchoSpacingTicks[4] = {240, 480, 720, 960};  // 1/16, 1/8, dotted 1/8, 1/4
constexpr Ticks kTremPeriodTicks[4] = {240, 480, 960, 1920};  // 1/16, 1/8, 1/4, 1/2
constexpr int kEchoMaxRepeats = 5;

// Clamps every field of a block into its legal range (readers use these instead of rejecting a file).
EchoFx sanitize(EchoFx fx);
TremPanFx sanitize(TremPanFx fx);
StutterFx sanitize(StutterFx fx);
RollFx sanitize(RollFx fx);

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
  std::optional<EchoFx> echo;
  std::optional<TremPanFx> tremPan;
  std::optional<StutterFx> stutter;
  std::optional<RollFx> roll;
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
  std::optional<EchoFx> echo;
  std::optional<TremPanFx> tremPan;
  std::optional<StutterFx> stutter;
  std::optional<RollFx> roll;
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
