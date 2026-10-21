#include "NoticeLogPanel.h"

#include <algorithm>
#include <cstddef>

#include <chopfractal/plugin_ui_adapter/status_text.hpp>

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;

NoticeLogPanel::NoticeLogPanel(NoticeLog& log) : log_(log) {
  setComponentID("NoticeLogPanel");
  setTitle("Notice log");
  list_.setRowHeight(18);
  list_.setTitle("Notices");
  list_.setColour(juce::ListBox::backgroundColourId, cdl::pal().bg);
  addAndMakeVisible(list_);
  clear_.setTitle("Clear notices");
  clear_.setTooltip("Remove every entry from the notice log");
  clear_.onClick = [this] { clearLog(); };
  copy_.setTitle("Copy notices");
  copy_.setTooltip("Copy the whole notice log to the clipboard as text");
  copy_.onClick = [this] { copyAll(); };
  addAndMakeVisible(clear_);
  addAndMakeVisible(copy_);
  setSize(460, 240);
  refresh();
}

void NoticeLogPanel::refresh() {
  rows_ = log_.snapshot();
  std::reverse(rows_.begin(), rows_.end());
  list_.updateContent();
  list_.repaint();
  repaint();
}

juce::String NoticeLogPanel::rowText(int row) const {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
  const auto& e = rows_[static_cast<std::size_t>(row)];
  return e.time.formatted("%H:%M:%S") + "  " + juce::String(cf::ui::severityPrefix(static_cast<int>(e.severity))) + e.shown();
}

juce::String NoticeLogPanel::allText() const {
  juce::StringArray lines;
  for (int i = 0; i < rowCount(); ++i) lines.add(rowText(i));
  return lines.joinIntoString("\n");
}

void NoticeLogPanel::clearLog() {
  log_.clear();
  refresh();
}

void NoticeLogPanel::copyAll() { juce::SystemClipboard::copyTextToClipboard(allText()); }

void NoticeLogPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const auto& p = cdl::pal();
  if (selected) {
    g.setColour(p.sprayWash);
    g.fillRect(0, 0, w, h);
  }
  const auto sev = rows_[static_cast<std::size_t>(row)].severity;
  g.setColour(sev == Severity::Error ? p.clip : sev == Severity::Warning ? p.warn : p.ink.withAlpha(0.8f));
  g.setFont(cdl::font(cdl::FontRole::MonoXs, 1.0f));
  g.drawText(rowText(row), 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

void NoticeLogPanel::paint(juce::Graphics& g) {
  g.fillAll(cdl::pal().panel);
  if (rows_.empty()) {
    g.setColour(cdl::pal().inkMuted);
    g.setFont(cdl::font(cdl::FontRole::Body, 1.0f));
    g.drawText("No notices yet.", list_.getBounds(), juce::Justification::centred, false);
  }
}

void NoticeLogPanel::resized() {
  auto a = getLocalBounds().reduced(6);
  auto bar = a.removeFromBottom(28);
  copy_.setBounds(bar.removeFromRight(80));
  bar.removeFromRight(6);
  clear_.setBounds(bar.removeFromRight(80));
  a.removeFromBottom(4);
  list_.setBounds(a);
}
