#pragma once
// Header strip: title, transport, tempo, undo / redo, the current preset, drawer toggles, theme and Simple / Advanced.
#include <functional>

#include "EditorPanels.h"

class TopBar : public EditorPanelBase {
 public:
  TopBar(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;

  void setScale(float s) { scale_ = s; resized(); repaint(); }
  void setPresetName(const juce::String&);
  void setAdvanced(bool);
  void syncTransport();  // called from the editor's tick: Play shows its on state while the pattern plays
  // Drawer toggle buttons sit between the preset name and the theme button; the shell adds one per registered drawer.
  void addDrawerButton(const juce::String& id, const juce::String& label, std::function<void(const juce::String&)> onClick);
  void setDrawerButtonOn(const juce::String& openId);
  // Drawers that would not fit as buttons live in the Panels menu; the button appears with the first entry.
  void addMenuEntry(const juce::String& id, const juce::String& title);
  const std::vector<std::pair<juce::String, juce::String>>& menuEntries() const { return menuEntries_; }
  void showPanelsMenu();
  std::function<void(const juce::String&)> onMenuChoice;

  std::function<void()> onTheme, onSettings, onMode, onPresetClick;

 private:
  struct DrawerButton {
    juce::String id;
    std::unique_ptr<juce::TextButton> button;
  };
  void modelChanged(Change) override {}
  void lookAndFeelChanged() override;

  float scale_ = 1.0f;
  juce::TextButton play_{"Play"}, stop_{"Stop"}, undo_{"Undo"}, redo_{"Redo"}, theme_{"Night"}, mode_{"Simple"};
  juce::ShapeButton settings_{"Settings", juce::Colours::transparentBlack, juce::Colours::transparentBlack, juce::Colours::transparentBlack};
  std::vector<DrawerButton> drawerButtons_;
  juce::TextButton panels_{"Panels"};
  std::vector<std::pair<juce::String, juce::String>> menuEntries_;
  juce::String openId_;
  juce::String presetName_;
  juce::Rectangle<int> titleArea_, bpmArea_, presetArea_;
};
