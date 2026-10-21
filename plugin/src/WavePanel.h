#pragma once
// Source waveform with chop markers. Drag selects source sections; Shift-click adds a marker, Alt-click removes the
// nearest one; a plain click or a double click auditions the chop under the cursor.
#include "EditorPanels.h"

class WavePanel : public EditorPanelBase {
 public:
  WavePanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;

 private:
  void modelChanged(Change) override;
  void updateSelection(juce::Point<int> point);

  bool selecting_ = false;
  juce::Point<int> start_, end_;
};
