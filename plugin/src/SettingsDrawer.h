#pragma once
// Settings drawer content: theme choice and a way back to the default window size. Registered as the "settings" drawer.
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "CdlTheme.h"

class SettingsDrawer : public juce::Component {
 public:
  SettingsDrawer() {
    setComponentID("SettingsDrawer");
    themeLabel_.setText("Theme", juce::dontSendNotification);
    addAndMakeVisible(themeLabel_);
    theme_.addItem("Night", 1);
    theme_.addItem("Concrete", 2);
    theme_.setTitle("Theme");
    theme_.setTooltip("Night is the default, Concrete is the daylight theme");
    theme_.onChange = [this] {
      if (onTheme) onTheme(theme_.getSelectedId() == 2 ? cdl::ThemeId::Concrete : cdl::ThemeId::Night);
    };
    addAndMakeVisible(theme_);
    reset_.setTitle("Reset window size");
    reset_.setTooltip("Return the window to 1100 by 720");
    reset_.onClick = [this] {
      if (onResetSize) onResetSize();
    };
    addAndMakeVisible(reset_);
  }
  void showTheme(cdl::ThemeId t) { theme_.setSelectedId(t == cdl::ThemeId::Concrete ? 2 : 1, juce::dontSendNotification); }
  void resized() override {
    auto a = getLocalBounds().reduced(8);
    auto row = a.removeFromTop(28);
    themeLabel_.setBounds(row.removeFromLeft(70));
    theme_.setBounds(row);
    a.removeFromTop(12);
    reset_.setBounds(a.removeFromTop(28));
  }
  std::function<void(cdl::ThemeId)> onTheme;
  std::function<void()> onResetSize;

 private:
  juce::Label themeLabel_;
  juce::ComboBox theme_;
  juce::TextButton reset_{"Reset window size"};
};
