#pragma once
// Message-thread view model of the editor: the cached views built from the session (rebuilt only when the
// session's published state changes), the selection, and the other transient UI state that more than one panel
// reads. Panels never call the processor themselves; they read this model and call EditorCommands.
#include <juce_audio_utils/juce_audio_utils.h>

#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/plugin_ui_adapter/edit_geometry.hpp>
#include <chopfractal/plugin_ui_adapter/fx_rack_view.hpp>
#include <chopfractal/plugin_ui_adapter/inspector_view.hpp>
#include <chopfractal/plugin_ui_adapter/orbit.hpp>
#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <cstdint>
#include <functional>
#include <vector>

#include "PluginProcessor.h"

// Replaced wholesale on every rebuild; panels read it by const&. `revision` increases with each rebuild.
struct EditorView {
  std::uint64_t revision = 0;
  bool hasSource = false, hasPattern = false;
  std::int64_t sourceFrames = 0;
  chopfractal::ui::PeakBins peaks;  // computed for the wave panel's width and the zoom window; kept while those and the source are unchanged
  chopfractal::ui::ViewWindow window;  // the visible part of the source (the whole clip unless zoomed)
  std::vector<chopfractal::ui::MarkerView> markers;
  std::vector<chopfractal::ChopInfo> chops;
  chopfractal::ui::PatternView pattern;  // the pattern the panels draw: the real one, the A/B morph preview or the playing scene
  juce::String previewLabel;             // "Morph preview" or "Scene preview" while the drawn pattern is not the project's own
  chopfractal::ui::OrbitView orbit;
  std::vector<chopfractal::ui::TreeNodeView> tree;
  int treeCols = 0, treeRows = 0;
  // Pattern geometry; like the peaks, kept from the previous view while the session has no pattern.
  std::int64_t barTicks = 0, gridTicks = 0;
  int barCount = 0;
};

class EditorModel {
 public:
  // Extras: the session's ProjectExtras differ from the last published copy (project load, restored state); panels re-sync
  // their widgets with dontSendNotification. Flags also covers the export running state.
  enum class Change { View, Evolve, Selection, Seed, Status, Setup, Flags, Extras, Scroll, Detect };
  struct Listener {
    virtual ~Listener() = default;
    virtual void modelChanged(Change) = 0;
  };

  explicit EditorModel(ChopFractalProcessor&);
  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

  // Rebuilds the views when the session signature changed or `force`, and runs the per-tick sync of Evolve, the
  // source-missing and embed flags and the status text. Returns true when the view was rebuilt.
  bool refresh(bool force, int waveWidthPx);
  bool rebuild() { return refresh(true, waveWidthPx_); }  // forced rebuild at the width of the last refresh
  const EditorView& view() const { return view_; }

  // ---- selection ----
  chopfractal::EventId selected() const { return selected_; }  // the primary selected hit
  void select(chopfractal::EventId);                            // sets only the primary hit (Orbit View clicks)
  void selectOnly(chopfractal::EventId);                        // one hit as the whole selection (keyboard navigation)
  void clearSelection();                                        // primary hit, hit list and chop list
  const std::vector<chopfractal::EventId>& selectedHits() const { return selectedHits_; }
  const std::vector<chopfractal::ChopId>& selectedChops() const { return selectedChops_; }
  bool hasSelection() const { return selected_.valid(); }
  // The hits the inspector and the effects rack edit: the selected hits, else the primary hit.
  std::vector<chopfractal::EventId> targetHits() const {
    std::vector<chopfractal::EventId> ids = selectedHits_;
    if (ids.empty() && selected_.valid()) ids.push_back(selected_);
    return ids;
  }
  chopfractal::ui::InspectorView inspector();  // built from the session's pattern; call from the message thread
  chopfractal::ui::FxRackView fxRack();
  // The rule editor's view of the session: applied rules, roles, the chops and the notes of the last generation step.
  struct RulesSnapshot {
    chopfractal::roles::RuleSet rules;
    chopfractal::roles::RoleMap roles;
    int patternBars = 0;
    std::vector<chopfractal::ChopInfo> chops;
    std::vector<chopfractal::composition::RuleNote> notes;
    int sampleRate = 48000;
  };
  RulesSnapshot rulesSnapshot();
  // The family tree's extra detail for one node, and its pattern as the pattern view draws it (for Compare thumbnails).
  bool nodeSummary(chopfractal::history::NodeId, chopfractal::history::NodeSummary& out);
  bool nodePatternView(chopfractal::history::NodeId, chopfractal::ui::PatternView& out);
  struct SnapshotState {
    bool hasA = false, hasB = false, morphActive = false;
    double morphAmount = 0.5;
  };
  SnapshotState snapshotState();
  chopfractal::history::NodeId activeNode() const;  // the active family-tree node (kNoNode before the first pattern)
  bool firstHitFx(chopfractal::ui::HitFx& out);  // the effect state of the first target hit (for Copy FX)
  int selectedBar() const;        // bar of the primary hit, 0 if none
  int selectedChopIndex() const;  // lane of the primary hit, -1 if none
  // Rubber-band selections, in the coordinates of the canvas the drag started in (origin at its top-left).
  void selectSectionsInWave(int startX, int endX, bool click, int canvasWidth);
  void selectSectionsInPattern(juce::Rectangle<int> band, juce::Point<int> end, bool click, juce::Rectangle<int> canvas);

  // ---- waveform window (zoom and scroll; transient, reset when the source changes) ----
  const chopfractal::ui::ViewWindow& waveWindow() const { return view_.window; }
  bool waveZoomed() const { return view_.window.startFrame > 0 || view_.window.endFrame < view_.sourceFrames; }
  void setWaveWindow(std::int64_t startFrame, std::int64_t endFrame);  // clamped to the clip and to at least 256 frames
  void fitWave() { setWaveWindow(0, view_.sourceFrames); }
  void zoomWaveAround(double x, double factor);  // factor > 1 zooms in; x in wave-panel pixels
  void scrollWaveBy(double deltaPx);
  // The selected marker (waveform editing); cleared when the marker disappears.
  chopfractal::ChopId selectedMarker() const { return selectedMarker_; }
  void selectMarker(chopfractal::ChopId);

  // ---- pattern lanes (vertical scrolling; transient) ----
  // The pattern panel reports its grid height and the minimum lane height; the lane header column and the grid both read
  // lanes(), so they always scroll together.
  void setLaneView(double viewHeight, double minLaneHeight);
  chopfractal::ui::LaneGeometry lanes() const;
  double laneScroll() const { return laneScroll_; }
  void setLaneScroll(double);
  void revealLane(int lane);

  // ---- detection preview (Detect Chops proposes, the Merge dialog applies) ----
  struct DetectPreview {
    bool active = false;
    chopfractal::composition::DetectOptions options;
    std::vector<std::int64_t> frames;  // proposed marker positions, ascending
    int existing = 0;                  // markers in the map now
  };
  const DetectPreview& detectPreview() const { return detect_; }
  void setDetectPreview(DetectPreview);
  void clearDetectPreview();

  // ---- persisted UI state (ProjectExtras, saved with the project; F01) ----
  // `extras()` is the cached copy of the session's extras. A panel changes it through setExtras(): the mutator edits a
  // copy, the copy is validated and stored in the session (no undo step) and the host is told the state changed. A
  // refused (invalid) edit leaves everything as it was.
  const chopfractal::composition::ProjectExtras& extras() const { return extras_; }
  bool setExtras(const std::function<void(chopfractal::composition::ProjectExtras&)>& mutate);

  std::uint64_t seed() const { return extras_.seed; }  // default 1; the seed field shows at most 12 decimal digits
  void setSeed(std::uint64_t);
  const chopfractal::composition::SetupSuggestion& setup() const { return setup_; }
  bool haveSetup() const { return haveSetup_; }
  void setSetup(chopfractal::composition::SetupSuggestion);  // cleared again by the next rebuild
  bool editMode() const { return extras_.ui.editHits; }       // "Move / resize hits"
  void setEditMode(bool on);
  int detectMode() const { return extras_.detectMode + 1; }   // the Detect Chops mode combo's id, 1..4 (see DetectChoice)
  void setDetectMode(int id) { setExtras([id](chopfractal::composition::ProjectExtras& e) { e.detectMode = id - 1; }); }
  bool sourceMissing() const { return sourceMissing_; }       // refreshed every tick
  // The session's Evolve state, refreshed every tick (it can change between rebuilds).
  bool evolveRunning() const { return evolveRunning_; }
  const chopfractal::evolve::Settings& evolve() const { return evolve_; }

  juce::String statusLine() const;  // the status message
  void setStatus(const juce::String&, Severity = Severity::Info);
  void setProgress(const juce::String&);  // status line without a log entry (export and arrangement progress)
  Severity statusSeverity() const { return proc_.statusSeverity(); }
  // F05 feedback readouts
  chopfractal::history::NodeId pendingNode() const { return pendingNode_; }  // the variation queued for the next loop, or kNoNode
  bool dropHover() const { return dropHover_; }
  void setDropHover(bool on);                                                 // a file drag is over the editor (Change::Flags)
  chopfractal::render::MeterReading takeMeter() { return proc_.takeMeter(); }
  static constexpr int kMaxVoices = 8;
  juce::String syncText(bool compact) const;

  // ---- pass-throughs, so panels never touch the processor ----
  double playheadQuarters() const { return proc_.playheadQuarters(); }
  bool hostPlaying() const { return proc_.hostPlaying.load(); }
  bool usedFallbackTempo() const { return proc_.usedFallbackTempo.load(); }
  bool usedFallbackMeter() const { return proc_.usedFallbackMeter.load(); }
  double manualBpm() const { return proc_.manualBpm.load(); }
  void setManualBpm(double bpm) { proc_.manualBpm = bpm; }
  double activeBpm() const { return proc_.activeBpm.load(); }
  bool embedSource() const { return proc_.embedSource.load(); }
  void setEmbedSource(bool on) { proc_.embedSource = on; }
  // Export on a worker thread (progress shown at the 10 Hz tick, Cancel Export in the status bar).
  bool exportRunning() const { return proc_.exportStatus().state == ExportStatus::State::Running; }
  float exportFraction() const { return proc_.exportStatus().fraction; }

 private:
  void announceSelection();  // status text plus the Selection change, after a rubber-band selection
  void emit(Change c) { listeners_.call([c](Listener& l) { l.modelChanged(c); }); }

  ChopFractalProcessor& proc_;
  juce::ListenerList<Listener> listeners_;
  EditorView view_;
  juce::String signature_;
  const void* peaksSource_ = nullptr;
  int peaksWidth_ = 0;
  int waveWidthPx_ = 0;
  std::int64_t waveStart_ = 0, waveEnd_ = 0;  // 0 / 0: the whole clip
  const void* waveSource_ = nullptr;
  std::int64_t peaksStart_ = -1, peaksEnd_ = -1;
  chopfractal::ChopId selectedMarker_;
  double laneViewH_ = 0.0, minLaneH_ = 24.0, laneScroll_ = 0.0;
  DetectPreview detect_;

  chopfractal::EventId selected_;
  std::vector<chopfractal::EventId> selectedHits_;
  std::vector<chopfractal::ChopId> selectedChops_;

  chopfractal::composition::ProjectExtras extras_;           // cache of session.extras(); updated by setExtras() and by refresh()
  chopfractal::composition::ProjectExtras publishedExtras_;  // what the panels were last told
  bool extrasPublished_ = false;
  chopfractal::composition::SetupSuggestion setup_;
  bool haveSetup_ = false;
  bool sourceMissing_ = false;
  chopfractal::history::NodeId pendingNode_ = chopfractal::history::kNoNode;
  juce::String pendingLabel_;
  bool dropHover_ = false;
  bool evolveRunning_ = false;
  chopfractal::evolve::Settings evolve_;

  bool flagsPublished_ = false, publishedSourceMissing_ = false, publishedEmbed_ = false, publishedExport_ = false;
  juce::String lastStatusKey_;
};
