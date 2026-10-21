#pragma once
// Editor: draws the headless view models from plugin_ui_adapter and routes user actions to the composition
// root. It contains no pattern, chop, or history logic of its own.
#include <juce_audio_utils/juce_audio_utils.h>

#include <chopfractal/plugin_ui_adapter/orbit.hpp>
#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <functional>
#include <memory>

#include "DragOut.h"
#include "PluginProcessor.h"
#include "PresetManager.h"
#include <chopfractal/plugin_ui_adapter/hit_edit.hpp>

class ChopFractalEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public juce::TooltipClient, private juce::Timer {
 public:
  explicit ChopFractalEditor(ChopFractalProcessor& processor);
  ~ChopFractalEditor() override;

  juce::String getTooltip() override;
  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void filesDropped(const juce::StringArray& files, int x, int y) override;
  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  bool keyPressed(const juce::KeyPress&) override;

  // Public so tests can drive them without modal dialogs.
  void doReroll();
  void applyPreset(const chopfractal::presets::Preset& p);
  chopfractal::presets::Preset currentPreset(const juce::String& name) const;
  void savePresetNamed(const juce::String& name, bool overwrite);
  void deleteSelectedPreset();
  void reloadPresets();
  juce::File makeDragFile(bool wav);
  // Read-only views for tests and accessibility tooling.
  juce::Rectangle<int> patternBounds() const { return patternArea_; }
  const std::vector<chopfractal::ui::EventRect>& hitRects() const { return pattern_.rects; }
  int laneCount() const { return static_cast<int>(chops_.size()); }
  chopfractal::EventId selectedHit() const { return selected_; }
  const std::vector<chopfractal::EventId>& selectedHits() const { return selectedHits_; }
  const std::vector<chopfractal::ChopId>& selectedChops() const { return selectedChops_; }
  juce::Rectangle<int> waveformBounds() const { return waveArea_; }

 private:
  using Attachment = juce::AudioProcessorValueTreeState;
  struct ParamSlider {
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<Attachment::SliderAttachment> attachment;
  };
  struct ParamToggle {
    juce::ToggleButton button;
    std::unique_ptr<Attachment::ButtonAttachment> attachment;
  };

  void timerCallback() override { refresh(false); }
  void refresh(bool force);
  void run(const std::function<chopfractal::Status(chopfractal::composition::ProjectSession&)>& action);
  void addSlider(ParamSlider& s, const char* paramId, const juce::String& title);
  void addToggle(ParamToggle& t, const char* paramId, const juce::String& title);
  void chooseFile();
  std::uint64_t currentSeed() const;
  void setSeed(std::uint64_t seed);
  void doGenerate();
  void doMutate();
  void doToggleLock();
  void doSmartSetup();
  void doFractal();
  void doEvolve();
  void doExportWav();
  void doExportKit();
  int selectedBar() const;
  void askSavePreset();
  juce::Point<int> treePos(const chopfractal::ui::TreeNodeView& n) const;
  void nudgeSelected(int cells, bool resize);
  void editSelected(const std::function<chopfractal::Result<chopfractal::pattern::Pattern>(const chopfractal::pattern::Pattern&, const chopfractal::ChopSnapshot&)>& fn);

  void updateSectionSelection(juce::Point<int> point);
  ChopFractalProcessor& proc_;
  juce::ToggleButton editMode_{"Move / resize hits"};
  std::vector<chopfractal::EventId> selectedHits_;
  std::vector<chopfractal::ChopId> selectedChops_;
  bool selectingSections_ = false, selectingWave_ = false;
  juce::Point<int> selectionStart_, selectionEnd_;

  juce::TextButton play_{"Play"}, stop_{"Stop"}, followHost_{"Follow DAW"};
  juce::TextEditor statusMessages_;
  juce::Label transportHint_{{}, "Load Loop > Detect Chops > Generate > Play"};
  juce::TextButton load_{"Load Loop"}, detect_{"Detect Chops"}, generate_{"Generate"}, mutate_{"Mutate"}, undo_{"Undo"}, redo_{"Redo"},
      zoom_{"Zoom In"}, collapse_{"Collapse"}, lock_{"Lock Bar"}, dice_{"New Seed"};
  juce::ToggleButton embed_{"Embed source in project"};
  juce::Label seedLabel_{{}, "Seed"};
  juce::TextEditor seed_;
  ParamSlider density_, swing_, variation_, dry_, gain_;
  juce::ComboBox bars_;
  juce::Label barsLabel_{{}, "Length (bars)"};
  std::unique_ptr<Attachment::ComboBoxAttachment> barsAttachment_;
  ParamToggle enable_, reverse_, pitch_, retrigger_, filter_, glide_, crunch_;
  ParamSlider fxIntensity_, chaos_;
  juce::TextButton reroll_{"Reroll"}, savePreset_{"Save Preset"}, deletePreset_{"Delete"};
  juce::ComboBox preset_;
  std::vector<PresetManager::Entry> presets_;
  DragHandle dragMidi_{"Drag MIDI", [this] { return makeDragFile(false); }};
  DragHandle dragWav_{"Drag WAV", [this] { return makeDragFile(true); }};
  double fractalMutation_ = -1.0;  // < 0: derive from the Variation control; set by a preset
  double fractalAccent_ = 0.85;
  // Direct hit editing (pattern grid): one gesture = one undo step, applied on mouse-up.
  enum class DragMode { None, Move, Resize };
  DragMode dragMode_ = DragMode::None;
  chopfractal::EventId dragId_;
  juce::Point<int> dragStart_, dragNow_;
  int dragLane_ = 0;
  bool dragCopy_ = false;
  std::int64_t barTicks_ = 0, gridTicks_ = 0;
  int barCount_ = 0;
  int treeCols_ = 0, treeRows_ = 0;
  juce::String lastStatusKey_;
  juce::TooltipWindow tooltip_{this};
  juce::Component* tooltipRoot_ = nullptr;  // root changes when the standalone wrapper attaches the editor

  // Feature controls (not host parameters: they act on the project).
  juce::TextButton locate_{"Locate Source"}, smart_{"Smart Setup"}, acceptRoles_{"Accept Roles"}, useTempo_{"Use Tempo"}, fractal_{"Fractal"}, keep_{"Keep"},
      exportWav_{"Export WAV"}, exportKit_{"Export Kit"};
  juce::TextButton storeA_{"Store A"}, recallA_{"Recall A"}, storeB_{"Store B"}, recallB_{"Recall B"}, setRole_{"Set Role"};
  juce::ComboBox role_;
  void historyMenu(chopfractal::history::NodeId id);
  juce::ToggleButton evolve_{"Evolve"};
  juce::Label motifLabel_{{}, "Motif"}, depthLabel_{{}, "Depth"}, everyLabel_{{}, "Every"}, fmtLabel_{{}, "Format"}, loopsLabel_{{}, "Loops"};
  juce::TextEditor motif_;
  juce::ComboBox detectMode_;
  const void* peaksSource_ = nullptr;
  int peaksWidth_ = 0;
  bool lastPlaying_ = false;
  chopfractal::evolve::Settings pushedEvolve_;  // the settings the editor last sent; a different session value (a restored project) is shown
  bool sourceMissing_ = false;
  juce::ComboBox depth_, every_, exportFormat_, exportRate_, exportLoops_;
  juce::Slider evolveAmount_;
  chopfractal::composition::SetupSuggestion setup_;
  bool haveSetup_ = false;
  std::unique_ptr<juce::FileChooser> chooser_;

  // View state, rebuilt only when the session's published state changes.
  juce::String signature_;
  chopfractal::ui::PeakBins peaks_;
  std::int64_t sourceFrames_ = 0;
  std::vector<chopfractal::ui::MarkerView> markers_;
  std::vector<chopfractal::ChopInfo> chops_;
  chopfractal::ui::PatternView pattern_;
  chopfractal::ui::OrbitView orbit_;
  std::vector<chopfractal::ui::TreeNodeView> tree_;
  chopfractal::EventId selected_;
  juce::String status_;

  juce::Rectangle<int> waveArea_, patternArea_, historyArea_, orbitArea_, versionArea_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalEditor)
};
