#include "StatusBar.h"

#include "BuildInfo.h"
#include "CdlLookAndFeel.h"

StatusBar::StatusBar(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "StatusBar") {
  messages_.setTitle("Status messages");
  messages_.setMultiLine(true, true);
  messages_.setReadOnly(true);
  messages_.setScrollbarsShown(true);
  messages_.setCaretVisible(false);
  messages_.setPopupMenuEnabled(true);
  messages_.setBorder({2, 6, 2, 6});
  messages_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
  messages_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
  messages_.setIndents(0, 0);
  lookAndFeelChanged();
  messages_.setTooltip("Full status message. Scroll to read long messages; select text to copy it.");
  addAndMakeVisible(messages_);
  cancelExport_.setTitle("Cancel Export");
  cancelExport_.onClick = [this] { cmd_.cancelExport(); };
  addChildComponent(cancelExport_);
}

void StatusBar::lookAndFeelChanged() {
  messages_.setColour(juce::TextEditor::backgroundColourId, cdl::pal().bg);
  messages_.setColour(juce::TextEditor::textColourId, cdl::pal().ink);
  messages_.applyFontToAllText(cdl::font(cdl::FontRole::Body, scale_));
}

void StatusBar::refreshReadouts() {
  const juce::String now = juce::String(static_cast<juce::int64>(model_.seed())) + "|" + juce::String(model_.activeBpm(), 1) + "|" + (model_.usedFallbackTempo() ? "L" : "H");
  if (now == lastReadout_) return;
  lastReadout_ = now;
  repaint(infoArea_);
}

void StatusBar::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  g.setColour(p.panel);
  g.fillRect(getLocalBounds());
  g.setColour(p.edge);
  g.fillRect(0, 0, getWidth(), 1);
  if (infoArea_.isEmpty()) return;
  auto r = infoArea_;
  auto draw = [&](const juce::String& label, const juce::String& value, int width) {
    auto cell = r.removeFromLeft(width);
    const auto lf = cdl::font(cdl::FontRole::Hint, scale_);
    const auto vf = cdl::font(cdl::FontRole::Value, scale_);
    const int lw = juce::GlyphArrangement::getStringWidthInt(lf, label) + 4;
    g.setFont(lf);
    g.setColour(p.inkMuted);
    g.drawText(label, cell.withWidth(lw), juce::Justification::centredLeft, false);
    g.setFont(vf);
    g.setColour(p.ink);
    g.drawText(value, cell.withTrimmedLeft(lw), juce::Justification::centredLeft, false);
  };
  draw("Seed", juce::String(static_cast<juce::int64>(model_.seed())), static_cast<int>(130 * scale_));
  draw(model_.usedFallbackTempo() ? "Local" : "Host", juce::String(model_.activeBpm(), 1) + " BPM", static_cast<int>(130 * scale_));
  // version string sits 1 px above the baseline; the sprayed tag is tilted 4 degrees
  g.setFont(cdl::font(cdl::FontRole::MonoXs, scale_));
  g.setColour(p.inkMuted);
  const auto ver = juce::String("v") + cfbuild::kVersionNum;
  g.drawText(ver, r.removeFromRight(static_cast<int>(60 * scale_)).translated(0, -1), juce::Justification::centredRight, false);
  g.saveState();
  const auto tagArea = r.removeFromRight(static_cast<int>(36 * scale_));
  g.addTransform(juce::AffineTransform::rotation(-4.0f * juce::MathConstants<float>::pi / 180.0f, static_cast<float>(tagArea.getCentreX()), static_cast<float>(tagArea.getCentreY())));
  g.setFont(cdl::font(cdl::FontRole::Tag, scale_));
  g.setColour(p.spray);
  g.drawText("cdl", tagArea, juce::Justification::centred, false);
  g.restoreState();
}

void StatusBar::resized() {
  auto area = getLocalBounds();
  infoArea_ = area.removeFromRight(static_cast<int>(std::min(area.getWidth() / 2, static_cast<int>(420 * scale_))));
  if (cancelExport_.isVisible()) cancelExport_.setBounds(area.removeFromRight(110).removeFromTop(20).reduced(2));
  messages_.setBounds(area);
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
  if (c != Change::Status) return;
  // Messages arrive from other threads' work too (file loading, Evolve, restore): show the line when it changes.
  const juce::String line = model_.statusLine();
  if (line == lastLine_) return;
  lastLine_ = line;
  messages_.setText(line, false);
  messages_.moveCaretToTop(false);
}
