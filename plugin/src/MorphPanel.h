#pragma once
// A/B morph controls: Morph toggle, Morph amount slider, Commit Morph, Cancel Morph. Pure view over
// ProjectSession::beginMorph / setMorph / commitMorph / cancelMorph; the slider is coalesced to one session update
// per 50 ms (the final value is always applied).
#include <juce_gui_basics/juce_gui_basics.h>

class ChopFractalProcessor;

class MorphPanel : public juce::Component, private juce::Timer {
 public:
  explicit MorphPanel(ChopFractalProcessor& processor);
  ~MorphPanel() override { stopTimer(); }
  void resized() override;
  bool previewing() const { return previewing_; }
  int appliedUpdates() const { return applied_; }  // test hook: session updates caused by the slider

 private:
  void timerCallback() override;
  void toggled();
  void applyPending();
  void commit();
  void cancel();
  void sync();
  static juce::String amountText(double v);

  ChopFractalProcessor& proc_;
  juce::ToggleButton morph_{"Morph"};
  juce::Slider amount_;
  juce::TextButton commit_{"Commit Morph"}, cancel_{"Cancel Morph"};
  juce::Label ends_;
  bool previewing_ = false;
  bool dirty_ = false;
  bool syncing_ = false;
  int applied_ = 0;
};
