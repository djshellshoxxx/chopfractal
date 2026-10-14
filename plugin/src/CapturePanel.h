#pragma once
// Compact input-capture controls (Arm / Capture / End Capture / Keep Capture / Discard / Undo Keep). Pure view:
// every action goes through CaptureController, and the state shown is polled from the engine's atomics.
#include <juce_gui_basics/juce_gui_basics.h>

class ChopFractalProcessor;

// 120x8 level bar; the clip flag latches until the next Arm.
class InputMeter : public juce::Component, public juce::SettableTooltipClient {
 public:
  void set(float level, bool clipped) {
    level_ = level;
    clipped_ = clipped;
    repaint();
  }
  float level() const { return level_; }
  bool clipped() const { return clipped_; }
  void paint(juce::Graphics& g) override;

 private:
  float level_ = 0.f;
  bool clipped_ = false;
};

class CapturePanel : public juce::Component, private juce::Timer {
 public:
  explicit CapturePanel(ChopFractalProcessor& processor);
  ~CapturePanel() override { stopTimer(); }
  void resized() override;
  const InputMeter& meter() const { return meter_; }
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
  InputMeter meter_;
  juce::Label state_;
  float shown_ = 0.f;
  juce::String lastEnd_;
};
