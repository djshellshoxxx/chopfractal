#pragma once
// The editor's keyboard map (F04). mapKey is a pure function of the key and where the focus is, so it is unit tested without a
// window; ChopFractalEditor::keyPressed dispatches the result to EditorCommands and the model. Keys that are not listed, or that
// carry other modifiers, are not ours: the host keeps its shortcuts.
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

enum class EditorAction {
  Generate, Mutate, Mute, ZoomIn, Collapse, ToggleLock, Undo, Redo,
  PrevHit, NextHit, LaneUp, LaneDown, Audition, Delete, Duplicate, Escape, SpaceHint,
  OpenPalette
};

// `canvasFocus`: the focus is on the editor or one of the four canvases. Arrows, Enter, Delete, Backspace and Escape are
// claimed only there (a focused button or slider keeps them); letter shortcuts and Cmd+K work wherever the focus is.
std::optional<EditorAction> mapKey(const juce::KeyPress&, bool canvasFocus);

// "G", "Shift+M", "Ctrl+Z" ... with Ctrl shown as Cmd on macOS. Empty when the action has no key.
juce::String shortcutText(EditorAction);
