#pragma once
// Compact input-capture controls (Arm / Capture / End Capture / Keep Capture / Discard / Undo Keep). Pure view:
// every action goes through CaptureController, and the state shown is polled from the engine's atomics.
#include <juce_gui_basics/juce_gui_basics.h>

class ChopFractalProcessor;

class CapturePanel : public juce::Component, private juce::Timer {
 public:
  explicit CapturePanel(ChopFractalProcessor& processor);
  ~CapturePanel() override { stopTimer(); }
  void resized() override;
  juce::String stateText() const { return state_.getText(); }

 private:
  void timerCallback() override { update(); }
  void update();
  void arm();
  void keep();

  ChopFractalProcessor& proc_;
  juce::ComboBox mode_, bars_;
  juce::TextButton arm_{"Arm"}, start_{"Capture"}, stop_{"End Capture"}, keep_{"Keep Capture"}, discard_{"Discard"}, undo_{"Undo Keep"};
  juce::ToggleButton chop_{"Chop after Keep"};
  juce::Label state_;
  juce::String lastEnd_;
};
