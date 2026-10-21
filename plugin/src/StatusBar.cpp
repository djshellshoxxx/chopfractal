#include "StatusBar.h"

StatusBar::StatusBar(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "StatusBar") {
  messages_.setTitle("Status messages");
  messages_.setMultiLine(true, true);
  messages_.setReadOnly(true);
  messages_.setScrollbarsShown(true);
  messages_.setCaretVisible(false);
  messages_.setPopupMenuEnabled(true);
  messages_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20252d));
  messages_.setColour(juce::TextEditor::textColourId, juce::Colours::white);
  messages_.setTooltip("Full status message. Scroll to read long messages; select text to copy it.");
  addAndMakeVisible(messages_);
}

void StatusBar::resized() { messages_.setBounds(getLocalBounds()); }

void StatusBar::modelChanged(Change c) {
  if (c != Change::Status) return;
  // Messages arrive from other threads' work too (file loading, Evolve, restore): show the line when it changes.
  const juce::String line = model_.statusLine();
  if (line == lastLine_) return;
  lastLine_ = line;
  messages_.setText(line, false);
  messages_.moveCaretToTop(false);
}
