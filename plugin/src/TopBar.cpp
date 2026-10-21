#include "TopBar.h"

#include "CdlLookAndFeel.h"

TopBar::TopBar(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "TopBar") {
  for (auto* b : {&play_, &stop_, &undo_, &redo_, &theme_, &mode_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  play_.onClick = [this] { cmd_.play(); };
  stop_.onClick = [this] { cmd_.stop(); };
  undo_.onClick = [this] { cmd_.undo(); };
  redo_.onClick = [this] { cmd_.redo(); };
  theme_.setTooltip("Switch between the Night and Concrete themes");
  theme_.onClick = [this] {
    if (onTheme) onTheme();
  };
  mode_.setTooltip("Simple shows the essentials, Advanced adds hit editing and lane controls");
  mode_.onClick = [this] {
    if (onMode) onMode();
  };
  settings_.setTitle("Settings");
  settings_.setTooltip("Theme and window settings");
  settings_.onClick = [this] {
    if (onSettings) onSettings();
  };
  addAndMakeVisible(settings_);
  lookAndFeelChanged();
}

void TopBar::lookAndFeelChanged() {
  // The gear is drawn by hand: a ring with eight teeth, 1.5 px stroke, round joins.
  auto makeGear = [](juce::Colour) {
    juce::Path p;
    const juce::Point<float> c(8.0f, 8.0f);
    p.addEllipse(c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
    for (int i = 0; i < 8; ++i) {
      const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 8.0f;
      p.startNewSubPath(c.x + 4.6f * std::sin(a), c.y - 4.6f * std::cos(a));
      p.lineTo(c.x + 6.6f * std::sin(a), c.y - 6.6f * std::cos(a));
    }
    p.addEllipse(c.x - 4.6f, c.y - 4.6f, 9.2f, 9.2f);
    return p;
  };
  settings_.setShape(makeGear({}), false, true, false);
  settings_.setColours(cdl::pal().inkMuted, cdl::pal().ink, cdl::pal().spray);
  settings_.setOutline(cdl::pal().inkMuted, 1.5f);
  theme_.setButtonText(cdl::currentTheme() == cdl::ThemeId::Night ? "Night" : "Concrete");
}

void TopBar::setPresetName(const juce::String& n) {
  if (n == presetName_) return;
  presetName_ = n;
  repaint(presetArea_);
}

void TopBar::setAdvanced(bool on) {
  mode_.setButtonText(on ? "Advanced" : "Simple");
  mode_.setToggleState(on, juce::dontSendNotification);
}

void TopBar::syncTransport() {
  const bool playing = model_.playheadQuarters() >= 0.0;
  if (play_.getToggleState() != playing) play_.setToggleState(playing, juce::dontSendNotification);
  repaint(bpmArea_);
}

void TopBar::addDrawerButton(const juce::String& id, const juce::String& label, std::function<void(const juce::String&)> onClick) {
  DrawerButton d{id, std::make_unique<juce::TextButton>(label)};
  d.button->setComponentID("drawer-" + id);
  d.button->setTitle(label);
  d.button->setTooltip("Open or close the " + label.toLowerCase() + " drawer");
  d.button->onClick = [id, onClick] { onClick(id); };
  addAndMakeVisible(*d.button);
  drawerButtons_.push_back(std::move(d));
  resized();
}

void TopBar::setDrawerButtonOn(const juce::String& openId) {
  for (auto& d : drawerButtons_) d.button->setToggleState(d.id == openId, juce::dontSendNotification);
}

void TopBar::resized() {
  const float s = scale_;
  auto px = [s](float v) { return static_cast<int>(std::lround(v * s)); };
  const bool narrow = getWidth() < 960;
  auto area = getLocalBounds().reduced(px(12), px(6));
  const int gap = px(6);
  titleArea_ = area.removeFromLeft(px(196));
  area.removeFromLeft(gap);
  play_.setBounds(area.removeFromLeft(px(56)));
  area.removeFromLeft(gap);
  stop_.setBounds(area.removeFromLeft(px(56)));
  area.removeFromLeft(gap * 2);
  if (!narrow) {
    bpmArea_ = area.removeFromLeft(px(96));
    area.removeFromLeft(gap);
  } else {
    bpmArea_ = {};
  }
  undo_.setBounds(area.removeFromLeft(px(56)));
  area.removeFromLeft(gap);
  redo_.setBounds(area.removeFromLeft(px(56)));
  area.removeFromLeft(gap * 2);
  // Right side, from the edge inwards.
  mode_.setBounds(area.removeFromRight(px(92)));
  area.removeFromRight(gap);
  settings_.setBounds(area.removeFromRight(area.getHeight()).reduced(px(4)));
  area.removeFromRight(gap);
  theme_.setBounds(area.removeFromRight(px(84)));
  area.removeFromRight(gap);
  for (auto it = drawerButtons_.rbegin(); it != drawerButtons_.rend(); ++it) {
    it->button->setBounds(area.removeFromRight(px(64)));
    area.removeFromRight(gap);
  }
  presetArea_ = area;
}

void TopBar::mouseDown(const juce::MouseEvent& e) {
  if (presetArea_.contains(e.getPosition()) && onPresetClick) onPresetClick();
}

void TopBar::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  g.setColour(p.panel);
  g.fillRect(getLocalBounds().withTrimmedLeft(1));
  g.setColour(p.edge);
  g.fillRect(1, getHeight() - 1, getWidth() - 1, 1);  // hand mark: the separator starts 1 px in
  cdl::paintStencilTitle(g, "ChopFractal", titleArea_.withTrimmedLeft(static_cast<int>(std::lround(6 * scale_))), scale_);
  if (!bpmArea_.isEmpty()) {
    const auto bpm = juce::String(model_.activeBpm(), 1);
    g.setFont(cdl::font(cdl::FontRole::ValueLg, scale_));
    g.setColour(p.ink);
    const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), bpm);
    g.drawText(bpm, bpmArea_.withWidth(w + 2), juce::Justification::centredLeft, false);
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::Hint, scale_));
    g.drawText("BPM", bpmArea_.withTrimmedLeft(w + 6), juce::Justification::centredLeft, false);
  }
  if (!presetArea_.isEmpty()) {
    g.setFont(cdl::font(cdl::FontRole::Body, scale_));
    g.setColour(presetName_.isEmpty() ? p.inkMuted : p.ink);
    g.drawText(presetName_.isEmpty() ? "no preset" : presetName_.toLowerCase(), presetArea_, juce::Justification::centred, true);
  }
}
