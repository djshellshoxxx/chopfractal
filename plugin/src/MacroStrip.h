#pragma once
#include "ParamTip.h"
// Bottom strip: the Generate cluster (Generate, Reroll, Evolve, break Profile) and the macro knobs. The knobs are data
// driven from the host parameter manifest (range, default, unit, choices come from modules/plugin_host_adapter); the
// table in MacroStrip.cpp only adds the label and the one-clause tooltip. Level match and Safety limiter sit next to Output.
#include <functional>
#include <memory>
#include <vector>

#include "EditorPanels.h"
#include "ShellLayout.h"

namespace chopfractal::host {
struct ParamDef;
}

class MacroKnob : public juce::Component {
 public:
  MacroKnob(juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& label, const juce::String& tip);
  void paint(juce::Graphics&) override;
  void resized() override;
  void setScale(float s) { scale_ = s; resized(); repaint(); }
  void setDriftFromParameter();  // marks the current value with the rain drift tick (the value the preset or project opened with)

  juce::Slider& slider() { return slider_; }
  // Stutter macro only: raising the amount above zero switches Allow Stutter on (a person's change, see the constructor).
  void enableStutterIfRaised();
  const chopfractal::host::ParamDef* def() const { return def_; }
  juce::String valueText() const;                   // number and unit as shown below the knob
  static juce::String formatNumber(const chopfractal::host::ParamDef&, double plain);
  static juce::String unitOf(const chopfractal::host::ParamDef&);

 private:
  juce::AudioProcessorValueTreeState& apvts_;
  const chopfractal::host::ParamDef* def_ = nullptr;
  juce::String label_;
  juce::Slider slider_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
  ParamTip tip_;  // help line plus host::describeParam for the current value
  float scale_ = 1.0f;
  bool ready_ = false;  // set once the attachment's first update is over
};

class MacroStrip : public EditorPanelBase {
 public:
  MacroStrip(EditorModel&, EditorCommands&, juce::AudioProcessorValueTreeState&);
  void paint(juce::Graphics&) override;
  void resized() override;
  void setScale(float s);
  void resetDriftTicks();  // after a preset is applied or a project restored
  void syncEvolve();       // the Evolve button mirrors the session
  void setEvolveState(bool on) { evolve_.setToggleState(on, juce::dontSendNotification); }

  std::function<void(bool)> onEvolve;  // the shell routes the toggle to the Evolve settings

  const std::vector<std::unique_ptr<MacroKnob>>& knobs() const { return knobs_; }

 private:
  void modelChanged(Change) override;

  juce::AudioProcessorValueTreeState& apvts_;
  ParamTip tips_;
  float scale_ = 1.0f;
  juce::TextButton generate_{"Generate"}, reroll_{"Reroll"};
  juce::ToggleButton evolve_{"Evolve"};
  juce::ComboBox profile_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> profileAttachment_;
  juce::ToggleButton levelMatch_{"Match"}, limiter_{"Limit"};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAttachment_, limiterAttachment_;
  std::vector<std::unique_ptr<MacroKnob>> knobs_;
  juce::Rectangle<int> clusterArea_, knobArea_;
};
