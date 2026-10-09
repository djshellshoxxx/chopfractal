#pragma once
// Editor: draws the headless view models from plugin_ui_adapter and routes user actions to the composition
// root. It contains no pattern, chop, or history logic of its own.
#include <juce_audio_utils/juce_audio_utils.h>

#include <chopfractal/plugin_ui_adapter/orbit.hpp>
#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <functional>
#include <memory>

#include "PluginProcessor.h"

class ChopFractalEditor : public juce::AudioProcessorEditor, private juce::Timer {
 public:
  explicit ChopFractalEditor(ChopFractalProcessor& processor);
  ~ChopFractalEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;

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

  ChopFractalProcessor& proc_;

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
  ParamSlider fxIntensity_;

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

  juce::Rectangle<int> waveArea_, patternArea_, historyArea_, orbitArea_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalEditor)
};
