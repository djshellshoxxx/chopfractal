#pragma once
// Every user action that calls the project session. Panels translate widget state into the small *Ui structs
// below and call a command; the command talks to the processor and the model and reports through the status
// line. Message thread only.
#include <juce_audio_utils/juce_audio_utils.h>

#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/plugin_ui_adapter/session_views.hpp>
#include <chopfractal/preset_store/presets.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <chopfractal/plugin_ui_adapter/edit_geometry.hpp>

#include <chopfractal/plugin_ui_adapter/fx_rack_view.hpp>

#include "DragOut.h"
#include "EditorModel.h"
#include "PluginProcessor.h"
#include "QuickStartTracker.h"

struct FractalUi {
  std::string motif = "x.xx";
  int depth = 2;            // 1..3
  double mutation = -1.0;   // < 0: derive from the Variation control; set by a preset
  double accent = 0.85;
};
struct EvolveUi {
  bool enabled = false;
  int everyIndex = 2;       // 0..4 -> {1, 2, 4, 8, 16} loops
  double amount = 0.25;
};
struct ExportUi {
  int formatIndex = 1;      // 16-bit, 24-bit, 32-bit float
  int rateIndex = 1;        // 44.1, 48, 88.2, 96 kHz
  int loopsIndex = 1;       // {1, 2, 4, 8, 16}
  bool normalize = false;   // scale the render so its peak sits at -1 dB (not saved with the project)
};
enum class DetectChoice { TransientsReplace = 1, TransientsKeepMine = 2, Grid8 = 3, Grid16 = 4 };  // the combo box ids

class EditorCommands {
 public:
  EditorCommands(ChopFractalProcessor&, EditorModel&);

  // withSession, take the notices (the last one wins), set the status (the error message, else the notice) and
  // rebuild the views. Returns the action's status.
  chopfractal::Status run(const std::function<chopfractal::Status(chopfractal::composition::ProjectSession&)>& action);

  // ---- transport and sources ----
  void play();
  void stop();
  void followHost();
  void chooseFile();
  // Loads a loop; asks first (Replace / Cancel) when the project already has a pattern or history. Tests replace the hook.
  void requestLoad(const juce::File&);
  std::function<void(std::function<void(bool)>)> confirmReplace;
  // Quick start (F19): called when a real event happens that the checklist tracks (audible playback, a finished export).
  std::function<void(QuickStep)> onMilestone;
  bool projectHasWork();
  void locateSource();
  void handleDroppedFiles(const juce::StringArray& files);
  static bool isAudioFile(const juce::File&);

  // ---- generation and history ----
  void detectChops(DetectChoice);
  void generate();
  void mutate();
  // Mutate that re-evaluates every unlocked, non-hand-placed beat (amount 1.0) under the label "Mutate (rules)"; used by the rules editor.
  void mutateAll();
  void reroll();
  void undo();
  void redo();
  void zoomInSelected();
  void collapseSelected();
  void toggleLockSelectedBar();
  void newSeed();
  void storeSnapshot(std::size_t slot);
  void recallSnapshot(std::size_t slot);
  void setRoleOfSelected(const juce::String& role);
  void activateNode(chopfractal::history::NodeId);  // at the next loop boundary while the host plays
  // ---- history drawer (G2) ----
  void setNodeFavorite(chopfractal::history::NodeId, bool favorite);
  void renameNode(chopfractal::history::NodeId, const std::string& name);
  void deleteBranch(chopfractal::history::NodeId);
  juce::String compareText(chopfractal::history::NodeId a, chopfractal::history::NodeId b);  // "12 vs 14 events; bars that differ: 2"; also the status line
  void beginMorph(double t);   // needs both comparison slots
  void setMorph(double t);
  void commitMorph();          // keeps the blend as a new family-tree node
  void cancelMorph();
  void historyMenu(chopfractal::history::NodeId, juce::Component* anchor);

  // ---- Smart Setup, Fractal, Evolve, export ----
  void smartSetup();
  void acceptRoles();
  void useTempo();
  void applyFractal(const FractalUi&);
  // `pushed` is the panel's record of the settings it last sent: read for the previous state and updated to the new
  // settings before the session call (as the old editor did). Returns whether the session now runs Evolve.
  bool setEvolve(const EvolveUi&, chopfractal::evolve::Settings& pushed);
  void keepEvolved();
  void exportWav(const ExportUi&);
  void exportKit();
  // The export drawer's settings live in the project (extras.exportUi): format, rate, loops and the MIDI options.
  ExportUi exportUi() const;
  void setExportUi(const ExportUi&);
  void setMidiSettings(const chopfractal::composition::MidiExportSettings&);
  chopfractal::composition::KitOptions kitOptions() const;               // MIDI settings, tempo and the loop count for the kit
  chopfractal::composition::MidiExportOptions midiOptions() const;      // the same for a dragged MIDI file
  chopfractal::composition::ExportWavOptions wavOptions(const ExportUi&) const;
  std::unique_ptr<dragout::DragFileSet> makeKitDragSet();               // slice WAVs plus the MIDI file, for Drag Kit

  chopfractal::Status startExport(const ExportRequest&);  // the FileChooser flows end here (public so tests can skip the dialog)
  void cancelExport();
  juce::File makeDragFile(bool wav, const ExportUi&);
  // F13: a family-tree node dragged out as MIDI (or, Alt-drag, as audio). Renders that node's variation, not the active one, and
  // never changes the session. An empty File (and a status line) when the node cannot be rendered.
  juce::File makeVariationDragFile(chopfractal::history::NodeId node, bool wav);
  // The node menu items "Export MIDI..." and "Export WAV...": a save dialog, then the variation is written there.
  void exportVariation(chopfractal::history::NodeId node, bool wav);

  NoticeLog& noticeLog() { return proc_.noticeLog(); }

  // ---- diagnostics (settings drawer, status bar) ----
  juce::String diagnosticsText() const;       // version, host, audio, interface, source, tempo, output and recent notices
  void copyDiagnostics();                      // puts diagnosticsText on the clipboard and says so in the status bar

  // ---- presets ----
  // The live state as a user preset: flat controls, engine v2 and effect parameters, seed, grid, Fractal settings, rules.
  chopfractal::presets::Preset currentPreset(const juce::String& name, const FractalUi&, const EvolveUi&) const;
  // Sets the preset's host parameters (with change gestures), replaces seed, motif, depth, grid and rules, keeps one level
  // of undo and says what happened. Never touches the pattern or the protected output parameters. With `generateAfter`
  // it runs Generate straight after. Returns false (and changes nothing) for an invalid preset.
  bool applyPreset(const chopfractal::presets::Preset&, bool generateAfter = false);
  bool canUndoPreset() const { return presetUndo_.valid; }
  bool undoPreset();  // restores parameters, extras and rules from before the last applyPreset
  void savePreset(const chopfractal::presets::Preset&, bool overwrite);
  void deletePreset(const juce::File&);

  // ---- canvas gestures ----
  void audition(chopfractal::ChopId);
  void auditionFrame(std::int64_t frame);
  void addMarkerAt(std::int64_t frame);
  void removeMarkerNear(double xPx, double tolerancePx = 8.0);
  // One edit gesture = one undo step.
  void editHits(const std::function<chopfractal::Result<chopfractal::pattern::Pattern>(const chopfractal::pattern::Pattern&, const chopfractal::ChopSnapshot&)>& fn);
  void nudgeSelected(int cells, bool resize);
  void deleteSelected();
  void toggleMuteSelected();
  void duplicateSelected();

  // ---- host parameters (command palette; same gesture as a drag, so automation recording and host undo behave) ----
  // Sets the plain value (snapped to the manifest's range and step) and returns what the parameter now holds, or NaN when the
  // id is not a parameter.
  double setParameter(const std::string& id, double plain);
  double parameter(const std::string& id) const;

  void say(const juce::String& text) { model_.setStatus(text); }

  // ---- direct editing (G2 / F02; EditorEditCommands.cpp) ----
  // Every pattern command is one session edit (one undo step, one "Edited hits" history node) and refuses locked or nested hits
  // with the messages of docs/specs/roadmap/F02-direct-editing.md. `lane` is an index into the chop list, -1 keeps the hit's chop.
  static constexpr const char* kMsgLocked = "That hit is locked. Unlock it or its bar first.";
  static constexpr const char* kMsgNested = "Zoomed hits cannot be edited here. Collapse the zoom first.";
  // Empty when the hit can be edited, else the message to show.
  juce::String hitGuard(chopfractal::EventId) const;
  void moveHit(chopfractal::EventId, int bar, chopfractal::Ticks startInBar, int lane);
  void resizeHit(chopfractal::EventId, chopfractal::Ticks newDuration);
  void duplicateHit(chopfractal::EventId, int bar, chopfractal::Ticks startInBar, int lane);
  void addHit(int bar, chopfractal::Ticks startInBar, int lane);
  void deleteHit(chopfractal::EventId);
  void setHitEnabled(chopfractal::EventId, bool);
  void setHitLocked(chopfractal::EventId, bool);
  void resetHit(chopfractal::EventId);  // transform, filter / glide / crunch and gesture effects back to defaults
  // Inspector and effects rack: the same change applied to several hits as one undo step. Mutators receive each hit's current value.
  void setHitsTransform(const std::vector<chopfractal::EventId>&, const std::function<void(chopfractal::EventTransform&)>&);
  void setHitsFx(const std::vector<chopfractal::EventId>&, const std::function<void(chopfractal::EventFx&)>&);
  void setHitsGesture(const std::vector<chopfractal::EventId>&, const std::function<void(chopfractal::pattern::GestureFx&)>&);
  // The rack: every selected hit's effect state (filter / glide / crunch plus the four gesture blocks) rewritten in one undo step.
  void setHitsFxState(const std::vector<chopfractal::EventId>&, const std::function<void(chopfractal::ui::HitFx&)>&);
  void setRoleOfHits(const std::vector<chopfractal::EventId>&, const juce::String& role);  // the role of every chop the hits use
  void setHitsProbability(const std::vector<chopfractal::EventId>&, float probability);
  // Live preview while a slider is dragged: the audio follows at once, nothing is recorded and no message is shown (the commit on
  // release is the one undo step and reports any refusal).
  void previewHitsTransform(const std::vector<chopfractal::EventId>&, const std::function<void(chopfractal::EventTransform&)>&);
  void previewHitsProbability(const std::vector<chopfractal::EventId>&, float probability);
  void setHitsLocked(const std::vector<chopfractal::EventId>&, bool);
  void setHitsLane(const std::vector<chopfractal::EventId>&, int lane);  // change the chop (source) of the hits
  // ---- rules editor (G2 / F06; EditorEditCommands.cpp). Rule logic lives in the session; these only route and phrase the result. ----
  bool applyRules(const chopfractal::roles::RuleSet&);          // false (with the module's message as status) when refused
  void testRules(const chopfractal::roles::RuleSet&);           // preview without changing anything; the result goes to the status line
  juce::String explainSelected();                               // why the selected hit is there; also the status line; empty with no selection
  bool setChopRole(chopfractal::ChopId, const juce::String&);  // empty clears the role
  void selectBeat(int bar, int beat);                           // highlights the first hit of that beat
  void moveMarker(chopfractal::ChopId, std::int64_t frame);
  void deleteMarker(chopfractal::ChopId);
  void setMarkerEnabled(chopfractal::ChopId, bool);
  void renameMarker(chopfractal::ChopId, const std::string&);
  void trimMarker(chopfractal::ChopId, std::uint32_t trimStart, std::uint32_t trimEnd);
  void undoMarkers();
  void redoMarkers();
  bool canUndoMarkers() const;
  bool canRedoMarkers() const;
  void setMarkerSnap(int mode);   // 0 Off, 1 Transient, 2 Zero crossing (saved with the project)
  void setGridSnap(bool on);
  void setSnapDivision(int division);  // 0 = the pattern grid, else 4, 8, 16, 32
  // Snapped frame for a marker placed or dragged at `frame` (honours the project's snap mode; `bypass` is the Cmd key).
  std::int64_t snapFrame(std::int64_t frame, double framesPerPixel, bool bypass) const;
  // Detection: preview proposes markers (nothing changes), applyDetect commits them, cancelDetect drops the preview.
  // With no markers yet there is nothing to merge, so beginDetect applies at once; it returns true when a preview is waiting.
  bool beginDetect(DetectChoice);
  void setDetectMerge(bool merge);  // Replace (false) or Merge with my markers (true); refreshes the preview
  void applyDetect();
  void cancelDetect();
  chopfractal::Ticks snapGridTicks() const;  // the effective snap step: the project grid or the chosen division

 private:
  void mutateImpl(double amountOverride, const std::string& label);
  ChopFractalProcessor& proc_;
  EditorModel& model_;
  chopfractal::composition::DetectOptions detectOptionsFor(int mode, chopfractal::composition::ProjectSession&) const;
  void refreshDetectPreview(const chopfractal::composition::DetectOptions&);
  std::unique_ptr<juce::FileChooser> chooser_;
  struct PresetUndo {
    bool valid = false;
    std::vector<chopfractal::presets::ParamValue> params;
    chopfractal::composition::ProjectExtras extras;
    chopfractal::roles::RuleSet rules;
  } presetUndo_;
  void setParamPlain(const std::string& id, double plain);
  bool normalizeExport_ = false;
  int detectMode_ = 1;
};
