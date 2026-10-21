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

#include "DragOut.h"
#include "EditorModel.h"
#include "PluginProcessor.h"

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
// What the arrangement part of the export drawer shows, read under the session lock.
struct ArrangementState {
  chopfractal::composition::Arrangement arrangement;
  std::vector<chopfractal::composition::ArrangementRow> rows;
  std::vector<std::pair<std::uint32_t, std::string>> scenes;  // id and "Scene 3 Verse" in list order
  bool hasSource = false, running = false;
  int jobSection = 0, jobSections = 0;
  double jobFraction = 0.0;
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
  bool projectHasWork();
  void locateSource();
  void handleDroppedFiles(const juce::StringArray& files);
  static bool isAudioFile(const juce::File&);

  // ---- generation and history ----
  void detectChops(DetectChoice);
  void generate();
  void mutate();
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

  // ---- arrangement export (F18) ----
  ArrangementState arrangementState();
  bool editArrangement(const std::function<void(chopfractal::composition::Arrangement&)>& edit);  // false (and a status line) when refused
  void arrangementFromChain();
  void autoFillArrangement();
  void exportArrangement();                                  // folder chooser, then startArrangementExport
  chopfractal::Status startArrangementExport(const std::string& folder);
  void cancelArrangement();
  bool pollArrangement();                                    // 10 Hz: reports a finished job once; true while one runs
  chopfractal::Status startExport(const ExportRequest&);  // the FileChooser flows end here (public so tests can skip the dialog)
  void cancelExport();
  juce::File makeDragFile(bool wav, const ExportUi&);

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

  void say(const juce::String& text) { model_.setStatus(text); }

 private:
  ChopFractalProcessor& proc_;
  EditorModel& model_;
  std::unique_ptr<juce::FileChooser> chooser_;
  struct PresetUndo {
    bool valid = false;
    std::vector<chopfractal::presets::ParamValue> params;
    chopfractal::composition::ProjectExtras extras;
    chopfractal::roles::RuleSet rules;
  } presetUndo_;
  void setParamPlain(const std::string& id, double plain);
  bool normalizeExport_ = false;
};
