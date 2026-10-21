#pragma once
// The Merge dialog of Detect Chops: shows how many chops the detection proposes (the proposals are drawn as dashed lines on
// the waveform), lets you replace your markers or merge with them, and applies or cancels. Closing it any other way cancels.
#include "EditorPanels.h"

class DetectDialog : public juce::Component, private EditorModel::Listener {
 public:
  DetectDialog(EditorModel&, EditorCommands&);
  ~DetectDialog() override;
  void resized() override;
  void paint(juce::Graphics&) override;
  // Opens the dialog in a call-out pointing at `target`, inside `parent` (the editor).
  static void launch(juce::Component& parent, juce::Rectangle<int> target, EditorModel&, EditorCommands&);

  static constexpr int kWidth = 300, kHeight = 150;

 private:
  void modelChanged(EditorModel::Change) override;
  void refreshText();
  void close();

  EditorModel& model_;
  EditorCommands& cmd_;
  juce::ToggleButton replace_{"Replace my markers"}, merge_{"Merge with my markers"};
  juce::TextButton apply_{"Apply detection"}, cancel_{"Cancel detection"};
  juce::Label info_;
};
