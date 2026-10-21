#pragma once
// Every user action that calls the project session. Panels translate widget state into the small *Ui structs
// below and call a command; the command talks to the processor and the model and reports through the status
// line. Message thread only.
#include <juce_audio_utils/juce_audio_utils.h>

#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/preset_store/presets.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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
  chopfractal::Status startExport(const ExportRequest&);  // the FileChooser flows end here (public so tests can skip the dialog)
  void cancelExport();
  juce::File makeDragFile(bool wav, const ExportUi&);

  // ---- presets ----
  chopfractal::presets::Preset currentPreset(const juce::String& name, const FractalUi&, const EvolveUi&) const;
  void applyPresetParams(const chopfractal::presets::Preset&);  // host parameters plus the status line
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
};
