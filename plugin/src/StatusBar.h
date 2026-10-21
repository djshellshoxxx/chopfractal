#pragma once
// Scrollable, read-only status text (full status and error messages, plus the host-fallback hints).
#include "EditorPanels.h"

class StatusBar : public EditorPanelBase {
 public:
  StatusBar(EditorModel&, EditorCommands&);
  void resized() override;
  void paint(juce::Graphics&) override;
  void setScale(float s) { scale_ = s; resized(); repaint(); }
  void refreshReadouts();  // the editor's tick: seed, tempo source and tempo, repainted only when the text changes

 private:
  void modelChanged(Change) override;

  juce::TextEditor messages_;
  juce::TextButton cancelExport_{"Cancel Export"};  // visible only while an export runs
  void lookAndFeelChanged() override;

  juce::String lastLine_, lastReadout_;
  float scale_ = 1.0f;
  juce::Rectangle<int> infoArea_;
};
