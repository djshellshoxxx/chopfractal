#pragma once
// Output meter for the status bar (F05): two bars from -60 to +6 dB with a tick at 0 dB, a peak-hold line and a latched
// CLIP chip that stays lit until it is clicked. A 30 Hz timer reads the renderer's lock-free meter and repaints only when
// something visible moved, so an idle plugin repaints nothing.
#include <chopfractal/plugin_ui_adapter/status_text.hpp>

#include "EditorModel.h"

class MeterStrip : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
 public:
  explicit MeterStrip(EditorModel&);
  ~MeterStrip() override;
  void paint(juce::Graphics&) override;
  void resized() override;
  void setScale(float s) { scale_ = s; resized(); repaint(); }

  bool clipLatched() const { return state_.clipLatched; }
  void clearClip();
  int voices() const { return voices_; }
  int repaintCount() const { return repaints_; }  // tests: stays unchanged while the output is silent
  const chopfractal::ui::MeterState& state() const { return state_; }
  juce::Button& clipChip() { return chip_; }
  void tickOnce(double dtSeconds);  // the timer body, public so tests can step it without waiting

 private:
  class Chip : public juce::Button {
   public:
    Chip() : juce::Button("Clear clip indicator") {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
    bool latched = false;
  };
  void timerCallback() override;
  void refreshAccessibility();

  EditorModel& model_;
  chopfractal::ui::MeterState state_;
  Chip chip_;
  juce::Rectangle<int> barsArea_;
  float scale_ = 1.0f;
  int voices_ = 0, repaints_ = 0;
  double lastTick_ = 0.0, lastDescribe_ = 0.0;
  juce::String lastTooltip_;
  bool announcedClip_ = false;
};
