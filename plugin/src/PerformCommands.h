#pragma once
// Commands and view-model feeds of the wave-two drawers (capture, tempo match, pads, scenes, morph, outputs, arrangement).
// Same rules as EditorCommands: message thread only, the panels call these and draw what the portable view models
// (plugin_ui_adapter) return; every session call runs inside proc.withSession and reports through the status line.
// The control-to-call tables are the "GUI command list" sections of docs/specs/roadmap/F07, F09, F10, F11, F12, F14 and F18.
#include <juce_audio_utils/juce_audio_utils.h>

#include <chopfractal/plugin_ui_adapter/capture_view.hpp>
#include <chopfractal/plugin_ui_adapter/outputs_view.hpp>
#include <chopfractal/plugin_ui_adapter/pads_view.hpp>
#include <chopfractal/plugin_ui_adapter/session_views.hpp>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "EditorCommands.h"
#include "EditorModel.h"
#include "PluginProcessor.h"
#include "StretchRunner.h"

class PerformCommands {
 public:
  PerformCommands(ChopFractalProcessor&, EditorModel&, EditorCommands&);
  ~PerformCommands();

  // Asks before a destructive step. The default shows an AlertWindow (buttons `ok` and "Cancel"); tests replace it.
  using Confirm = std::function<void(const juce::String& title, const juce::String& message, const juce::String& ok, std::function<void()> onOk)>;
  Confirm confirm;

  // Drains the session notices (boundary events such as "Morph ended ..." arrive between commands) into the status line.
  void drainNotices();
  // The editor's 10 Hz tick: applies a finished stretch, then drains the notices.
  void background();

  // ---------------------------------------------------------------------------------------------
  // Input capture (F12 section 12)
  // ---------------------------------------------------------------------------------------------
  struct CaptureChoice {
    chopfractal::capture::CaptureMode mode = chopfractal::capture::CaptureMode::SyncedBars;
    int bars = 4;
    double maxSeconds = 30.0;
  };
  chopfractal::ui::CaptureView captureView(const CaptureChoice&);
  chopfractal::capture::CaptureStatus captureStatus() { return proc_.captureEngine().status(); }
  double sampleRate() const { return proc_.getSampleRate() > 0 ? proc_.getSampleRate() : 48000.0; }
  void arm(const CaptureChoice&, bool needsConfirm);
  void startCapture();
  void stopCapture() { proc_.stopCapture(); }
  void discardCapture() { proc_.discardCapture(); }
  void keepCapture(const chopfractal::ui::CaptureView&, bool chopAfter);
  void undoKeep();

  // ---------------------------------------------------------------------------------------------
  // Tempo match (F14 section 3)
  // ---------------------------------------------------------------------------------------------
  StretchRunner& stretch() { return stretch_; }
  bool usedHostTempo() const { return !model_.usedFallbackTempo(); }
  double targetBpmDefault() const { return usedHostTempo() ? model_.activeBpm() : model_.manualBpm(); }
  // The loop's length in seconds and the loop-length choices (suggestions first), empty without a source.
  struct SourceBrief {
    bool present = false, missing = false;
    std::int64_t frames = 0;
    int sampleRate = 0, channels = 0;
    double seconds() const { return sampleRate > 0 ? static_cast<double>(frames) / sampleRate : 0.0; }
  };
  SourceBrief sourceBrief();
  double sourceSeconds() { return sourceBrief().seconds(); }
  // Smart Setup's loop suggestions when it has run for these chops, else the same analysis from the length and host tempo.
  std::vector<chopfractal::composition::LoopLengthChoice> loopChoices();
  std::string loopChoiceKey();  // changes when loopChoices() would
  bool sourceLooksStretched();
  bool hasPattern();
  void startStretch(const chopfractal::composition::StretchRequest&);
  void cancelStretch();
  void undoStretch();

  // ---------------------------------------------------------------------------------------------
  // Performance pads (F07 section 12)
  // ---------------------------------------------------------------------------------------------
  // `sounding` is the held-lit note mask the panel keeps (bit n of word n >> 5); everything else is read here.
  chopfractal::ui::PadView padView(int page, const std::array<std::uint32_t, 4>& sounding);
  std::array<std::uint32_t, 4> takePadActivity() { return proc_.takePadActivity(); }
  chopfractal::composition::PadSettings padSettings() { return proc_.padSettings(); }
  void editPadSettings(const std::function<void(chopfractal::composition::PadSettings&)>& edit);
  void padTrigger(int note, int velocity, bool on) { proc_.padTrigger(note, velocity, on); }
  void padAllNotesOff() { proc_.padAllNotesOff(); }
  void setChokeGroup(int chopIndex, int group);
  void setRecordArmed(bool on) { proc_.setRecordArmed(on); }
  void discardTake() { proc_.discardTake(); }

  // ---------------------------------------------------------------------------------------------
  // Scenes and A/B morph (F09 and F10 section 12)
  // ---------------------------------------------------------------------------------------------
  struct SceneInfo {
    chopfractal::scenes::SceneId id = 0;
    std::string name;
    int loops = 4;
    chopfractal::scenes::Next next = chopfractal::scenes::Next::Advance;
    chopfractal::scenes::SceneId jumpTo = 0;
    std::uint64_t seed = 0, evolveStep = 0;
    std::uint32_t eventCount = 0;
    int bars = 0;
    bool fits = true;
  };
  struct SceneState {
    std::vector<SceneInfo> scenes;
    bool wrap = true, recording = false, evolveRunning = false, hasPattern = false, hasSource = false;
    chopfractal::composition::SceneStatus status;
    bool hasA = false, hasB = false, morphActive = false;
    double morphAmount = 0.5;
    int indexOf(chopfractal::scenes::SceneId id) const {
      for (std::size_t i = 0; i < scenes.size(); ++i)
        if (scenes[i].id == id) return static_cast<int>(i);
      return -1;
    }
  };
  SceneState sceneState();  // one lock, everything the scene strip, the detail form and the morph row need
  chopfractal::ui::SceneView sceneView(const SceneState&, int selectedIndex);
  chopfractal::ui::MorphView morphView(const SceneState&);
  void captureScene();
  void updateScene(chopfractal::scenes::SceneId);
  void keepScene(chopfractal::scenes::SceneId);
  void moveScene(chopfractal::scenes::SceneId, int delta);
  void removeScene(chopfractal::scenes::SceneId);
  void clearScenes();  // asks first
  void editScene(chopfractal::scenes::SceneId, const std::function<void(chopfractal::scenes::Scene&)>&);
  void setSceneWrap(bool);
  void setRecordScenes(bool);
  bool playScenes(chopfractal::scenes::SceneId first);
  void stopScenes();
  // Folder chooser, then the export; a name clash asks before overwriting. `wav` adds scenes.wav.
  void exportScenes(bool wav);
  chopfractal::Status exportScenesTo(const std::string& directory, bool wav, bool overwrite);  // public so tests can skip the dialog
  bool beginMorph(double t);  // false when refused (the message is in the status line)
  bool setMorph(double t);
  void cancelMorph();
  void commitMorph();

  // ---------------------------------------------------------------------------------------------
  // Outputs and stems routing (F11 section 12)
  // ---------------------------------------------------------------------------------------------
  struct OutputsState {
    chopfractal::ui::OutputsView view;
    std::vector<std::uint64_t> chopIds;  // per chop row, snapshot order
  };
  OutputsState outputsState();
  void routeMain();
  void routeByRole();
  void routeByChop();
  void setRoleBus(const std::string& role, int bus);
  void setChopBus(std::uint64_t chopId, int bus);  // bus -1: back to the role default

  // ---------------------------------------------------------------------------------------------
  // Arrangement (F18 section 12)
  // ---------------------------------------------------------------------------------------------
  struct ArrangementState {
    chopfractal::composition::Arrangement arrangement;
    std::vector<chopfractal::composition::ArrangementRow> rows;
    int sceneCount = 0;
    std::vector<std::pair<std::uint32_t, int>> scenes;  // id and pattern bars, list order
    bool hasSource = false;
    double bpm = 120.0;
    bool running = false;
    chopfractal::composition::ArrangementProgress progress;
  };
  ArrangementState arrangementState();
  chopfractal::ui::ArrangementView arrangementView(const ArrangementState&, int selectedRow);
  void editArrangement(const std::function<void(chopfractal::composition::Arrangement&)>&);
  void fillArrangementFromChain();
  void autoFillArrangement();
  void startArrangementExport(const std::string& name);
  void cancelArrangementExport();
  // Reads a finished job (status line, release); call from the background tick. True when a job just ended.
  bool pollArrangementJob();
  // Opens a drawer by id (the shell sets it); the Arrange... button of the Scenes drawer uses it.
  std::function<void(const juce::String&)> openDrawer;
  std::function<juce::File()> exportFolder;  // the folder the arrangement files go to; tests redirect it
  std::string projectName();

  EditorModel& model() { return model_; }
  EditorCommands& commands() { return cmd_; }
  ChopFractalProcessor& processor() { return proc_; }

 private:
  ChopFractalProcessor& proc_;
  EditorModel& model_;
  EditorCommands& cmd_;
  StretchRunner stretch_;
  std::unique_ptr<juce::FileChooser> chooser_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};
