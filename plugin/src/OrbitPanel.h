#pragma once
// Orbit View: one ring per chop with the playhead sweeping clockwise. Clicking a hit selects it.
#include "EditorPanels.h"

class OrbitPanel : public EditorPanelBase {
 public:
  OrbitPanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;

 private:
  void modelChanged(Change) override;
};
