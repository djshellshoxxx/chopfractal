#pragma once
// Deterministic pattern model, generation, mutation, edit commands, flattening, and versioned state.
// Everything here is a pure function over immutable value types: Pattern in, Pattern out. No audio
// buffers, file paths, host playhead, or UI types. Threading: call from UI/worker threads only; the
// flattened event list is what the audio side consumes.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_contracts/policy.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace chopfractal::pattern {

// Bump kEngineVersion whenever generation output for the same inputs would change. Saved patterns carry
// the engine version, so an algorithm change never silently rewrites an old pattern.
constexpr std::uint32_t kEngineVersion = 1;
constexpr std::uint32_t kSchemaVersion = 1;

struct Variation {
  double phrase = 0.5;  // chance a bar is new rather than a repeat of an earlier bar
  double bar = 0.5;     // chance each beat of a repeated bar is regenerated
  double beat = 0.5;    // chance a beat is new rather than a repeat of the previous beat
  double event = 0.5;   // chance an event picks an alternate chop / transform / micro-shift / subdivision
};

struct Settings {
  int bars = 4;  // 1, 2, 4 or 8
  TimeSignature timeSignature;
  Grid grid{16, false};
  double density = 0.5;  // target fraction of grid slots that carry an event
  Variation variation;
  double swing = 0.0;  // 0..1, applied only to odd slots of straight grids of 1/8 or finer
  std::uint64_t seed = 1;
  int maxEvents = static_cast<int>(limits::kMaxEventsPerPattern);
  bool allowReverse = false;
  bool allowPitch = false;
  bool allowRetrigger = false;
  int pitchRange = 7;        // semitones, 0..24
  int maxRetrigger = 4;      // 2..8
  Ticks maxShiftTicks = 0;   // micro-shift bound, at most grid/8
  bool allowFilter = false;  // per-event effects (docs/specs/effects-filter-glide-crunch.md)
  bool allowGlide = false;
  bool allowCrunch = false;
  double fxIntensity = 0.5;  // 0..1 magnitude scale for generated effects
};

struct Beat {
  ScopeId id;
  bool locked = false;
  bool userOwned = false;
  std::vector<Event> events;
};

struct Bar {
  ScopeId id;
  bool locked = false;
  bool userOwned = false;
  int copyOf = -1;  // index of the bar this one repeats, or -1
  std::vector<Beat> beats;
};

struct Pattern {
  std::uint32_t engineVersion = kEngineVersion;
  Settings settings;
  ScopeId phraseId;
  bool phraseLocked = false;
  std::vector<Bar> bars;
  std::uint64_t nextId = 1;  // shared counter for scope and event ids; never reused
};

struct Note {
  enum class Kind : std::uint8_t { BlockedByRule, LockedSkipped, LimitReached, NoCandidate };
  Kind kind = Kind::NoCandidate;
  std::uint32_t rule = 0;
  int bar = 0;
  int beat = 0;
};
struct GenerationReport {
  std::vector<Note> notes;
};

struct MutateOptions {
  std::uint64_t seed = 2;  // advancing the seed is an explicit user action
  double amount = 0.5;     // probability that each unlocked beat is regenerated
  bool includeEdited = false;
};

Status validateSettings(const Settings& s);
Ticks barTicks(const Settings& s);
Ticks lengthTicks(const Pattern& p);
std::size_t eventCount(const Pattern& p);  // top-level events (children excluded)

Result<Pattern> generate(const ChopSnapshot& chops, const Settings& settings, const ICandidatePolicy* policy = nullptr,
                         GenerationReport* report = nullptr);
Result<Pattern> mutate(const Pattern& pattern, const ChopSnapshot& chops, const MutateOptions& options,
                       const ICandidatePolicy* policy = nullptr, GenerationReport* report = nullptr);

// Bounded, sorted, absolute-time events for the audio renderer. Never truncates: errors instead.
Result<FlatEventList> flatten(const Pattern& pattern, const ChopSnapshot& chops);
// Structural validation (ids, ranges, windows, caps). Used on load and after every edit.
Status validate(const Pattern& pattern);

// ---- edits (each returns a new Pattern; structural edits are refused inside locked scopes) ----
struct ScopeRef {
  ScopeLevel level = ScopeLevel::Phrase;
  int bar = 0;
  int beat = 0;
  EventId event;
};
struct Location {
  int bar = 0;
  int beat = 0;
  std::size_t index = 0;
};

std::optional<Location> locate(const Pattern& p, EventId id);
const Event* findEvent(const Pattern& p, EventId id);
bool isLocked(const Pattern& p, EventId id);  // effective: the event or any ancestor scope is locked

Result<Pattern> setLock(const Pattern& p, const ScopeRef& scope, bool locked);
Pattern clearLocks(const Pattern& p);
Result<Pattern> addEvent(const Pattern& p, int bar, ChopId chop, Ticks startInBar, Ticks duration, const ChopSnapshot& chops);
Result<Pattern> deleteEvent(const Pattern& p, EventId id);
Result<Pattern> moveEvent(const Pattern& p, EventId id, int newBar, Ticks newStartInBar);
Result<Pattern> setEventChop(const Pattern& p, EventId id, ChopId chop, const ChopSnapshot& chops);
Result<Pattern> setEventTransform(const Pattern& p, EventId id, const EventTransform& tx);
Result<Pattern> setEventFx(const Pattern& p, EventId id, const EventFx& fx);
// Replaces every event of one bar (e.g. a generated Fractal Rhythm bar). Refused in locked scopes; the
// bar becomes user-owned. Events must already be valid, with unique ids not used elsewhere.
Result<Pattern> setBarEvents(const Pattern& p, int bar, std::vector<Event> events, const ChopSnapshot& chops);
// Allocates `count` fresh ids from the pattern's counter (for externally built events).
std::pair<Pattern, EventId> reserveIds(const Pattern& p, std::uint64_t count);
Result<Pattern> duplicateBar(const Pattern& p, int fromBar, int toBar);
Result<Pattern> restoreSourceOrder(const Pattern& p, const ChopSnapshot& chops);  // also clears all locks
Result<Pattern> setChild(const Pattern& p, EventId id, std::shared_ptr<const NestedPattern> child, const ChopSnapshot& chops);
Result<Pattern> collapse(const Pattern& p, EventId id);  // drops the child tree; the parent event plays alone
Result<Pattern> setChildActive(const Pattern& p, EventId id, bool active);  // keep the tree, toggle parent/child playback
// Reconcile after marker edits: drops events whose chop vanished, clears sub-regions that left their chop.
Pattern sanitize(const Pattern& p, const ChopSnapshot& chops);

// ---- state ----
std::vector<std::uint8_t> serialize(const Pattern& p);
Result<Pattern> deserialize(const std::uint8_t* data, std::size_t size);

}  // namespace chopfractal::pattern
