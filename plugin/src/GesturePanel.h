#pragma once
// Gestures drawer (F08): hold Stutter, Reverse or Tape Stop to play the loop live. A button acts while it is held (mouse or
// the Space and Return keys) and lets go when released, when the pointer leaves it, or when the drawer closes. MIDI CC 20, 21
// and 22 (value 64 or more is held) do the same. The rate and the two tape times are saved with the project.
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "PluginProcessor.h"

class GesturePanel : public juce::Component, private juce::Timer {
 public:
  explicit GesturePanel(ChopFractalProcessor& processor);
  ~GesturePanel() override;
  void resized() override;
  void paint(juce::Graphics&) override;
  void visibilityChanged() override;

  juce::Button& stutterButton() { return buttons_[0]; }
  juce::Button& reverseButton() { return buttons_[1]; }
  juce::Button& tapeStopButton() { return buttons_[2]; }
  juce::ComboBox& rateBox() { return rate_; }
  juce::Slider& tapeStopSlider() { return stopMs_; }
  juce::Slider& spinUpSlider() { return spinMs_; }

 private:
  void timerCallback() override;
  void held(int index, bool on);

  ChopFractalProcessor& proc_;
  std::array<juce::TextButton, 3> buttons_;
  std::array<bool, 3> down_{};
  juce::Label rateLabel_, stopLabel_, spinLabel_, hint_;
  juce::ComboBox rate_;
  juce::Slider stopMs_, spinMs_;
};
