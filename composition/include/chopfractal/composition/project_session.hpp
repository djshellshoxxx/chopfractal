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
#include <chopfractal/chop_insight/insight.hpp>
#include <chopfractal/evolve/evolve.hpp>
#include <chopfractal/fractal_rhythm/fractal.hpp>
#include <chopfractal/wav_export/wav.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chopfractal::composition {

constexpr std::uint32_t kSessionSchemaVersion = 1;
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
  Result<bool> evolveStep();
  Status keepEvolved(const std::string& label = {});  // records the current pattern as a family-tree branch

  // ---- Smart Setup ----
  Result<SetupSuggestion> suggestSetup(double hostBpm = 0.0) const;  // never changes state
  // Applies roles only to chops that have none (never overwrites); returns how many were assigned.
  Result<int> acceptRoleSuggestions(const std::vector<insight::RoleSuggestion>& suggestions, double minConfidence = 0.0);
  // Detects chops (merging with the user's markers) when the source has none, then suggests.
  Result<SetupSuggestion> smartSetup(double hostBpm = 0.0);

  // ---- export (message thread; never the audio thread) ----
  Result<ExportReport> exportWav(const std::string& path, const ExportWavOptions& options) const;
  // slice_NN_<role>.wav for every chop, pattern.mid replaying the pattern on those slices, and kit.txt.
  Result<ExportReport> exportKit(const std::string& directory, const KitOptions& options) const;

  // ---- project state ----
  std::size_t embeddedSourceBytes() const;  // estimate shown before the user opts in to embedding
  std::size_t embeddedSourceCap() const { return embeddedCap_; }
  Status setEmbeddedSourceCap(std::size_t bytes);  // 1 .. kCodecCeilingBytes - 64 MiB headroom
  Result<std::vector<std::uint8_t>> saveState(bool embedSource) const;
  // All-or-nothing: on any error the session is left exactly as it was.
  Status loadState(const std::uint8_t* data, std::size_t size, const SourceResolver& resolver);
  void reset();

  std::vector<Notice> takeNotices();
  const std::vector<Notice>& notices() const { return notices_; }

 private:
  struct Target;
  void notice(Notice::Level level, std::string text);
  void noteReport(const pattern::GenerationReport& report);
  std::unique_ptr<roles::GrammarPolicy> makePolicy() const;
  Status finalize(Result<pattern::Pattern> result, const char* historyLabel, const pattern::GenerationReport* report = nullptr);
  Status installPlayback(const pattern::Pattern& p);  // validate + publish; no state change on failure
  Status republish();
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
  pattern::PatternSession patterns_;
  history::VariationTree history_;
  history::NodeId pendingActivation_ = history::kNoNode;
  render::Mailbox<render::Playback>* mailbox_ = nullptr;
  std::shared_ptr<const render::Playback> playback_;
  codec::MigrationRegistry migrations_;
  std::size_t embeddedCap_ = kMaxEmbeddedSourceBytes;
  evolve::Controller evolve_;
  std::vector<Notice> notices_;
};

// ---- host parameter binding ----
// Automatable parameters map onto generation settings; the pattern keeps its own copy, so changing a
// parameter never rewrites an existing pattern until the user presses Generate or Mutate.
pattern::Settings settingsFromParams(const host::ParamValues& values, pattern::Settings base);
double mutateAmountFromParams(const host::ParamValues& values);

}  // namespace chopfractal::composition
