#pragma once
// The keyboard shortcut list (F04), shown in a call-out. Rows come from the same table the key map and the palette use.
#include <juce_gui_basics/juce_gui_basics.h>

#include <utility>
#include <vector>

#include "CdlLookAndFeel.h"
#include "EditorKeys.h"

class ShortcutsPanel : public juce::Component {
 public:
  ShortcutsPanel() {
    setTitle("Keyboard shortcuts");
    setDescription(text());
    setSize(400, static_cast<int>(rows().size()) * 20 + 40);
  }
  static const std::vector<std::pair<EditorAction, const char*>>& rows() {
    static const std::vector<std::pair<EditorAction, const char*>> r = {
        {EditorAction::OpenPalette, "Command palette"}, {EditorAction::Generate, "Generate new pattern"}, {EditorAction::Mutate, "Mutate pattern"},
        {EditorAction::Undo, "Undo"}, {EditorAction::Redo, "Redo"}, {EditorAction::PrevHit, "Previous hit (nudge in edit mode)"},
        {EditorAction::NextHit, "Next hit (nudge in edit mode)"}, {EditorAction::LaneUp, "Hit in the lane above"},
        {EditorAction::LaneDown, "Hit in the lane below"}, {EditorAction::Audition, "Audition the selected hit"}, {EditorAction::Mute, "Mute or unmute selected hit"},
        {EditorAction::Duplicate, "Duplicate selected hit"}, {EditorAction::Delete, "Delete selected hit"}, {EditorAction::ZoomIn, "Zoom into selected hit"},
        {EditorAction::Collapse, "Collapse selected hit"}, {EditorAction::ToggleLock, "Lock or unlock bar"}, {EditorAction::Escape, "Clear selection"},
        {EditorAction::SpaceHint, "Play or stop (standalone)"}, {EditorAction::WaveZoomIn, "Zoom the waveform in (waveform focused)"},
        {EditorAction::WaveZoomOut, "Zoom the waveform out"}, {EditorAction::WaveFit, "Show the whole sample"}};
    return r;
  }
  static juce::String text() {
    juce::String t;
    for (const auto& [a, name] : rows()) t += shortcutText(a) + ": " + name + ". ";
    return t;
  }
  void paint(juce::Graphics& g) override {
    const auto& p = cdl::pal();
    g.fillAll(p.panel);
    g.setFont(cdl::font(cdl::FontRole::Label, 1.0f));
    g.setColour(p.ink);
    g.drawText("KEYBOARD SHORTCUTS", 12, 6, getWidth() - 24, 22, juce::Justification::centredLeft, false);
    int y = 32;
    for (const auto& [a, name] : rows()) {
      g.setFont(cdl::font(cdl::FontRole::Value, 1.0f));
      g.setColour(p.sprayHi);
      g.drawText(shortcutText(a), 12, y, 70, 20, juce::Justification::centredLeft, false);
      g.setFont(cdl::font(cdl::FontRole::Hint, 1.0f));
      g.setColour(p.ink);
      g.drawText(name, 86, y, getWidth() - 96, 20, juce::Justification::centredLeft, true);
      y += 20;
    }
  }
};
