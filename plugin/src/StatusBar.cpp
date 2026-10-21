#include "StatusBar.h"

#include <chopfractal/plugin_ui_adapter/status_text.hpp>

#include "BuildInfo.h"
#include "CdlLookAndFeel.h"

namespace cf = chopfractal;

void StatusBar::MessageButton::paintButton(juce::Graphics& g, bool over, bool) {
  const auto& p = cdl::pal();
  auto r = getLocalBounds();
  if (over || hasKeyboardFocus(false)) {
    g.setColour(p.sprayWash);
    g.fillRect(r);
  }
  g.setColour(colour);
  g.setFont(cdl::font(cdl::FontRole::Body, scale));
  g.drawText(text, r.reduced(static_cast<int>(8 * scale), 0), juce::Justification::centredLeft, true);
}

StatusBar::StatusBar(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "StatusBar"), meter_(model) {
  message_.setTitle("Status messages");
  message_.setTooltip("Latest message, click to open the notice log with every warning and error of this session");
  message_.onClick = [this] { showLog(); };
  addAndMakeVisible(message_);
  addAndMakeVisible(meter_);
  voices_.setTooltip("Active voices, sounding or fading");
  sync_.setTooltip("Following the host tempo and time signature");
  addAndMakeVisible(voices_);
  addAndMakeVisible(sync_);
  cancelExport_.setTitle("Cancel Export");
  cancelExport_.onClick = [this] { cmd_.cancelExport(); };
  addChildComponent(cancelExport_);
  lookAndFeelChanged();
  refreshReadouts();
  updateMessage();
}

StatusBar::~StatusBar() {
  if (box_ != nullptr) box_->dismiss();  // the popup is a child of the editor and must not outlive the panels it reads
}

void StatusBar::setScale(float s) {
  scale_ = s;
  message_.scale = s;
  meter_.setScale(s);
  voices_.setScale(s);
  sync_.setScale(s);
  resized();
  repaint();
}

void StatusBar::lookAndFeelChanged() { updateMessage(); }

void StatusBar::updateMessage() {
  const juce::String line = model_.statusLine();
  const int sev = static_cast<int>(model_.statusSeverity());
  const juce::String prefix = juce::String(cf::ui::severityPrefix(sev));
  const juce::String firstLine = line.upToFirstOccurrenceOf("\n", false, false);
  const juce::String text = line.isEmpty() ? juce::String() : prefix + firstLine;
  const auto& p = cdl::pal();
  const juce::Colour colour = sev == 2 ? p.clip : sev == 1 ? p.warn : p.ink.withAlpha(0.8f);
  if (text == shown_ && colour == message_.colour) return;
  shown_ = text;
  message_.text = text;
  message_.colour = colour;
  message_.setDescription(line);
  message_.repaint();
}

void StatusBar::refreshReadouts() {
  const bool compact = getWidth() < 900;
  if (compact != compact_) {
    compact_ = compact;
    resized();
  }
  sync_.setText(model_.syncText(compact_));
  sync_.setTooltip(juce::String(cf::ui::syncTooltip(model_.activeBpm(), model_.usedFallbackTempo(), model_.usedFallbackMeter())));
  voices_.setText(juce::String(cf::ui::voiceText(meter_.voices(), EditorModel::kMaxVoices)));
  voices_.setTooltip("Active voices: " + juce::String(meter_.voices()) + " of " + juce::String(EditorModel::kMaxVoices) + ", sounding or fading");
  const juce::String seed = juce::String(static_cast<juce::int64>(model_.seed()));
  if (seed != lastSeed_) {
    lastSeed_ = seed;
    repaint(seedArea_);
  }
  updateMessage();
}

NoticeLogPanel* StatusBar::showLog() {
  if (log_ != nullptr) {
    log_->refresh();
    return log_.getComponent();
  }
  auto content = std::make_unique<NoticeLogPanel>(cmd_.noticeLog());
  log_ = content.get();
  if (auto* parent = getParentComponent()) {
    box_ = &juce::CallOutBox::launchAsynchronously(std::move(content), parent->getLocalArea(this, message_.getBounds()), parent);
  } else {
    return nullptr;
  }
  return log_.getComponent();
}

void StatusBar::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  g.setColour(p.panel);
  g.fillRect(getLocalBounds());
  g.setColour(p.edge);
  g.fillRect(0, 0, getWidth(), 1);
  if (!seedArea_.isEmpty()) {
    const auto lf = cdl::font(cdl::FontRole::Hint, scale_);
    const auto vf = cdl::font(cdl::FontRole::Value, scale_);
    const int lw = juce::GlyphArrangement::getStringWidthInt(lf, "Seed") + 4;
    g.setFont(lf);
    g.setColour(p.inkMuted);
    g.drawText("Seed", seedArea_.withWidth(lw), juce::Justification::centredLeft, false);
    g.setFont(vf);
    g.setColour(p.ink);
    g.drawText(lastSeed_, seedArea_.withTrimmedLeft(lw), juce::Justification::centredLeft, true);
  }
  g.setFont(cdl::font(cdl::FontRole::MonoXs, scale_));
  g.setColour(p.inkMuted);
  g.drawText(juce::String("v") + cfbuild::kVersionNum, versionArea_.withTrimmedRight(static_cast<int>(40 * scale_)), juce::Justification::centredRight, false);
  g.saveState();
  const auto tagArea = versionArea_.removeFromRight(static_cast<int>(36 * scale_));
  g.addTransform(juce::AffineTransform::rotation(-4.0f * juce::MathConstants<float>::pi / 180.0f, static_cast<float>(tagArea.getCentreX()), static_cast<float>(tagArea.getCentreY())));
  g.setFont(cdl::font(cdl::FontRole::Tag, scale_));
  g.setColour(p.spray);
  g.drawText("cdl", tagArea, juce::Justification::centred, false);
  g.restoreState();
}

void StatusBar::resized() {
  auto area = getLocalBounds().withTrimmedTop(1);
  auto w = [this](float v) { return static_cast<int>(v * scale_); };
  versionArea_ = area.removeFromRight(w(96));
  seedArea_ = area.removeFromRight(w(compact_ ? 84 : 104));
  area.removeFromRight(w(6));
  sync_.setBounds(area.removeFromRight(w(compact_ ? 158 : 292)));
  area.removeFromRight(w(6));
  voices_.setBounds(area.removeFromRight(w(46)));
  area.removeFromRight(w(6));
  meter_.setBounds(area.removeFromRight(w(128)).reduced(0, w(3)));
  area.removeFromRight(w(6));
  if (cancelExport_.isVisible()) {
    auto b = area.removeFromRight(w(110));
    cancelExport_.setBounds(b.withSizeKeepingCentre(b.getWidth(), std::min(b.getHeight(), w(20))));
    area.removeFromRight(w(6));
  }
  message_.setBounds(area);
}

void StatusBar::modelChanged(Change c) {
  if (c == Change::Flags) {
    if (cancelExport_.isVisible() != model_.exportRunning()) {
      cancelExport_.setVisible(model_.exportRunning());
      resized();
    }
    return;
  }
  if (c == Change::Seed) refreshReadouts();
  if (c == Change::Status) updateMessage();
}
