#pragma once
// The composition root: the only place that wires the portable modules together.
//
//  * converts commands into calls on source_chop / pattern_engine / recursive_hit_zoom / chop_roles_grammar
//  * injects the role grammar into generation as an optional candidate policy
//  * stores every Generate/Mutate/Zoom result as a variation_history node
//  * validates a complete immutable playback snapshot BEFORE handing it to the audio side
//  * composes and restores whole-project state under per-module IDs and schema versions
//  * turns module errors into user-facing Notices (core modules return errors, they never present UI)
//
// Threading: a ProjectSession is owned by one non-real-time thread (UI or worker). The only thing that
// crosses to the audio thread is the immutable render::Playback published through the Mailbox.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_roles_grammar/grammar.hpp>
#include <chopfractal/level_match/level_match.hpp>
#include <chopfractal/pattern_engine/fx_pass.hpp>
#include <chopfractal/pattern_engine/morph.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>
#include <chopfractal/pattern_engine/session.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <chopfractal/recursive_hit_zoom/zoom.hpp>
#include <chopfractal/source_chop/analysis.hpp>
#include <chopfractal/source_chop/chop_map.hpp>
#include <chopfractal/state_codec/codec.hpp>
#include <chopfractal/variation_history/history.hpp>

#include <cstdint>
#include <chopfractal/composition/stretch.hpp>
#include <chopfractal/chop_insight/insight.hpp>
#include <chopfractal/evolve/evolve.hpp>
#include <chopfractal/scenes/scenes.hpp>
#include <chopfractal/composition/arrangement.hpp>
#include <chopfractal/composition/arrangement_job.hpp>
#include <chopfractal/fractal_rhythm/fractal.hpp>
#include <chopfractal/groove/groove.hpp>
#include <chopfractal/wav_export/wav.hpp>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chopfractal::composition {

// Project session schema (module "composition"). LAZY VERSIONING: saveState writes schema 2 only when something
// new is persisted (non-default ProjectExtras, an occupied A/B slot, or a feature section); otherwise it writes
// schema 1 and the exact bytes of the pre-v2 build, so a project that uses none of it still opens in older builds.
// A v1 payload is a valid v2 payload with no extras, which is why the registered migration is the identity.
constexpr std::uint32_t kSessionSchemaV1 = 1;
constexpr std::uint32_t kSessionSchemaVersion = 2;
constexpr std::uint32_t kSessionAudioSchema = 1;  // module "source_audio" (embedded audio)
// Default cap on audio embedded in project state: 128 MiB (about 12 minutes of 44.1 kHz stereo float).
// Decided generous; a session can lower or raise it with setEmbeddedSourceCap(). The codec ceiling
// (kCodecCeilingBytes) bounds what can ever be saved or loaded.
constexpr std::size_t kMaxEmbeddedSourceBytes = 128u * 1024u * 1024u;
constexpr std::size_t kCodecCeilingBytes = 256u * 1024u * 1024u;

struct Notice {
  enum class Level : std::uint8_t { Info, Warning, Error };
  Level level = Level::Info;
  std::string text;
};

// Rule editor support (rules.cpp, G2 / F06).
struct RuleNote {  // one change a rule blocked during the last Generate, Mutate or Evolve step
  int bar = 0;
  int beat = 0;
  std::uint32_t rule = 0;  // 0 = a rule (unknown)
  std::string text;
};
struct EventExplanation {
  bool allowed = true;
  bool preserved = false;   // a Preserve rule protects the hit from Mutate
  bool handPlaced = false;  // the user placed or edited it: Mutate keeps it whatever the rules say
  double weight = 1.0;
  std::vector<std::uint32_t> rules;  // rules that took part, ascending
  std::string text;
};
struct RulePreview {
  int events = 0;
  int blocked = 0;
  std::vector<std::pair<std::uint32_t, int>> blockedByRule;  // sorted by rule id; id 0 is "a rule"
};

enum class DetectMode { Transients, EvenGrid };
struct DetectOptions {
  DetectMode mode = DetectMode::Transients;
  source::AnalysisParams analysis;
  source::GridSpec grid;
  source::MergeMode merge = source::MergeMode::Replace;
};

enum class Quantize { Immediate, LoopBoundary };

// ---- Smart Setup, export (docs/specs/smart-setup.md, export-wav-and-producer-kit.md) ----
struct SetupSuggestion {
  std::vector<insight::RoleSuggestion> roles;
  std::vector<insight::LoopSuggestion> loops;
};
struct ExportWavOptions {
  wav::Options wav;             // format, sample rate, dither, normalization
  double bpm = 120.0;
  int loops = 1;                // 1..16
  double tailSeconds = 0.5;     // 0..30
  bool overwrite = false;       // only after the user confirmed
};
struct ExportReport {
  std::uint64_t frames = 0;
  std::uint64_t notes = 0;  // kit: MIDI notes written
  float peak = 0.f;
  std::uint64_t clipped = 0;
  std::vector<std::string> files;
};
// MIDI velocity from the event level `l` (F13): Linear clamp(l*127), Soft 127*min(l,1)^2, Hard 127*sqrt(min(l,1)), Fixed.
enum class VelocityCurve : std::uint8_t { Linear = 0, Soft = 1, Hard = 2, Fixed = 3 };
struct MidiExportOptions {
  int baseNote = 36;                      // 0..127; chop n (snapshot order) -> baseNote + n
  double bpm = 120.0;                     // 20..999, written as the tempo event
  int loops = 1;                          // 1..16 repeats of the pattern
  VelocityCurve curve = VelocityCurve::Linear;
  int fixedVelocity = 100;                // 1..127, used by Fixed
  int channel = 0;                        // 0..15 (the UI shows 1..16)
  std::string trackName = "chopfractal";  // at most 60 bytes
};
struct KitOptions {
  int baseNote = 36;            // chop n -> note baseNote + n
  double bpm = 120.0;
  int loops = 1;                // 1..16 repeats of the pattern in pattern.mid
  bool overwrite = false;
  std::string name = "chopfractal";
  VelocityCurve curve = VelocityCurve::Linear;  // defaults reproduce the pre-F13 pattern.mid byte for byte
  int fixedVelocity = 100;
  int channel = 0;
};
// MIDI velocity 1..127 for an event level under a curve (`fixedVelocity` is used by Fixed only).
int midiVelocity(float level, VelocityCurve curve, int fixedVelocity = 100);
constexpr std::size_t kMaxDragFiles = 64;           // 63 slices + the MIDI file
constexpr double kMaxDragAudioSeconds = 120.0;      // a dragged WAV longer than this is refused by the UI
struct KitFile {
  std::string name;
  std::vector<std::uint8_t> bytes;
};
// The MIDI export preferences that are saved with the project (they ride in the Export block of the extras section).
struct MidiExportSettings {
  int baseNote = 36;  // 0..127
  VelocityCurve curve = VelocityCurve::Linear;
  int fixedVelocity = 100;  // 1..127
  int loops = 1;            // 1..16
  int channel = 0;          // 0..15
  friend bool operator==(const MidiExportSettings& a, const MidiExportSettings& b) {
    return a.baseNote == b.baseNote && a.curve == b.curve && a.fixedVelocity == b.fixedVelocity && a.loops == b.loops && a.channel == b.channel;
  }
};

// ---- project extras (state v2, docs/specs/roadmap/F01-state-v2-and-persistence.md section 4) ----
// UI and generation choices that used to be lost on save. They live outside the history (no undo step).
struct ExportUiSettings {
  int formatIndex = 1;  // 16-bit, 24-bit, 32-bit float
  int rateIndex = 1;    // 44.1, 48, 88.2, 96 kHz
  int loopsIndex = 1;   // {1, 2, 4, 8, 16}
  MidiExportSettings midi;  // F13; written after the three indexes only when it differs from the default
  friend bool operator==(const ExportUiSettings& a, const ExportUiSettings& b) {
    return a.formatIndex == b.formatIndex && a.rateIndex == b.rateIndex && a.loopsIndex == b.loopsIndex && a.midi == b.midi;
  }
};
struct UiPrefs {
  int viewMode = 1;  // 0 Simple, 1 Advanced
  int scalePercent = 100;
  int width = 940, height = 912;
  bool editHits = false;  // the "Move / resize hits" toggle
  int markerSnap = 1;     // 0 Off, 1 Transient, 2 ZeroCrossing
  bool gridSnap = true;
  int snapDivision = 0;   // 0 = the pattern grid, else {1, 2, 4, 8, 16, 32}
  bool favoritesOnly = false;
  std::string search;     // at most 32 bytes
  friend bool operator==(const UiPrefs& a, const UiPrefs& b) {
    return a.viewMode == b.viewMode && a.scalePercent == b.scalePercent && a.width == b.width && a.height == b.height && a.editHits == b.editHits &&
           a.markerSnap == b.markerSnap && a.gridSnap == b.gridSnap && a.snapDivision == b.snapDivision && a.favoritesOnly == b.favoritesOnly && a.search == b.search;
  }
};
struct ProjectExtras {
  std::uint64_t seed = 1;  // 0..999999999999
  std::string motif = "x.xx";  // 1..8 chars of x a .
  int fractalDepth = 2;    // 1..3
  int roleIndex = 0;       // into roles::builtinRoles()
  int detectMode = 0;      // 0..3 (= the editor's DetectChoice - 1)
  int gridDivision = 16;   // generation grid {1, 2, 4, 8, 16, 32}
  bool gridTriplet = false;
  double fractalMutation = -1.0;     // -1: derive from Variation, else 0..1
  double fractalAccentDecay = 0.85;  // (0, 1]
  ExportUiSettings exportUi;
  UiPrefs ui;
  friend bool operator==(const ProjectExtras& a, const ProjectExtras& b) {
    return a.seed == b.seed && a.motif == b.motif && a.fractalDepth == b.fractalDepth && a.roleIndex == b.roleIndex && a.detectMode == b.detectMode &&
           a.gridDivision == b.gridDivision && a.gridTriplet == b.gridTriplet && a.fractalMutation == b.fractalMutation &&
           a.fractalAccentDecay == b.fractalAccentDecay && a.exportUi == b.exportUi && a.ui == b.ui;
  }
  friend bool operator!=(const ProjectExtras& a, const ProjectExtras& b) { return !(a == b); }
};
// OutOfRange / InvalidArgument per field; the message names the field.
Status validate(const ProjectExtras&);

// ---- feature sections (the "StateSection" API; tag registry in the F01 spec section 4.4) ----
// A feature that needs project state owns one tag in 9..31 and a codec for `bytes`; the session stores, saves and
// restores them opaquely. Unknown tags of 9 and above are kept and written back unchanged.
struct StateSection {
  std::uint8_t tag = 0;
  std::uint16_t schema = 1;
  std::vector<std::uint8_t> bytes;  // excludes the schema
};
// Runs after a project is loaded, for a registered tag that is present. A failure drops only that section and queues
// the warning "Some <feature> settings could not be restored.".
using SectionDecoder = std::function<Status(const StateSection&)>;
constexpr std::uint8_t kFirstFeatureTag = 9, kLastFeatureTag = 31;
constexpr std::uint8_t kTagRouting = 9;       // owned by F11 Multi-output stems (composition/src/routing.cpp)
constexpr std::uint8_t kTagPads = 10;         // owned by F07 Performance pads (composition/src/pads.cpp)
constexpr std::uint8_t kTagScenes = 11;       // owned by F09 Evolve scenes (composition/src/scenes.cpp)
constexpr std::uint8_t kTagArrangement = 16;  // owned by F18 Arrangement export (composition/src/arrangement_session.cpp)
constexpr std::uint8_t kTagGroove = 14;       // owned by F17 Groove Lab (composition/src/groove.cpp)
constexpr std::uint8_t kTagFractalTune = 13;  // owned by F01 itself (written from ProjectExtras), refused by setSection
std::size_t sectionByteCap(std::uint8_t tag);  // the per-tag cap of the registry
constexpr std::size_t kMaxExtrasBlocks = 32;
constexpr std::size_t kMaxExtrasBytes = 40u * 1024u * 1024u;

// ---- multiple outputs (F11; composition/src/routing.cpp) ----
enum class RoutingMode : std::uint8_t { MainOnly = 0, ByRole = 1, ByChop = 2, Custom = 3 };
// Which output each chop plays on: bus 0 is Main, 1..8 are "Out 2".."Out 9". MainOnly means no routes at all (the maps are empty).
struct OutputRouting {
  RoutingMode mode = RoutingMode::MainOnly;
  std::map<std::string, std::uint8_t> roleBus;   // role name -> bus 0..8
  std::map<std::uint64_t, std::uint8_t> chopBus; // chop id -> bus 0..8, an explicit per-chop override
  friend bool operator==(const OutputRouting& a, const OutputRouting& b) { return a.mode == b.mode && a.roleBus == b.roleBus && a.chopBus == b.chopBus; }
};

// ---- performance pads (F07; composition/src/pads.cpp) ----
// Project-level pad choices. Pad mode and velocity curve are host parameters; takes are never saved.
struct PadSettings {
  std::uint8_t baseNote = 36;          // 0..111; chop i plays on note baseNote + i
  std::uint8_t midiChannel = 0;        // 0 omni, 1..16
  std::uint8_t quantizeDivision = 16;  // 0 off, 4, 8, 16, 32
  bool quantizeTriplet = false;
  std::uint8_t merge = 0;              // 0 add to the pattern, 1 replace the hit's slot
  std::map<std::uint64_t, std::uint8_t> chokeOverride;  // chop id -> 0 off, 1..4; absent = default (role "hat" -> 1, else off)
  friend bool operator==(const PadSettings& a, const PadSettings& b) {
    return a.baseNote == b.baseNote && a.midiChannel == b.midiChannel && a.quantizeDivision == b.quantizeDivision &&
           a.quantizeTriplet == b.quantizeTriplet && a.merge == b.merge && a.chokeOverride == b.chokeOverride;
  }
};
constexpr std::size_t kMaxTakeHits = 4096;
// One recorded pad press. ppqOff < 0 means the release was not seen.
struct TakeHit {
  double ppqOn = 0.0;
  double ppqOff = -1.0;
  std::uint8_t note = 0;
  std::uint8_t velocity = 100;
};
struct TakeOptions {
  int quantizeDivision = 16;  // 0 off, 4, 8, 16, 32
  bool triplet = false;
  std::uint8_t merge = 0;     // 0 add, 1 replace
  render::VelocityCurve curve = render::VelocityCurve::Linear;
  render::PadMode mode = render::PadMode::Poly;
  int baseNote = 36;
};
struct TakeReport {
  int added = 0, replaced = 0, skippedLocked = 0, skippedCap = 0, skippedUnmapped = 0, skippedDuplicate = 0;
};

struct LoadOptions {
  bool deferSourceResolve = false;  // true: never call the resolver and queue no "not found" notice (the caller restores the audio itself)
};

// Given the saved source description, returns decoded audio, or nullptr when it cannot be found.
using SourceResolver = std::function<std::shared_ptr<const render::SourceData>(const source::SourceInfo&)>;

// ---- Evolve scenes (F09; composition/src/scenes.cpp) ----
struct SceneExportOptions {
  bool list = true;      // scenes.tsv
  bool midi = true;      // scene_NN_<name>.mid per scene (repeats capped at 16)
  bool wav = false;      // scenes.wav: every scene rendered and laid end to end, tails overlapping the next scene
  double bpm = 120.0;    // 20..999
  int baseNote = 36;     // 0..127
  wav::Options wavOptions;
  bool overwrite = false;
};
struct SceneStatus {
  bool running = false;              // scenes are being sequenced (including "waiting for the first loop boundary")
  bool holding = false;              // the sequence finished; the last scene keeps looping until stopScenes()
  scenes::SceneId playing = 0;       // 0 while the first scene waits for its boundary
  scenes::SceneId armed = 0;         // the scene that starts at the next loop boundary
  int loopIndex = 0;                 // loops of `playing` completed so far
  int loops = 0;                     // loops `playing` lasts
  bool recording = false;
  bool waitingForTransport = false;  // running, but the host transport is stopped
};

// ---- Arrangement export (F18; composition/src/arrangement_session.cpp) ----
struct ArrangementExportSettings {
  std::string root;          // export folder, created when missing
  double bpm = 120.0;        // 20..999, rounded to a whole BPM as playback does
  wav::Options wavOptions;   // format and render sample rate
  bool levelMatch = false;   // F16: recompute a match gain per scene
  double matchTrimDb = 0.0;
  std::function<void(float)> observer;  // test hook: called on the worker after every progress update
};
// One line of the arrangement list as the dialog shows it (the view model formats the text).
struct ArrangementRow {
  std::uint32_t sceneId = 0;
  int sceneNumber = 0;        // 1-based position in the scene list, 0 when the scene was deleted
  std::string sceneName;      // empty when deleted
  int patternBars = 0;        // 0 when deleted
  int bars = 4, repeats = 1;
  bool deleted = false;       // the scene no longer exists: shown as "Scene deleted", export blocked
  bool fits = true;           // the scene still flattens against today's chops
};

class ProjectSession {
 public:
  ProjectSession();
  ProjectSession(const ProjectSession&) = delete;  // the Groove section decoder captures `this`
  ProjectSession& operator=(const ProjectSession&) = delete;

  // ---- audio side ----
  // Where validated playback snapshots are published. Not owned; must outlive the session or be detached.
  void attachMailbox(render::Mailbox<render::Playback>* mailbox);
  std::shared_ptr<const render::Playback> playback() const { return playback_; }
  Result<std::vector<std::vector<float>>> renderOffline(const render::OfflineSettings& settings) const;

  // ---- gesture effects, output stage (docs/specs/roadmap/E01, E02, F16) ----
  // Which events carry Echo / Tremolo-Pan / Stutter / Roll, and the global fallbacks the blocks refer to. A change
  // re-runs the effect pass over the current pattern (one undo entry, never touches timing or chops) and republishes.
  // Generate, Mutate and every other pattern change keep applying the stored settings.
  Status setFx(const pattern::FxPassSettings& pass, const render::PlaybackOptions& options);
  const pattern::FxPassSettings& fxSettings() const { return fx_; }
  const render::PlaybackOptions& playbackOptions() const { return playbackOptions_; }
  // The parameters the exporter renders with (limiter, ceiling, polish, gesture mask, output gain); dry mix is ignored.
  void setRenderParams(const render::RenderParams& params) { renderParams_ = params; }
  // Same parameters plus the measured match gain, as the processor uses them.
  render::RenderParams renderParams() const;
  float matchGainDb() const { return matchGainDb_.load(std::memory_order_relaxed); }
  // Level match (F16): measures the source and one cycle of the pattern and stores the gain renderParams() reports.
  // Runs on the calling (message or worker) thread; returns a result with valid == false and gain 0 when level match is
  // off, there is no source or pattern, or the pattern is silent.
  Result<levelmatch::MatchResult> updateLevelMatch(const host::ParamValues& values, double bpm);
  Result<render::PreviewRequest> auditionRequest(ChopId chop) const;  // same read/fade path as playback

  // ---- source and chops ----
  Status loadSource(std::shared_ptr<const render::SourceData> data, std::string name, std::string path = {});
  void clearSource();
  bool hasSource() const { return chopMap_.has_value(); }
  bool sourceMissing() const { return chopMap_.has_value() && !sourceData_; }
  const source::ChopMap* chopMap() const { return chopMap_ ? &*chopMap_ : nullptr; }
  ChopSnapshotPtr chops() const { return chops_; }
  // The chops with their engine v2 roles and level-match trims (explicit roles first, then audio analysis); cached.
  // Generation and Mutate of a v2 pattern read this view; describeEvent() needs it too. Null before a source is loaded.
  ChopSnapshotPtr annotatedChops() const;

  Result<std::vector<source::Marker>> previewChops(const DetectOptions& options) const;  // never applies
  Status applyChops(const DetectOptions& options);
  // Single entry point for marker edits (add/move/delete/enable/trim/...). Reconciles the pattern afterwards.
  Status editMarkers(const std::function<Status(source::ChopMap&)>& edit);
  Status undoMarkers();
  Status redoMarkers();
  bool canUndoMarkers() const { return chopMap_ && chopMap_->canUndo(); }
  bool canRedoMarkers() const { return chopMap_ && chopMap_->canRedo(); }
  std::shared_ptr<const render::SourceData> sourceData() const { return sourceData_; }
  // True when the source has no file behind it (captured or stretched), so project state must embed the audio.
  bool sourceNeedsEmbed() const;
  // Input capture "Keep" (composition/src/capture.cpp): loadSource with no path, so the audio is embedded in the project.
  Status loadCapturedSource(std::shared_ptr<const render::SourceData> data, std::string name);
  // ---- tempo-match stretch (composition/src/stretch.cpp) ----
  // Validates the request (ratio 0.5x..2x, 20..999 BPM, size limits, marker collisions) and snapshots the source.
  Result<StretchPlan> planStretch(const StretchRequest& request) const;
  // All-or-nothing: swaps in the stretched source, rescaled markers and pattern, restarts the variation tree.
  Status applyStretched(const StretchPlan& plan, std::shared_ptr<const render::SourceData> stretched);
  // Re-attach audio to a saved project. Must be the same audio (hash match); otherwise load it as a new source.
  Status relinkSource(std::shared_ptr<const render::SourceData> data);

  // ---- roles and grammar ----
  Status assignRole(ChopId chop, const std::string& role);
  Status setRules(roles::RuleSet rules);
  const roles::RoleMap& roleMap() const { return roles_; }
  const roles::RuleSet& rules() const { return rules_; }
  Status clearRole(ChopId chop);                          // NotFound for an unknown chop; no-op when unassigned
  const std::vector<RuleNote>& lastRuleNotes() const { return ruleNotes_; }
  Result<EventExplanation> explainEvent(EventId id) const;  // why the hit is allowed, weighted or protected
  // Pure: generates into a scratch pattern with `draft` and the current roles; nothing is committed or announced.
  Result<RulePreview> previewRules(const roles::RuleSet& draft, const pattern::Settings& settings) const;

  // ---- pattern ----
  const pattern::Pattern* pattern() const { return patterns_.hasPattern() ? &patterns_.current() : nullptr; }
  Status generate(const pattern::Settings& settings, const std::string& label = {});
  // `liveSettings` (optional) refreshes density/variation/swing/transform flags before mutating; changing
  // length, meter, or grid needs Generate and is refused here.
  Status mutate(const pattern::MutateOptions& options, const pattern::Settings* liveSettings = nullptr, const std::string& label = {});
  Status zoomIn(EventId event, const zoom::Settings& settings, std::uint64_t seed);
  Status mutateChildren(EventId event, const zoom::Settings& settings, std::uint64_t seed);
  Status collapse(EventId event);
  // Any pure pattern_engine edit (move, lock, set transform, duplicate bar, ...) goes through here.
  Status edit(const std::function<Result<pattern::Pattern>(const pattern::Pattern&, const ChopSnapshot&)>& fn);
  // Audio-only preview of a pure edit while a slider is dragged: the result is validated and published to the renderer, but the pattern,
  // the undo stack and the family tree are untouched. The edit that follows on release (edit + commitEdit) replaces it;
  // cancelEditPreview() drops it. Returns the same errors as edit().
  Status previewEdit(const std::function<Result<pattern::Pattern>(const pattern::Pattern&, const ChopSnapshot&)>& fn);
  Status cancelEditPreview() { return republish(); }  // publishes the stored pattern again
  Status commitEdit(const std::string& label = {});  // settled manual edits become one history node
  bool undo();
  bool redo();
  Status storeSnapshot(std::size_t slot);  // refreshes a running morph (composition/src/morph.cpp)
  Status recallSnapshot(std::size_t slot);

  // ---- A/B morph (F10; composition/src/morph.cpp) ----
  // Blends the stored A and B patterns by `t` (0 = A, 1 = B) into a PREVIEW that only the audio side sees: pattern(),
  // history, undo and the saved state are untouched until commitMorph(). Every command that changes the pattern ends the
  // morph (a notice says so); Evolve and morph exclude each other.
  Status beginMorph(double t = 0.5);  // both slots required; installs the preview playback
  Status setMorph(double t);          // 0..1, NaN is InvalidArgument; no history, no undo entry
  bool morphActive() const { return morphActive_; }
  double morphAmount() const { return morphT_; }
  // The pattern the panels draw: the scene that plays, or the morph preview while it is active, else pattern().
  const pattern::Pattern* displayPattern() const;
  // Freezes the blend (unmatched hits resolved), records it as a family-tree node "Morph A>B NN%" and one undo step.
  Status commitMorph(const std::string& label = {});
  void cancelMorph();  // back to the real pattern; no notice

  // ---- Evolve scenes (F09; composition/src/scenes.cpp) ----
  // A scene is a stored pattern plus loops and what follows it. Playback is an OVERRIDE: pattern(), history, undo, A/B
  // slots and the saved pattern never change while scenes play; any command that changes the pattern stops the scenes.
  // The processor calls onSceneBoundary after onLoopBoundary and onTransportChanged on host play/stop edges.
  void attachArmMailbox(render::Mailbox<render::ArmedSwitch>* mailbox);
  const scenes::SceneList& sceneList() const { return scenes_; }
  bool sceneFits(scenes::SceneId id) const;  // the stored pattern still flattens against today's chops
  Result<scenes::SceneId> captureScene(const std::string& name = {});  // the real pattern (never the Chaos / morph / scene overlay)
  Status updateScene(scenes::SceneId id);    // replaces the scene's pattern with the current one; stops playback first
  Status editScene(scenes::SceneId id, const std::function<void(scenes::Scene&)>& edit);  // name, loops, next, jumpTo only
  Status removeScene(scenes::SceneId id);
  Status moveScene(scenes::SceneId id, int newIndex);
  Status clearScenes();
  Status setSceneWrap(bool wrap);
  Status setRecordScenes(bool on);
  bool recordingScenes() const { return recordScenes_; }
  Status playScenes(scenes::SceneId first = 0, Quantize when = Quantize::LoopBoundary);
  Status stopScenes();  // back to the real pattern; no notice
  void onSceneBoundary(std::uint64_t appliedToken);  // once per observed pattern-loop boundary, same lock as onLoopBoundary
  void onTransportChanged(bool playing, std::uint64_t appliedToken = 0);  // host play/stop edges
  SceneStatus sceneStatus() const;
  Status keepScene(scenes::SceneId id, const std::string& label = {});  // stops playback, finalize(scene pattern)
  Result<ExportReport> exportScenes(const std::string& directory, const SceneExportOptions& options) const;

  // ---- Arrangement export (F18; composition/src/arrangement_session.cpp) ----
  // The arrangement list is project state (section tag 16, absent while it is the default). It names scenes by id; a deleted
  // scene keeps its row (flagged) and blocks the export. The export runs on one worker thread over an immutable snapshot.
  const Arrangement& arrangement() const { return arrangement_; }
  Status setArrangement(const Arrangement& a);  // stored even when the total is not 8..32
  std::vector<ArrangementRow> arrangementRows() const;
  Status fillArrangementFromChain();            // Use scene chain
  Status autoFillArrangement();                 // Auto fill 16 bars
  // The immutable snapshot a job renders (message thread). Validates in the order of the spec's messages.
  Result<ArrangementPlan> planArrangementExport(const Arrangement& a, const std::string& name, const ArrangementExportSettings& settings) const;
  // Validates (same order as the messages in the spec), snapshots and starts the worker. Returns "An arrangement export is
  // already running." while a job runs; a finished job is replaced.
  Status startArrangementExport(const Arrangement& a, const std::string& name, const ArrangementExportSettings& settings);
  ArrangementJob* arrangementJob() const { return arrangementJob_.get(); }
  void cancelArrangementExport();
  void releaseArrangementJob();                 // after the result was read; cancels and joins a running one

  // ---- variation family tree ----
  const history::VariationTree& history() const { return history_; }
  // Loads a node's exact pattern. LoopBoundary defers until onLoopBoundary() (the safe default during playback).
  Status activate(history::NodeId node, Quantize when);
  void onLoopBoundary();
  bool hasPendingActivation() const { return pendingActivation_ != history::kNoNode; }
  history::NodeId pendingActivationNode() const { return pendingActivation_; }  // the variation queued for the next loop boundary, or kNoNode
  Status renameNode(history::NodeId node, std::string label);
  Status setFavorite(history::NodeId node, bool favorite);
  Status deleteBranch(history::NodeId node, bool includeFavorites = false);
  Result<std::string> compareNodes(history::NodeId a, history::NodeId b) const;

  // ---- multiple outputs (F11; composition/src/routing.cpp) ----
  const OutputRouting& outputRouting() const { return routing_; }
  // Validates (buses 0..8, chops and roles exist), stores (section tag 9, absent while default), rebuilds the Playback with the
  // routing baked in and publishes it. MainOnly clears the maps. All or nothing; no undo step.
  Status setOutputRouting(const OutputRouting& routing);
  Status resetOutputRouting() { return setOutputRouting(OutputRouting{}); }
  // Effective output of a chop: its own override, else its role's route, else Main (0).
  int outputBusOf(ChopId chop) const;
  // Deterministic assignment: one output per role in snapshot order of first appearance (roles beyond 8 stay on Main), or
  // chop n on output 1 + n % 8. Both replace the previous routes and return how many roles or chops were routed.
  int autoRouteByRole();
  int autoRouteByChop();
  // How many aux outputs the host has enabled (message thread; used for status text and "off in host" labels, never for the Playback).
  void setAuxBusesAvailable(int n) { auxAvailable_ = n < 0 ? 0 : (n > render::kMaxAuxBuses ? render::kMaxAuxBuses : n); }
  int auxBusesAvailable() const { return auxAvailable_; }

  // ---- performance pads (F07; composition/src/pads.cpp) ----
  // Where the note-to-chop bank is published. Not owned; must outlive the session or be detached.
  void attachPadMailbox(render::Mailbox<render::PadBank>* mailbox);
  const PadSettings& padSettings() const { return padSettings_; }
  // Validates, stores (section tag 10, absent while default) and republishes the bank. No undo step.
  Status setPadSettings(const PadSettings& settings);
  Status setChokeGroup(ChopId chop, std::uint8_t group);  // 0 off, 1..4
  // The bank last published.
  std::shared_ptr<const render::PadBank> padBank() const { return padBank_; }
  // Quantizes the hits onto the pattern grid and merges them as user-owned events in one undo step. When nothing
  // changes there is no undo entry.
  Result<TakeReport> mergeTake(const std::vector<TakeHit>& hits, const TakeOptions& options);

  // ---- Groove Lab (F17; composition/src/groove.cpp) ----
  // Playback-time swing, push/pull, humanize and accents. Never changes the pattern; rebuilds the playback snapshot.
  // The groove is project state (section tag 14, absent while it equals the default) and has its own undo stack.
  const groove::Groove& groove() const { return groove_; }
  Status setGroove(const groove::Groove& g);
  bool canUndoGroove() const { return grooveHistory_.canUndo(); }
  bool canRedoGroove() const { return grooveHistory_.canRedo(); }
  bool undoGroove();
  bool redoGroove();
  // The events MIDI export and drag-out write: the playback's events, or without the groove when bakeIntoMidi is off.
  Result<FlatEventList> midiEvents() const;

  // ---- Fractal Rhythm ----
  // Replaces the chosen bars (empty = every unlocked bar) with a fractal groove; locked bars are refused when
  // named explicitly and skipped otherwise. The bars become user-owned, so Mutate keeps them.
  Status applyFractal(const fractal::Settings& settings, const std::vector<int>& bars = {}, const std::string& label = {});

  // ---- Evolve ----
  Status setEvolve(const evolve::Settings& settings);  // starts or stops the scheduler
  const evolve::Settings& evolveSettings() const { return evolve_.settings(); }
  bool evolveRunning() const { return evolve_.running(); }
  // Call once per loop at the loop midpoint, from the message thread. Returns true when a step was applied.
  Result<bool> evolveStep(double chaos = 0.0);  // chaos in [0,1] is the live Chaos control (0 = off)
  Status keepEvolved(const std::string& label = {});  // records the current pattern as a family-tree branch

  // ---- Smart Setup ----
  Result<SetupSuggestion> suggestSetup(double hostBpm = 0.0) const;  // never changes state
  // Applies roles only to chops that have none (never overwrites); returns how many were assigned.
  Result<int> acceptRoleSuggestions(const std::vector<insight::RoleSuggestion>& suggestions, double minConfidence = 0.0);
  // Detects chops (merging with the user's markers) when the source has none, then suggests.
  Result<SetupSuggestion> smartSetup(double hostBpm = 0.0);

  // ---- export (message thread; never the audio thread) ----
  Result<ExportReport> exportWav(const std::string& path, const ExportWavOptions& options) const;
  // The same bytes exportKit writes to <name>.mid / a rendered WAV, without touching the filesystem (used by drag-out).
  Result<std::vector<std::uint8_t>> renderMidi(const KitOptions& options) const;
  Result<std::vector<std::uint8_t>> renderWavBytes(const ExportWavOptions& options) const;
  // slice_NN_<role>.wav for every chop, pattern.mid replaying the pattern on those slices, and kit.txt.
  Result<ExportReport> exportKit(const std::string& directory, const KitOptions& options) const;

  // ---- MIDI / audio drag-out and per-variation export (F13; composition/src/midi_drag.cpp) ----
  // Nothing here touches the session, history or undo. The `Current` forms are renderMidi / renderWavBytes with the
  // full option struct. A variation is read from its history node (the live pattern for the active node), repaired
  // with sanitize() against today's chops, and rendered through the same playback path as the live pattern (groove
  // baked per its setting); without decoded audio the MIDI still works from the chop map alone (retrigger sub-hits only).
  Result<std::vector<std::uint8_t>> renderCurrentMidi(const MidiExportOptions& options) const;
  Result<std::vector<std::uint8_t>> renderCurrentWav(const ExportWavOptions& options) const { return renderWavBytes(options); }
  // `skippedEvents` receives how many events were dropped because their chop no longer exists.
  Result<std::vector<std::uint8_t>> renderVariationMidi(history::NodeId node, const MidiExportOptions& options, int* skippedEvents = nullptr) const;
  Result<std::vector<std::uint8_t>> renderVariationWav(history::NodeId node, const ExportWavOptions& options) const;
  Result<ExportReport> exportVariationMidi(history::NodeId node, const std::string& path, const MidiExportOptions& options, bool overwrite) const;
  Result<ExportReport> exportVariationWav(history::NodeId node, const std::string& path, const ExportWavOptions& options) const;
  // Slice WAV bytes (same encode as exportKit: 24-bit, fades 32/64 frames) in chop order, then `<name>.mid`.
  Result<std::vector<KitFile>> renderKitFiles(const KitOptions& options) const;

  // ---- project state ----
  const ProjectExtras& extras() const { return extras_; }
  // Validates, stores. No history node, no undo step, no republish.
  Status setExtras(const ProjectExtras& e);
  bool hasSnapshot(std::size_t slot) const { return patterns_.hasSnapshot(slot); }
  // Feature sections (tags 9..31, F13's tag is F01-owned). `bytes.empty()` removes the section; larger than the
  // per-tag cap is OutOfRange. Empty sections are never written, so an unused feature leaves the bytes unchanged.
  const StateSection* section(std::uint8_t tag) const;
  Status setSection(StateSection s);
  void registerSectionDecoder(std::uint8_t tag, std::string feature, SectionDecoder decoder);
  // True when saveState would write schema 2 (see the constants above). The plugin wrapper follows it.
  bool stateUsesSchemaV2() const;
  const codec::MigrationRegistry& migrations() const { return migrations_; }
  std::size_t embeddedSourceBytes() const;  // estimate shown before the user opts in to embedding
  std::size_t embeddedSourceCap() const { return embeddedCap_; }
  bool wasEmbedded() const { return loadedEmbedded_; }  // the last loaded project carried its audio
  Status setEmbeddedSourceCap(std::size_t bytes);  // 1 .. kCodecCeilingBytes - 64 MiB headroom
  Result<std::vector<std::uint8_t>> saveState(bool embedSource) const;
  // All-or-nothing: on any error the session is left exactly as it was.
  Status loadState(const std::uint8_t* data, std::size_t size, const SourceResolver& resolver) { return loadState(data, size, resolver, LoadOptions{}); }
  Status loadState(const std::uint8_t* data, std::size_t size, const SourceResolver& resolver, const LoadOptions& options);
  // Queues the two warnings loadState uses when the audio of a reference-only project cannot be found (wrongAudio
  // false) or is a different file (true). Used by callers that restored the audio themselves after a deferred load.
  void reportSourceUnavailable(bool wrongAudio);
  void reset();

  std::vector<Notice> takeNotices();
  const std::vector<Notice>& notices() const { return notices_; }

 private:
  struct Target;
  void notice(Notice::Level level, std::string text);
  std::map<std::uint8_t, std::vector<std::uint8_t>> extrasBlocks() const;  // the blocks saveState would write (empty = schema 1)
  void noteReport(const pattern::GenerationReport& report);
  void recordRuleNotes(const pattern::GenerationReport& report);
  std::unique_ptr<roles::GrammarPolicy> makePolicy() const;
  Status finalize(Result<pattern::Pattern> result, const char* historyLabel, const pattern::GenerationReport* report = nullptr);
  Status installPlayback(const pattern::Pattern& p);  // validate + publish; no state change on failure
  Status republish();
  bool settleRestoredPattern();
  void recordHistory(const std::string& label);
  Status applyActivation(history::NodeId node);
  Status reconcileAfterMarkerChange();
  void publish(std::shared_ptr<const render::Playback> pb);
  Status chooseTarget(const pattern::Pattern& p, EventId id, Target& out) const;
  Result<pattern::Pattern> withChild(const pattern::Pattern& p, const Target& t, std::shared_ptr<const NestedPattern> child) const;
  Result<zoom::Context> zoomContext(const pattern::Pattern& p, const Target& t) const;

  std::shared_ptr<const render::SourceData> sourceData_;
  std::optional<source::ChopMap> chopMap_;
  ChopSnapshotPtr chops_;
  roles::RoleMap roles_;
  roles::RuleSet rules_;
  std::vector<RuleNote> ruleNotes_;  // transient: BlockedByRule notes of the last generation step
  std::map<std::string, codec::ModulePayload> foreignModules_;  // modules written by a newer build: kept and re-saved untouched
  pattern::PatternSession patterns_;
  history::VariationTree history_;
  history::NodeId pendingActivation_ = history::kNoNode;
  render::Mailbox<render::Playback>* mailbox_ = nullptr;
  std::shared_ptr<const render::Playback> playback_;
  pattern::FxPassSettings fx_;
  render::PlaybackOptions playbackOptions_;
  render::RenderParams renderParams_;
  std::atomic<float> matchGainDb_{0.f};
  codec::MigrationRegistry migrations_;
  ProjectExtras extras_;
  std::map<std::uint8_t, StateSection> sections_;  // tags 9..31 (and unknown tags >= 32), saved in tag order
  struct SectionDecoderEntry {
    std::string feature;
    SectionDecoder decode;
  };
  std::map<std::uint8_t, SectionDecoderEntry> sectionDecoders_;
  std::size_t embeddedCap_ = kMaxEmbeddedSourceBytes;
  evolve::Controller evolve_;
  bool loadedEmbedded_ = false;
  std::vector<Notice> notices_;
  mutable ChopSnapshotPtr annotated_;
  mutable std::uint64_t annotatedKey_ = 0;
  groove::Groove groove_;
  SnapshotStack<groove::Groove> grooveHistory_{groove::Groove{}};
  // scenes (F09)
  enum class ScenePhase : std::uint8_t { Off, Pending, Running, Held };
  struct BuiltScene {
    std::shared_ptr<const pattern::Pattern> pattern;
    std::shared_ptr<const render::Playback> playback;
  };
  void stopScenesForEdit();  // pattern-changing entry points; the caller publishes the real playback itself
  bool endScenes(const char* noticeText, bool restore);
  void cancelArm();
  Status armScene(scenes::SceneId id);
  bool adoptArmed(std::uint64_t appliedToken);
  void installScene(scenes::SceneId id, const BuiltScene& built);
  Result<BuiltScene> buildScene(const scenes::Scene& scene) const;
  void rebuildPlaylist(bool warn);
  Status refreshScenes();
  Status storeScenesSection();
  Status decodeScenes(const StateSection& s);
  void clearSceneState();
  Result<scenes::SceneId> captureImpl(const std::string& name, std::uint64_t evolveStep, int loops);
  void recordEvolveScene(std::uint64_t stepIndex);
  Result<std::vector<std::uint8_t>> midiFromPattern(const pattern::Pattern& fixed, const MidiExportOptions& options) const;
  scenes::SceneList scenes_;
  scenes::SceneList playlist_;  // the scenes that fit today's chops, patterns left out (the sequencer needs only the metadata)
  scenes::Sequencer sequencer_;
  ScenePhase scenePhase_ = ScenePhase::Off;
  render::Mailbox<render::ArmedSwitch>* armMailbox_ = nullptr;
  std::uint64_t armCounter_ = 0;  // last token issued (arms and cancels share one increasing sequence)
  std::uint64_t armedToken_ = 0;  // token of the pending arm, 0 = none
  scenes::SceneId armedScene_ = 0;
  BuiltScene armedBuilt_;         // keeps the armed Playback alive until it is in the main mailbox
  std::shared_ptr<const pattern::Pattern> sceneOverride_;
  scenes::SceneId playingScene_ = 0;
  bool recordScenes_ = false;
  bool hostPlaying_ = false;
  mutable ChopSnapshotPtr fitChops_;
  mutable std::map<scenes::SceneId, bool> fitCache_;

  // arrangement (F18)
  Status storeArrangementSection();
  Status decodeArrangement(const StateSection& s);
  Result<midi::FileSpec> midiSpecFromPattern(const pattern::Pattern& fixed, const MidiExportOptions& options) const;
  Arrangement arrangement_;
  std::unique_ptr<ArrangementJob> arrangementJob_;

  // morph (F10)
  bool endMorph(bool withNotice);  // drops the preview state only; callers that change the pattern republish themselves
  Status refreshMorph();           // rebuilds and republishes the preview from the current slots and amount
  Result<pattern::Pattern> morphPattern(double t, bool freeze) const;
  bool morphActive_ = false;
  double morphT_ = 0.5;
  std::shared_ptr<const pattern::Pattern> morphPreview_;
  Status decodeGroove(const StateSection& s);
  Status decodePads(const StateSection& s);
  Status decodeRouting(const StateSection& s);
  render::BusRouting busRouting() const;
  void pruneRouting();                 // drops routes of chops that no longer exist
  void resetPerChopRouting();          // new source: routes and choke overrides name chops of the old one
  OutputRouting routing_;
  int auxAvailable_ = 0;
  void publishPadBank();
  PadSettings padSettings_;
  render::Mailbox<render::PadBank>* padMailbox_ = nullptr;
  std::shared_ptr<const render::PadBank> padBank_;
  Result<std::shared_ptr<const render::Playback>> buildPlayback(const pattern::Pattern& p, bool withGroove) const;
};

// ---- host parameter binding ----
// Automatable parameters map onto generation settings; the pattern keeps its own copy, so changing a
// parameter never rewrites an existing pattern until the user presses Generate or Mutate.
pattern::Settings settingsFromParams(const host::ParamValues& values, pattern::Settings base);
// Engine v2 controls (profile, backbeat, anchors, groove / humanize / fill / glitch amounts) from the parameters.
// `setEngineVersion` also copies the engine choice: true when generating, false for Mutate, which keeps the version
// the pattern was made with (composition/src/engine_params.cpp).
void applyBreakParams(const host::ParamValues& values, pattern::Settings& settings, bool setEngineVersion);
// The Groove Lab state the five groove parameters describe, on top of the stored groove (grid, push/pull, accents, seed).
groove::Groove grooveFromParams(const host::ParamValues& values, const groove::Groove& base);
// Chaos macro (docs/specs/pass3/_BRIEF.md P1): returns `base` unchanged for chaos <= 0 (or non-finite);
// otherwise raises variation, density, transform/effect allowances and magnitudes. Monotone in `chaos`,
// output always satisfies pattern::validateSettings when `base` does. settingsFromParams applies it.
pattern::Settings applyChaos(const pattern::Settings& base, double chaos);
double mutateAmountFromParams(const host::ParamValues& values);
// Effect-pass settings and global fallbacks from the automation values (allow_* off means amount 0).
pattern::FxPassSettings fxSettingsFromParams(const host::ParamValues& values, std::uint64_t seed = 1);

}  // namespace chopfractal::composition
