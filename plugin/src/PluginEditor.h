#pragma once
// Editor: draws the headless view models from plugin_ui_adapter and routes user actions to the composition
// root. It contains no pattern, chop, or history logic of its own.
#include <juce_audio_utils/juce_audio_utils.h>

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
  ParamToggle enable_, reverse_, pitch_, retrigger_;
  std::unique_ptr<juce::FileChooser> chooser_;

  // View state, rebuilt only when the session's published state changes.
  juce::String signature_;
  chopfractal::ui::PeakBins peaks_;
  std::int64_t sourceFrames_ = 0;
  std::vector<chopfractal::ui::MarkerView> markers_;
  std::vector<chopfractal::ChopInfo> chops_;
  chopfractal::ui::PatternView pattern_;
  std::vector<chopfractal::ui::TreeNodeView> tree_;
  chopfractal::EventId selected_;
  juce::String status_;

  juce::Rectangle<int> waveArea_, patternArea_, historyArea_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalEditor)
};
