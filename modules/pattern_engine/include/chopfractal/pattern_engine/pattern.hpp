#pragma once
// Deterministic pattern model, generation, mutation, edit commands, flattening, and versioned state.
// Everything here is a pure function over immutable value types: Pattern in, Pattern out. No audio
// buffers, file paths, host playhead, or UI types. Threading: call from UI/worker threads only; the
// flattened event list is what the audio side consumes.
#include <chopfractal/break_engine/break_engine.hpp>
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_contracts/policy.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace chopfractal::pattern {

// Bump kEngineVersion whenever generation output for the same inputs would change. Saved patterns carry
// the engine version, so an algorithm change never silently rewrites an old pattern.
//   1 = legacy engine (source-order slots, generic metric weights). Unchanged; its golden hashes are pinned.
//   2 = break-aware engine (role-driven, backbeat anchors, velocity hierarchy, phrase and fill model).
// A pattern saved without a v2 block loads as version 1 and keeps using the legacy path for Mutate.
constexpr std::uint32_t kEngineVersionLegacy = 1;
constexpr std::uint32_t kEngineVersionBreak = 2;
constexpr std::uint32_t kEngineVersion = kEngineVersionBreak;  // newest version this build can read
constexpr std::uint32_t kSchemaVersion = 1;

struct Variation {
  double phrase = 0.5;  // chance a bar is new rather than a repeat of an earlier bar
  double bar = 0.5;     // chance each beat of a repeated bar is regenerated
  double beat = 0.5;    // chance a beat is new rather than a repeat of the previous beat
  double event = 0.5;   // chance an event picks an alternate chop / transform / micro-shift / subdivision
};

struct Settings {
  // Which generation engine produces (and later mutates) this pattern. The library default stays 1 so existing
  // callers and pinned hashes are untouched; the host layer (settingsFromParams) defaults new projects to 2.
  std::uint32_t engineVersion = kEngineVersionLegacy;
  breakeng::Options brk;  // used only when engineVersion >= 2
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
  std::uint32_t engineVersion = kEngineVersionLegacy;
  Settings settings;
  ScopeId phraseId;
  bool phraseLocked = false;
  std::vector<Bar> bars;
  std::uint64_t nextId = 1;  // shared counter for scope and event ids; never reused
};

struct Note {
  enum class Kind : std::uint8_t { BlockedByRule, LockedSkipped, LimitReached, NoCandidate, EngineFallback };
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
std::size_t eventCount(const Pattern& p);  // all events including zoomed children

Result<Pattern> generate(const ChopSnapshot& chops, const Settings& settings, const ICandidatePolicy* policy = nullptr,
                         GenerationReport* report = nullptr);
// True when engine v2 can generate for these settings (4/4 on a straight grid of 1/8 or finer). Otherwise
// generate() falls back to the legacy engine and reports Note::Kind::EngineFallback.
bool supportsBreakEngine(const Settings& s);
// Roles the chops can play; what the planner needs to know before it plans.
breakeng::Avail availableRoles(const ChopSnapshot& chops);
// Role, nearest 16th step and anchor status of a top-level event of a v2 pattern (nullopt for legacy patterns,
// unknown ids and zoomed children). Anchors are the downbeat kick and the backbeat snare (including a displaced one).
struct EventRole {
  DrumRole role = DrumRole::Unknown;
  int step = 0;
  bool anchor = false;
};
std::optional<EventRole> describeEvent(const Pattern& p, EventId id, const ChopSnapshot& chops);
// The planner settings a pattern's Settings imply (used by tests, the audition tool and mutate).
breakeng::PlanSettings planSettingsOf(const Settings& s, const ChopSnapshot& chops);

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
// The query a rule policy sees for an existing top-level event (the one generation used when it placed it); chop is invalid when
// the id is not a top-level event.
CandidateQuery candidateQueryFor(const Pattern& p, EventId id);
const Event* findEvent(const Pattern& p, EventId id);
bool isLocked(const Pattern& p, EventId id);  // effective: the event or any ancestor scope is locked

Result<Pattern> setLock(const Pattern& p, const ScopeRef& scope, bool locked);
Pattern clearLocks(const Pattern& p);                // clears every nesting level
bool barHasLock(const Pattern& p, int bar);          // any lock on the phrase, bar, a beat, a hit or a zoomed pattern inside it
Result<Pattern> addEvent(const Pattern& p, int bar, ChopId chop, Ticks startInBar, Ticks duration, const ChopSnapshot& chops);
Result<Pattern> deleteEvent(const Pattern& p, EventId id);
Result<Pattern> moveEvent(const Pattern& p, EventId id, int newBar, Ticks newStartInBar);
// Direct-editing helpers (docs/specs/pass3/_BRIEF.md P4). All refuse locked events/scopes with Blocked.
// resizeEvent keeps the start and sets a new duration (must stay inside the bar); a resized hit loses its child
// tree because the child window always equals the parent's duration.
Result<Pattern> resizeEvent(const Pattern& p, EventId id, Ticks newDuration);
// Copies a hit (without children, unlocked, user-owned, fresh id) to `newStartInBar` of the same bar; the
// copy's duration is clipped to the bar end. Counts against the event cap.
Result<Pattern> duplicateEvent(const Pattern& p, EventId id, Ticks newStartInBar);
Result<Pattern> setEventEnabled(const Pattern& p, EventId id, bool enabled);  // mute / unmute without deleting
Result<Pattern> setEventChop(const Pattern& p, EventId id, ChopId chop, const ChopSnapshot& chops);
Result<Pattern> setEventTransform(const Pattern& p, EventId id, const EventTransform& tx);
Result<Pattern> setEventFx(const Pattern& p, EventId id, const EventFx& fx);
// Cross-bar duplicate (G2/F02): the copy lands at (bar, startInBar), keeps chop, region, transform, fx, gesture blocks,
// probability and enabled flag, has no child, is unlocked and user-owned, and its duration is clipped to the bar end.
// Refused with Blocked when the destination beat or bar is locked, LimitExceeded at the event cap. `newId` receives the copy's id.
Result<Pattern> duplicateEvent(const Pattern& p, EventId id, int bar, Ticks startInBar, EventId* newId = nullptr);
// Chance (0..1) that the hit plays in a given loop (resolved deterministically at flatten time).
Result<Pattern> setEventProbability(const Pattern& p, EventId id, float probability);
// The optional per-hit gesture blocks (Echo, Tremolo-Pan, Stutter, Roll) set together; an empty optional removes the block.
// Hand-set Echo and Tremolo-Pan blocks are written as "user forced" (level / depth plus 128, Tremolo-Pan mode 254 for off) so the
// effect pass leaves them alone; Stutter and Roll are protected by the event being user-owned.
struct GestureFx {
  std::optional<EchoFx> echo;
  std::optional<TremPanFx> tremPan;
  std::optional<StutterFx> stutter;
  std::optional<RollFx> roll;
  friend bool operator==(const GestureFx& a, const GestureFx& b) { return a.echo == b.echo && a.tremPan == b.tremPan && a.stutter == b.stutter && a.roll == b.roll; }
};
Result<Pattern> setEventGestureFx(const Pattern& p, EventId id, const GestureFx& fx);
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

// Applies `fn` to every non-empty Event::region, recursively through zoomed child trees. Empty regions (whole
// chop) stay empty; ids, seeds, locks, flags and structure are untouched, so serialize() of a pattern without
// regions is unchanged. Pure and deterministic. Used to rescale sub-ranges when the source is time-stretched; the
// caller reconciles the result with sanitize() and validate(). Refuses a mapped range with end < start or start < 0.
Result<Pattern> mapRegions(const Pattern& p, const std::function<SampleRange(SampleRange)>& fn);

// ---- state ----
std::vector<std::uint8_t> serialize(const Pattern& p);
Result<Pattern> deserialize(const std::uint8_t* data, std::size_t size);

}  // namespace chopfractal::pattern
