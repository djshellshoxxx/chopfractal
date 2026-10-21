#include "SettingsDrawer.h"

#include <chopfractal/plugin_ui_adapter/settings_view.hpp>

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;

SettingsDrawer::SettingsDrawer() {
  setComponentID("SettingsDrawer");
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&body_, false);
  viewport_.setScrollBarsShown(true, false);
  viewport_.setTitle("Settings");

  theme_.addItem("Night", 1);
  theme_.addItem("Concrete", 2);
  theme_.setTitle("Theme");
  theme_.setTooltip("Night is the default, Concrete is the daylight theme");
  theme_.onChange = [this] {
    if (!syncing_ && onTheme) onTheme(theme_.getSelectedId() == 2 ? cdl::ThemeId::Concrete : cdl::ThemeId::Night);
  };
  for (std::size_t i = 0; i < cf::ui::uiScaleItems().size(); ++i) scale_.addItem(cf::ui::uiScaleItems()[i], static_cast<int>(i) + 1);
  scale_.setTitle("Interface scale");
  scale_.setTooltip("Make the whole interface larger or smaller, the window size limits how far it grows");
  scale_.onChange = [this] {
    if (!syncing_ && onScale) onScale(scalePercent());
  };
  motion_.setTitle("Reduced motion");
  motion_.setTooltip("Update the playhead ten times a second instead of every frame and skip meter easing");
  motion_.onClick = [this] {
    if (!syncing_ && onReducedMotion) onReducedMotion(motion_.getToggleState());
  };
  limiter_.setTitle("Default limiter");
  limiter_.setTooltip("New plugin instances start with the safety limiter on, saved projects keep their own setting");
  match_.setTitle("Default level match");
  match_.setTooltip("New plugin instances start with level match on, saved projects keep their own setting");
  ceiling_.setSliderStyle(juce::Slider::LinearHorizontal);
  ceiling_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
  ceiling_.setRange(-6.0, 0.0, 0.5);
  ceiling_.textFromValueFunction = [](double v) { return juce::String(cf::ui::limiterCeilingText(v)); };
  ceiling_.setTitle("Default limiter ceiling");
  ceiling_.setTooltip("Limiter ceiling that new plugin instances start with");
  limiter_.onClick = [this] { audioChanged(); };
  match_.onClick = [this] { audioChanged(); };
  ceiling_.onValueChange = [this] { audioChanged(); };
  factory_.setTitle("Restore factory output");
  factory_.setTooltip("Go back to the built-in limiter and level match defaults");
  factory_.onClick = [this] {
    auto u = cdl::loadUserSettings();
    u.audioDefaultsCustom = false;
    u.defaultLimiter = false;
    u.defaultLevelMatch = false;
    u.defaultCeilingDb = -1.0f;
    cdl::saveUserSettings(u);
    showUserSettings(u);
  };
  reset_.setTitle("Reset window size");
  reset_.setTooltip("Return the window to 1100 by 720");
  reset_.onClick = [this] {
    if (onResetSize) onResetSize();
  };
  copy_.setTitle("Copy diagnostics");
  copy_.setTooltip("Copy version, host, audio, source and recent notices to the clipboard for a bug report");
  copy_.onClick = [this] {
    if (onCopyDiagnostics) onCopyDiagnostics();
  };
  for (auto* l : {&themeLabel_, &scaleLabel_, &ceilingLabel_}) {
    l->setMinimumHorizontalScale(0.8f);
    body_.addAndMakeVisible(*l);
  }
  for (juce::Component* c : std::initializer_list<juce::Component*>{&theme_, &scale_, &motion_, &limiter_, &match_, &ceiling_, &factory_, &reset_, &copy_}) body_.addAndMakeVisible(*c);
  showUserSettings(cdl::loadUserSettings());
  body_.setSize(280, 330);
}

void SettingsDrawer::showTheme(cdl::ThemeId t) {
  const bool was = syncing_;
  syncing_ = true;
  theme_.setSelectedId(t == cdl::ThemeId::Concrete ? 2 : 1, juce::dontSendNotification);
  syncing_ = was;
}

void SettingsDrawer::showUserSettings(const cdl::UserSettings& u) {
  const bool was = syncing_;
  syncing_ = true;
  scale_.setSelectedId(cf::ui::uiScaleIndex(u.uiScalePercent) + 1, juce::dontSendNotification);
  motion_.setToggleState(u.reducedMotion, juce::dontSendNotification);
  limiter_.setToggleState(u.audioDefaultsCustom ? u.defaultLimiter : false, juce::dontSendNotification);
  match_.setToggleState(u.audioDefaultsCustom ? u.defaultLevelMatch : false, juce::dontSendNotification);
  ceiling_.setValue(u.audioDefaultsCustom ? u.defaultCeilingDb : -1.0f, juce::dontSendNotification);
  factory_.setEnabled(u.audioDefaultsCustom);
  syncing_ = was;
}

int SettingsDrawer::scalePercent() const { return cf::ui::uiScalePercentAt(scale_.getSelectedId() - 1); }

void SettingsDrawer::audioChanged() {
  if (syncing_) return;
  auto u = cdl::loadUserSettings();
  u.audioDefaultsCustom = true;
  u.defaultLimiter = limiter_.getToggleState();
  u.defaultLevelMatch = match_.getToggleState();
  u.defaultCeilingDb = static_cast<float>(ceiling_.getValue());
  cdl::saveUserSettings(u);
  factory_.setEnabled(true);
}

void SettingsDrawer::Body::paint(juce::Graphics& g) {
  auto section = [&](juce::Rectangle<int> r, const char* title) { cdl::paintSectionLabel(g, title, r.removeFromTop(20)); };
  section(owner.appearanceArea_, "Appearance");
  section(owner.audioArea_, "Output defaults");
  section(owner.windowArea_, "Window");
  section(owner.diagArea_, "Support");
}

void SettingsDrawer::layoutBody(int width) {
  const int pad = 8, gap = 4;
  int y = 0;
  auto row = [&](int h = 26) {
    juce::Rectangle<int> r(pad, y, width - 2 * pad, h);
    y += h + gap;
    return r;
  };
  appearanceArea_ = {0, y, width, 0};
  y += 22;
  {
    auto r = row();
    themeLabel_.setBounds(r.removeFromLeft(100));
    theme_.setBounds(r.removeFromLeft(120));
  }
  {
    auto r = row();
    scaleLabel_.setBounds(r.removeFromLeft(100));
    scale_.setBounds(r.removeFromLeft(120));
  }
  motion_.setBounds(row());
  appearanceArea_.setHeight(y - appearanceArea_.getY());
  y += 8;
  audioArea_ = {0, y, width, 0};
  y += 22;
  limiter_.setBounds(row());
  match_.setBounds(row());
  {
    auto r = row();
    ceilingLabel_.setBounds(r.removeFromLeft(60));
    ceiling_.setBounds(r);
  }
  factory_.setBounds(row());
  audioArea_.setHeight(y - audioArea_.getY());
  y += 8;
  windowArea_ = {0, y, width, 0};
  y += 22;
  reset_.setBounds(row());
  windowArea_.setHeight(y - windowArea_.getY());
  y += 8;
  diagArea_ = {0, y, width, 0};
  y += 22;
  copy_.setBounds(row());
  diagArea_.setHeight(y - diagArea_.getY());
  body_.setSize(width, y + 8);
}

void SettingsDrawer::resized() {
  viewport_.setBounds(getLocalBounds());
  layoutBody(std::max(220, getWidth() - viewport_.getScrollBarThickness()));
}
