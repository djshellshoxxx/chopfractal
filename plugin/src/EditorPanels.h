#pragma once
// Common base of the editor panels: a component that listens to the EditorModel and calls EditorCommands. Panels
// never touch the processor or the session; only the model and the commands do.
#include <juce_audio_utils/juce_audio_utils.h>

#include "EditorCommands.h"
#include "EditorModel.h"

class EditorPanelBase : public juce::Component, protected EditorModel::Listener {
 public:
  ~EditorPanelBase() override { model_.removeListener(this); }

 protected:
  using Change = EditorModel::Change;
  EditorPanelBase(EditorModel& model, EditorCommands& commands, const char* componentId) : model_(model), cmd_(commands) {
    setComponentID(componentId);
    model_.addListener(this);
  }
  void modelChanged(Change) override {}
  // The editor takes the keyboard focus (its key handler edits the selection), not the panel that was clicked.
  void grabEditorFocus() {
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>()) editor->grabKeyboardFocus();
  }

  EditorModel& model_;
  EditorCommands& cmd_;
};
