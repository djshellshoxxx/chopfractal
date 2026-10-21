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
#include <chopfractal/fractal_rhythm/fractal.hpp>
#include <chopfractal/wav_export/wav.hpp>
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
struct KitOptions {
  int baseNote = 36;            // chop n -> note baseNote + n
  double bpm = 120.0;
  int loops = 1;                // 1..16 repeats of the pattern in pattern.mid
  bool overwrite = false;
  std::string name = "chopfractal";
};

// ---- project extras (state v2, docs/specs/roadmap/F01-state-v2-and-persistence.md section 4) ----
// UI and generation choices that used to be lost on save. They live outside the history (no undo step).
struct ExportUiSettings {
  int formatIndex = 1;  // 16-bit, 24-bit, 32-bit float
  int rateIndex = 1;    // 44.1, 48, 88.2, 96 kHz
  int loopsIndex = 1;   // {1, 2, 4, 8, 16}
  friend bool operator==(const ExportUiSettings& a, const ExportUiSettings& b) {
    return a.formatIndex == b.formatIndex && a.rateIndex == b.rateIndex && a.loopsIndex == b.loopsIndex;
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
constexpr std::uint8_t kTagFractalTune = 13;  // owned by F01 itself (written from ProjectExtras), refused by setSection
std::size_t sectionByteCap(std::uint8_t tag);  // the per-tag cap of the registry
constexpr std::size_t kMaxExtrasBlocks = 32;
constexpr std::size_t kMaxExtrasBytes = 40u * 1024u * 1024u;

struct LoadOptions {
  bool deferSourceResolve = false;  // true: never call the resolver and queue no "not found" notice (the caller restores the audio itself)
};

// Given the saved source description, returns decoded audio, or nullptr when it cannot be found.
using SourceResolver = std::function<std::shared_ptr<const render::SourceData>(const source::SourceInfo&)>;

class ProjectSession {
 public:
  ProjectSession();

  // ---- audio side ----
  // Where validated playback snapshots are published. Not owned; must outlive the session or be detached.
  void attachMailbox(render::Mailbox<render::Playback>* mailbox);
  std::shared_ptr<const render::Playback> playback() const { return playback_; }
  Result<std::vector<std::vector<float>>> renderOffline(const render::OfflineSettings& settings) const;
  Result<render::PreviewRequest> auditionRequest(ChopId chop) const;  // same read/fade path as playback

  // ---- source and chops ----
  Status loadSource(std::shared_ptr<const render::SourceData> data, std::string name, std::string path = {});
  void clearSource();
  bool hasSource() const { return chopMap_.has_value(); }
  bool sourceMissing() const { return chopMap_.has_value() && !sourceData_; }
  const source::ChopMap* chopMap() const { return chopMap_ ? &*chopMap_ : nullptr; }
  ChopSnapshotPtr chops() const { return chops_; }

  Result<std::vector<source::Marker>> previewChops(const DetectOptions& options) const;  // never applies
  Status applyChops(const DetectOptions& options);
  // Single entry point for marker edits (add/move/delete/enable/trim/...). Reconciles the pattern afterwards.
  Status editMarkers(const std::function<Status(source::ChopMap&)>& edit);
  Status undoMarkers();
  Status redoMarkers();
  std::shared_ptr<const render::SourceData> sourceData() const { return sourceData_; }
  // True when the source has no file behind it (captured or stretched), so project state must embed the audio.
  bool sourceNeedsEmbed() const;
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
  Status commitEdit(const std::string& label = {});  // settled manual edits become one history node
  bool undo();
  bool redo();
  Status storeSnapshot(std::size_t slot) { return patterns_.storeSnapshot(slot); }
  Status recallSnapshot(std::size_t slot);

  // ---- variation family tree ----
  const history::VariationTree& history() const { return history_; }
  // Loads a node's exact pattern. LoopBoundary defers until onLoopBoundary() (the safe default during playback).
  Status activate(history::NodeId node, Quantize when);
  void onLoopBoundary();
  bool hasPendingActivation() const { return pendingActivation_ != history::kNoNode; }
  Status renameNode(history::NodeId node, std::string label);
  Status setFavorite(history::NodeId node, bool favorite);
  Status deleteBranch(history::NodeId node, bool includeFavorites = false);
  Result<std::string> compareNodes(history::NodeId a, history::NodeId b) const;

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
  std::map<std::string, codec::ModulePayload> foreignModules_;  // modules written by a newer build: kept and re-saved untouched
  pattern::PatternSession patterns_;
  history::VariationTree history_;
  history::NodeId pendingActivation_ = history::kNoNode;
  render::Mailbox<render::Playback>* mailbox_ = nullptr;
  std::shared_ptr<const render::Playback> playback_;
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
};

// ---- host parameter binding ----
// Automatable parameters map onto generation settings; the pattern keeps its own copy, so changing a
// parameter never rewrites an existing pattern until the user presses Generate or Mutate.
pattern::Settings settingsFromParams(const host::ParamValues& values, pattern::Settings base);
// Chaos macro (docs/specs/pass3/_BRIEF.md P1): returns `base` unchanged for chaos <= 0 (or non-finite);
// otherwise raises variation, density, transform/effect allowances and magnitudes. Monotone in `chaos`,
// output always satisfies pattern::validateSettings when `base` does. settingsFromParams applies it.
pattern::Settings applyChaos(const pattern::Settings& base, double chaos);
double mutateAmountFromParams(const host::ParamValues& values);

}  // namespace chopfractal::composition
