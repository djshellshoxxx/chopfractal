#include "EditorKeys.h"

std::optional<EditorAction> mapKey(const juce::KeyPress& k, bool canvasFocus) {
  const auto mods = k.getModifiers();
  const bool cmd = mods.isCommandDown() || mods.isCtrlDown();
  const bool shift = mods.isShiftDown();
  const bool alt = mods.isAltDown();
  if (alt) return std::nullopt;
  const int code = k.getKeyCode();
  // Hosts report a letter either as its key code (upper or lower case) or only as the text character.
  juce::juce_wchar ch = (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z') ? juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(code))
                                                                                    : juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
  if (ch == 0) ch = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
  const bool letterZ = ch == 'z';
  if (cmd) {
    if (ch == 'k') return shift ? std::nullopt : std::optional<EditorAction>(EditorAction::OpenPalette);
    if (letterZ) return shift ? EditorAction::Redo : EditorAction::Undo;
    if (ch == 'y') return shift ? std::nullopt : std::optional<EditorAction>(EditorAction::Redo);
    if (ch == 'd') return shift ? std::nullopt : std::optional<EditorAction>(EditorAction::Duplicate);
    return std::nullopt;
  }
  if (ch == 'g' && !shift) return EditorAction::Generate;
  if (ch == 'm') return shift ? EditorAction::Mutate : EditorAction::Mute;
  if (ch == 'd' && !shift) return EditorAction::Duplicate;
  if (letterZ) return shift ? EditorAction::Collapse : EditorAction::ZoomIn;
  if (ch == 'l' && !shift) return EditorAction::ToggleLock;
  if (!canvasFocus) return std::nullopt;
  if (code == juce::KeyPress::homeKey) return EditorAction::WaveFit;
  {
    const juce::juce_wchar t = k.getTextCharacter();
    if (t == '+' || t == '=' || code == '+' || code == '=') return EditorAction::WaveZoomIn;
    if (t == '-' || code == '-') return EditorAction::WaveZoomOut;
  }
  if (code == juce::KeyPress::leftKey) return EditorAction::PrevHit;
  if (code == juce::KeyPress::rightKey) return EditorAction::NextHit;
  if (code == juce::KeyPress::upKey) return EditorAction::LaneUp;
  if (code == juce::KeyPress::downKey) return EditorAction::LaneDown;
  if (code == juce::KeyPress::returnKey) return EditorAction::Audition;
  if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) return EditorAction::Delete;
  if (code == juce::KeyPress::escapeKey) return EditorAction::Escape;
  if (code == juce::KeyPress::spaceKey) return EditorAction::SpaceHint;
  return std::nullopt;
}

juce::String shortcutText(EditorAction a) {
#if JUCE_MAC
  const juce::String cmd = "Cmd+";
#else
  const juce::String cmd = "Ctrl+";
#endif
  switch (a) {
    case EditorAction::Generate: return "G";
    case EditorAction::Mutate: return "Shift+M";
    case EditorAction::Mute: return "M";
    case EditorAction::ZoomIn: return "Z";
    case EditorAction::Collapse: return "Shift+Z";
    case EditorAction::ToggleLock: return "L";
    case EditorAction::Undo: return cmd + "Z";
    case EditorAction::Redo: return cmd + "Y";
    case EditorAction::PrevHit: return "Left";
    case EditorAction::NextHit: return "Right";
    case EditorAction::LaneUp: return "Up";
    case EditorAction::LaneDown: return "Down";
    case EditorAction::Audition: return "Enter";
    case EditorAction::Delete: return "Delete";
    case EditorAction::Duplicate: return "D";
    case EditorAction::Escape: return "Esc";
    case EditorAction::SpaceHint: return "Space";
    case EditorAction::OpenPalette: return cmd + "K";
    case EditorAction::WaveZoomIn: return "+";
    case EditorAction::WaveZoomOut: return "-";
    case EditorAction::WaveFit: return "Home";
  }
  return {};
}
