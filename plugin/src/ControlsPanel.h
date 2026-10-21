#pragma once
#include "ParamTip.h"
// Transport, the main action buttons, the seed, and the host-automatable parameter controls (their attachments live
// here). Widgets are declared before the attachments that bind them, so the attachments are destroyed first.
#include <memory>

#include "EditorPanels.h"

class ControlsPanel : public EditorPanelBase {
 public:
  ControlsPanel(EditorModel&, EditorCommands&, juce::AudioProcessorValueTreeState&);
  void resized() override;

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

  void modelChanged(Change) override;
  void addSlider(ParamSlider&, const char* paramId, const juce::String& title);
  void addToggle(ParamToggle&, const char* paramId, const juce::String& title);

  Attachment& apvts_;
  ParamTip tips_;
  juce::TextButton followHost_{"Follow DAW"};
  juce::Label transportHint_{{}, "Load Loop > Detect Chops > Generate > Play"};
  juce::TextButton mutate_{"Mutate"}, dice_{"New Seed"};
  juce::ToggleButton embed_{"Embed source in project"};
  juce::Label seedLabel_{{}, "Seed"};
  juce::TextEditor seed_;
  ParamSlider density_, swing_, variation_, dry_, gain_;
  ParamToggle enable_, reverse_, pitch_, retrigger_, filter_, glide_, crunch_;
  ParamSlider fxIntensity_, chaos_;
};
