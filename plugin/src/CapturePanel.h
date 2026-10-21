#pragma once
// Capture drawer (F12 section 12): Arm, Capture Now, Stop Capture, Keep Capture, Discard Capture, Undo Keep, the bars / max
// length choice, the input level meter, the countdown and the progress bar. Every label, flag and tooltip comes from
// ui::buildCaptureView(); the buttons call PerformCommands.
#include <chopfractal/plugin_ui_adapter/capture_view.hpp>

#include "DrawerPanel.h"
#include "PerformCommands.h"

// Input level with a peak tick; "Clipped" is written out so the state never depends on colour alone.
class CaptureMeter : public juce::Component, public juce::SettableTooltipClient {
 public:
  CaptureMeter() { setComponentID("CaptureMeter"); }
  void set(float level, float peak, bool clipped, const juce::String& text) {
    if (level == level_ && peak == peak_ && clipped == clipped_ && text == text_) return;
    level_ = level; peak_ = peak; clipped_ = clipped; text_ = text;
    repaint();
  }
  void paint(juce::Graphics& g) override;
  float level() const { return level_; }
  float peak() const { return peak_; }
  bool clipped() const { return clipped_; }

 private:
  float level_ = 0.f, peak_ = 0.f;
  bool clipped_ = false;
  juce::String text_;
};

class CapturePanel : public DrawerPanel {
 public:
  CapturePanel(EditorModel&, EditorCommands&, PerformCommands&);
  const chopfractal::ui::CaptureView& view() const { return view_; }
  PerformCommands::CaptureChoice choice() const;
  const CaptureMeter& meter() const { return meter_; }

 private:
  int layoutBody(int width) override;
  void tick() override;

  PerformCommands& perform_;
  chopfractal::ui::CaptureView view_;
  juce::Label state_, length_, modeLabel_, barsLabel_, countdown_;
  juce::ComboBox mode_, bars_, maxLength_;
  juce::TextButton arm_{"Arm"}, now_{"Capture Now"}, stop_{"Stop Capture"}, keep_{"Keep Capture"}, discard_{"Discard Capture"}, undoKeep_{"Undo Keep"};
  juce::ToggleButton chopAfter_{"Chop after Keep"};
  CaptureMeter meter_;
  double progress_ = 0.0;
  juce::ProgressBar progressBar_{progress_};
};
