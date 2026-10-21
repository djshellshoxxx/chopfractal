#pragma once
// Message-thread view model of the editor: the cached views built from the session (rebuilt only when the
// session's published state changes), the selection, and the other transient UI state that more than one panel
// reads. Panels never call the processor themselves; they read this model and call EditorCommands.
#include <juce_audio_utils/juce_audio_utils.h>

#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/plugin_ui_adapter/orbit.hpp>
#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <cstdint>
#include <vector>

#include "PluginProcessor.h"

// Replaced wholesale on every rebuild; panels read it by const&. `revision` increases with each rebuild.
struct EditorView {
  std::uint64_t revision = 0;
  bool hasSource = false, hasPattern = false;
  std::int64_t sourceFrames = 0;
  chopfractal::ui::PeakBins peaks;  // computed for the wave panel's width; kept while the source and width are unchanged
  std::vector<chopfractal::ui::MarkerView> markers;
  std::vector<chopfractal::ChopInfo> chops;
  chopfractal::ui::PatternView pattern;
  chopfractal::ui::OrbitView orbit;
  std::vector<chopfractal::ui::TreeNodeView> tree;
  int treeCols = 0, treeRows = 0;
  // Pattern geometry; like the peaks, kept from the previous view while the session has no pattern.
  std::int64_t barTicks = 0, gridTicks = 0;
  int barCount = 0;
};

class EditorModel {
 public:
  enum class Change { View, Evolve, Selection, Seed, Status, Setup, Flags, Capture, Extras /* emitted from F01 on */ };
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
  void clearSelection();                                        // primary hit, hit list and chop list
  const std::vector<chopfractal::EventId>& selectedHits() const { return selectedHits_; }
  const std::vector<chopfractal::ChopId>& selectedChops() const { return selectedChops_; }
  bool hasSelection() const { return selected_.valid(); }
  int selectedBar() const;        // bar of the primary hit, 0 if none
  int selectedChopIndex() const;  // lane of the primary hit, -1 if none
  // Rubber-band selections, in the coordinates of the canvas the drag started in (origin at its top-left).
  void selectSectionsInWave(int startX, int endX, bool click, int canvasWidth);
  void selectSectionsInPattern(juce::Rectangle<int> band, juce::Point<int> end, bool click, juce::Rectangle<int> canvas);

  // ---- transient UI state ----
  std::uint64_t seed() const { return seed_; }  // default 1; the seed field shows at most 12 decimal digits
  void setSeed(std::uint64_t);
  const chopfractal::composition::SetupSuggestion& setup() const { return setup_; }
  bool haveSetup() const { return haveSetup_; }
  void setSetup(chopfractal::composition::SetupSuggestion);  // cleared again by the next rebuild
  bool editMode() const { return editMode_; }                 // "Move / resize hits"
  void setEditMode(bool on);
  int detectMode() const { return detectMode_; }              // the Detect Chops mode combo's id, 1..4 (see DetectChoice)
  void setDetectMode(int id) { detectMode_ = id; }
  bool sourceMissing() const { return sourceMissing_; }       // refreshed every tick
  // The session's Evolve state, refreshed every tick (it can change between rebuilds).
  bool evolveRunning() const { return evolveRunning_; }
  const chopfractal::evolve::Settings& evolve() const { return evolve_; }

  juce::String statusLine() const;  // the status message plus the host-fallback hints
  void setStatus(const juce::String&);

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
  ChopFractalProcessor::CaptureInfo captureInfo() const { return proc_.captureInfo(); }

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

  chopfractal::EventId selected_;
  std::vector<chopfractal::EventId> selectedHits_;
  std::vector<chopfractal::ChopId> selectedChops_;

  std::uint64_t seed_ = 1;
  chopfractal::composition::SetupSuggestion setup_;
  bool haveSetup_ = false;
  bool editMode_ = false;
  int detectMode_ = 1;
  bool sourceMissing_ = false;
  bool evolveRunning_ = false;
  chopfractal::evolve::Settings evolve_;

  bool flagsPublished_ = false, publishedSourceMissing_ = false, publishedEmbed_ = false;
  juce::String lastStatusKey_, lastCaptureKey_;
};
