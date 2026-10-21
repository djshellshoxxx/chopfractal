#pragma once
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
  void updateCaptureControls();
  void addSlider(ParamSlider&, const char* paramId, const juce::String& title);
  void addToggle(ParamToggle&, const char* paramId, const juce::String& title);

  Attachment& apvts_;
  juce::ToggleButton editMode_{"Move / resize hits"};
  juce::TextButton play_{"Play"}, stop_{"Stop"}, followHost_{"Follow DAW"};
  // Input capture: Arm, then Stop (or let a bar length finish), then Keep to load it as the source.
  juce::TextButton recArm_{"Rec Arm"}, recStop_{"Rec Stop"}, recKeep_{"Rec Keep"};
  juce::ComboBox capLen_;
  juce::Label transportHint_{{}, "Load Loop > Detect Chops > Generate > Play"};
  juce::TextButton load_{"Load Loop"}, detect_{"Detect Chops"}, generate_{"Generate"}, mutate_{"Mutate"}, undo_{"Undo"}, redo_{"Redo"}, zoom_{"Zoom In"},
      collapse_{"Collapse"}, lock_{"Lock Bar"}, dice_{"New Seed"};
  juce::ToggleButton embed_{"Embed source in project"};
  juce::Label seedLabel_{{}, "Seed"};
  juce::TextEditor seed_;
  ParamSlider density_, swing_, variation_, dry_, gain_;
  juce::ComboBox bars_;
  juce::Label barsLabel_{{}, "Length (bars)"};
  std::unique_ptr<Attachment::ComboBoxAttachment> barsAttachment_;
  ParamToggle enable_, reverse_, pitch_, retrigger_, filter_, glide_, crunch_;
  ParamSlider fxIntensity_, chaos_;
};
