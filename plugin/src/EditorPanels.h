#pragma once
// Common base of the editor panels: a component that listens to the EditorModel and calls EditorCommands. Panels
// never touch the processor or the session; only the model and the commands do.
#include <juce_audio_utils/juce_audio_utils.h>

#include "EditorCommands.h"
#include "EditorModel.h"

class EditorPanelBase : public juce::Component, protected EditorModel::Listener {
 public:
  ~EditorPanelBase() override { model_.removeListener(this); }

 void setAccessibleInfo(const juce::String& title, const juce::String& description) {
    if (getTitle() != title) setTitle(title);
    if (getDescription() != description) setDescription(description);
  }  // panels name themselves for screen readers; a canvas also describes its content

 protected:
  using Change = EditorModel::Change;
  EditorPanelBase(EditorModel& model, EditorCommands& commands, const char* componentId) : model_(model), cmd_(commands) {
    setComponentID(componentId);
    model_.addListener(this);
  }
  void modelChanged(Change) override {}
  // A canvas (wantsKeyboardFocus) takes the focus itself and shows the focus ring; its unhandled keys bubble up to the editor's
  // key handler. Any other panel hands the focus to the editor.
  void grabEditorFocus() {
    if (getWantsKeyboardFocus()) {
      grabKeyboardFocus();
      return;
    }
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>()) editor->grabKeyboardFocus();
  }
  void focusGained(FocusChangeType) override { if (getWantsKeyboardFocus()) repaint(); }
  void focusLost(FocusChangeType) override { if (getWantsKeyboardFocus()) repaint(); }
  // F04: a 2 px outline inset 1 px marks the focused canvas.
  void paintOverChildren(juce::Graphics& g) override {
    if (!getWantsKeyboardFocus() || !hasKeyboardFocus(false)) return;
    g.setColour(juce::Colour(0xff7cf29a));
    g.drawRect(getLocalBounds().reduced(1), 2);
  }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
  }

  EditorModel& model_;
  EditorCommands& cmd_;
};
