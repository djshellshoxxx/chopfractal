#pragma once
// Scrollable, read-only status text (full status and error messages, plus the host-fallback hints).
#include "EditorPanels.h"

class StatusBar : public EditorPanelBase {
 public:
  StatusBar(EditorModel&, EditorCommands&);
  void resized() override;

 private:
  void modelChanged(Change) override;

  juce::TextEditor messages_;
  juce::String lastLine_;
};
