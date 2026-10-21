#include "WavePanel.h"

#include <algorithm>

#include "CdlLookAndFeel.h"
#include "EditorTheme.h"

namespace cf = chopfractal;

WavePanel::WavePanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "WavePanel") { setOpaque(true); }

void WavePanel::modelChanged(Change c) {
  if (c == Change::View || c == Change::Selection) repaint();
}

void WavePanel::paint(juce::Graphics& g) {
  const auto& v = model_.view();
  const auto area = getLocalBounds();
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  g.setColour(editor_theme::background());
  g.fillRect(area);
  // Chop lanes: each chop's range tinted in its lane colour with a top bar, so waveform, chop strip and pattern lanes read as one.
  if (v.sourceFrames > 0)
    for (std::size_t i = 0; i < v.chops.size(); ++i) {
      const auto& chop = v.chops[i];
      const float x = static_cast<float>(chop.range.start) / static_cast<float>(v.sourceFrames) * static_cast<float>(area.getWidth());
      const float width = static_cast<float>(chop.range.length()) / static_cast<float>(v.sourceFrames) * static_cast<float>(area.getWidth());
      const auto lane = editor_theme::laneColour(static_cast<int>(i));
      g.setColour(lane.withAlpha(0.14f));
      g.fillRect(x, 0.0f, width, static_cast<float>(area.getHeight()));
      g.setColour(lane);
      g.fillRect(x, 0.0f, width, 2.0f);
    }
  if (!v.peaks.min.empty()) {
    g.setColour(editor_theme::accentBlue());
    const int n = static_cast<int>(v.peaks.min.size());
    const float cy = static_cast<float>(area.getCentreY());
    const float h = static_cast<float>(area.getHeight()) * 0.45f;
    for (int i = 0; i < n; ++i)
      g.drawVerticalLine(i, cy - v.peaks.max[static_cast<std::size_t>(i)] * h, cy - v.peaks.min[static_cast<std::size_t>(i)] * h);
  } else {
    g.setColour(cdl::pal().inkMuted);
    g.drawText(model_.sourceMissing() ? "The project's audio file was not found. Press Locate Source (your markers, pattern and history are kept)."
                                      : "Drop an audio sample here, or press Load Loop. Then Detect Chops, Generate and Play.",
               area, juce::Justification::centred);
  }
  // Waveform selections correspond to complete chop sections, not destructive source trims.
  const auto& selectedChops = model_.selectedChops();
  for (const auto& chop : v.chops)
    if (std::find(selectedChops.begin(), selectedChops.end(), chop.id) != selectedChops.end() && v.sourceFrames > 0) {
      const float x = static_cast<float>(chop.range.start) / static_cast<float>(v.sourceFrames) * area.getWidth();
      const float width = static_cast<float>(chop.range.length()) / static_cast<float>(v.sourceFrames) * area.getWidth();
      g.setColour(cdl::pal().spray.withAlpha(0.28f));
      g.fillRect(x, 0.0f, width, static_cast<float>(area.getHeight()));
    }
  int markerNumber = 0;
  for (const auto& m : v.markers) {
    ++markerNumber;
    g.setColour(m.enabled ? editor_theme::accentAmber() : cdl::pal().inkMuted);
    const int x = static_cast<int>(m.x);
    g.drawVerticalLine(x, 0.0f, static_cast<float>(area.getBottom()));
    g.setFont(cdl::font(cdl::FontRole::Value, 0.9f * cdl::uiScale()));
    g.drawText(juce::String(markerNumber), x + 2, 2, 28, 14, juce::Justification::left);
  }
  if (selecting_) {
    const auto rectangle = juce::Rectangle<int>(start_, end_);
    g.setColour(editor_theme::accentBlue().withAlpha(0.2f));
    g.fillRect(rectangle);
    g.setColour(editor_theme::accentBlue());
    g.drawRect(rectangle, 1);
  }
}

void WavePanel::updateSelection(juce::Point<int> point) {
  end_ = {juce::jlimit(0, getWidth() - 1, point.x), juce::jlimit(0, getHeight() - 1, point.y)};
  const auto rect = juce::Rectangle<int>(start_, end_);
  const bool click = rect.getWidth() < 3 && rect.getHeight() < 3;
  model_.selectSectionsInWave(start_.x, end_.x, click, getWidth());
  repaint();
}

void WavePanel::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  const std::int64_t frames = model_.view().sourceFrames;
  selecting_ = false;
  if (frames <= 0) return;
  if (!e.mods.isShiftDown() && !e.mods.isAltDown() && !e.mods.isPopupMenu()) {
    grabEditorFocus();
    selecting_ = true;
    start_ = end_ = pt;
    updateSelection(pt);
    for (const auto id : model_.selectedChops()) {
      cmd_.audition(id);
      break;
    }
    return;
  }
  const cf::ui::ViewWindow w{0, frames, static_cast<double>(getWidth())};
  const std::int64_t frame = cf::ui::xToFrame(w, static_cast<double>(pt.x));
  if (e.mods.isAltDown())  // Alt-click removes the nearest marker
    cmd_.removeMarkerNear(static_cast<double>(pt.x));
  else if (e.mods.isShiftDown())  // Shift-click adds a marker; a plain click only auditions the chop under the cursor
    cmd_.addMarkerAt(frame);
  else
    cmd_.auditionFrame(frame);
}

void WavePanel::mouseDrag(const juce::MouseEvent& e) {
  if (selecting_) updateSelection(e.getPosition());
}

void WavePanel::mouseUp(const juce::MouseEvent& e) {
  if (!selecting_) return;
  updateSelection(e.getPosition());
  selecting_ = false;
  model_.rebuild();
}

void WavePanel::mouseDoubleClick(const juce::MouseEvent& e) {
  const std::int64_t frames = model_.view().sourceFrames;
  if (frames <= 0) return;
  const cf::ui::ViewWindow w{0, frames, static_cast<double>(getWidth())};
  cmd_.auditionFrame(cf::ui::xToFrame(w, static_cast<double>(e.getPosition().x)));
}
