#pragma once
// Settings drawer (docs/specs/gui-redesign.md 7.8): theme, interface scale, reduced motion, the output defaults a new
// plugin instance starts with, window size and Copy diagnostics. Everything here is per user (ui.json), none of it is
// saved in a project. Registered as the "settings" drawer.
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "CdlTheme.h"

class SettingsDrawer : public juce::Component {
 public:
  SettingsDrawer();
  void showTheme(cdl::ThemeId);
  void showUserSettings(const cdl::UserSettings&);  // re-reads the choices without firing the callbacks
  void resized() override;

  // Read-only state and drivers for tests.
  int scalePercent() const;
  bool reducedMotion() const { return motion_.getToggleState(); }
  juce::Button& copyButton() { return copy_; }

  std::function<void(cdl::ThemeId)> onTheme;
  std::function<void(int percent)> onScale;
  std::function<void(bool)> onReducedMotion;
  std::function<void()> onResetSize;
  std::function<void()> onCopyDiagnostics;

 private:
  struct Body : juce::Component {
    SettingsDrawer& owner;
    explicit Body(SettingsDrawer& o) : owner(o) {}
    void paint(juce::Graphics& g) override;
  };
  void audioChanged();
  void layoutBody(int width);

  juce::Viewport viewport_;
  Body body_{*this};
  juce::Label themeLabel_{{}, "Theme"}, scaleLabel_{{}, "Interface scale"}, ceilingLabel_{{}, "Ceiling"};
  juce::ComboBox theme_, scale_;
  juce::ToggleButton motion_{"Reduced motion"}, limiter_{"Limiter on for new instances"}, match_{"Level match on for new instances"};
  juce::Slider ceiling_;
  juce::TextButton factory_{"Restore factory output"}, reset_{"Reset window size"}, copy_{"Copy diagnostics"};
  juce::Rectangle<int> appearanceArea_, audioArea_, windowArea_, diagArea_;
  bool syncing_ = false;
};
