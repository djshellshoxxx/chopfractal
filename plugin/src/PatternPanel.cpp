#include "PatternPanel.h"

#include <algorithm>
#include <chopfractal/chop_contracts/time.hpp>
#include <chopfractal/plugin_ui_adapter/hit_edit.hpp>
#include <cmath>
#include <cstdlib>

#include "CdlLookAndFeel.h"
#include "EditorTheme.h"

namespace cf = chopfractal;
using editor_theme::laneColour;

PatternPanel::PatternPanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "PatternPanel"), playhead_(model) {
  setOpaque(true);
  setWantsKeyboardFocus(true);
  addAndMakeVisible(playhead_);
}

void PatternPanel::resized() { playhead_.setBounds(getLocalBounds()); }

void PatternPanel::setPlaying(bool on) {
  playing_ = on;
  if (on && !vblank_ && !cdl::reducedMotion()) vblank_ = std::make_unique<juce::VBlankAttachment>(this, [this] { playhead_.repaint(); });
  if ((!on || cdl::reducedMotion()) && vblank_) {
    vblank_.reset();
    playhead_.repaint();
  }
}

void PatternPanel::refreshPlayhead() {
  if (playing_ && cdl::reducedMotion()) playhead_.repaint();  // reduced motion: the 10 Hz editor timer instead of every frame
}

void PatternPanel::PlayheadLayer::paint(juce::Graphics& g) {
  const double pos = model.playheadQuarters();
  const auto len = model.view().orbit.lengthTicks;
  if (pos < 0.0 || len <= 0) return;
  const double ticks = std::fmod(pos * static_cast<double>(chopfractal::kTicksPerQuarter), static_cast<double>(len));
  const float x = static_cast<float>(ticks / static_cast<double>(len)) * static_cast<float>(getWidth());
  g.setColour(cdl::pal().rain);
  g.fillRect(x - 1.0f, 0.0f, 2.0f, static_cast<float>(getHeight()));
  juce::Path tri;
  tri.addTriangle(x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
  g.fillPath(tri);
}

void PatternPanel::modelChanged(Change c) {
  if (c == Change::View || c == Change::Selection) repaint();
}

void PatternPanel::paint(juce::Graphics& g) {
  const auto& v = model_.view();
  const auto& pattern = v.pattern;
  const auto area = getLocalBounds();
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  g.setColour(editor_theme::background());
  g.fillRect(area);
  // Lanes by chop, numbered so colour is never the only indicator.
  const int lanes = std::max(1, static_cast<int>(v.chops.size()));
  const float laneH = static_cast<float>(area.getHeight()) / static_cast<float>(lanes);
  const float w = static_cast<float>(area.getWidth());
  g.setColour(cdl::pal().edge.withAlpha(0.55f));  // lane separators
  for (int lane = 1; lane < lanes; ++lane) g.drawHorizontalLine(static_cast<int>(static_cast<float>(lane) * laneH), 0.0f, w);
  g.setColour(cdl::pal().edge);
  for (double x : pattern.beatLines) g.drawVerticalLine(static_cast<int>(x * w), 0.0f, static_cast<float>(area.getBottom()));
  for (std::size_t b = 0; b < pattern.barLines.size(); ++b) {
    g.setColour(cdl::pal().edgeHi);
    const int x = static_cast<int>(pattern.barLines[b] * w);
    g.drawVerticalLine(x, 0.0f, static_cast<float>(area.getBottom()));
    if (b < pattern.barLocked.size() && pattern.barLocked[b]) {
      g.setColour(editor_theme::accentRed());
      g.drawText("locked", x + 3, area.getBottom() - 16, 60, 14, juce::Justification::left);
    }
  }
  const auto& selectedHits = model_.selectedHits();
  for (const auto& r : pattern.rects) {
    const float inset = static_cast<float>(r.depth) * 3.0f;
    const float y = static_cast<float>(std::max(0, r.chopIndex)) * laneH + inset;
    juce::Rectangle<float> rect(static_cast<float>(r.x0) * w, y, std::max(2.0f, static_cast<float>(r.x1 - r.x0) * w - 1.0f), std::max(4.0f, laneH - 2.0f - 2.0f * inset));
    g.setColour(laneColour(r.chopIndex).withAlpha(r.depth > 0 ? 0.95f : 0.75f));
    g.fillRect(rect);
    if (r.id == model_.selected() || std::find(selectedHits.begin(), selectedHits.end(), r.id) != selectedHits.end()) {
      g.setColour(cdl::pal().ink);
      g.drawRect(rect, 2.0f);
    } else if (r.locked || r.hasChild) {
      g.setColour(r.locked ? editor_theme::accentRed() : cdl::pal().ink);
      g.drawRect(rect, 1.0f);
    }
    if (rect.getWidth() > 14.0f && rect.getHeight() > 12.0f) {
      g.setColour(cdl::pal().bg);
      g.setFont(cdl::font(cdl::FontRole::Value, 0.9f * cdl::uiScale()));
      g.drawText(juce::String(r.chopIndex + 1), rect.toNearestInt(), juce::Justification::centred);
    }
  }
  if (pattern.rects.empty()) {
    g.setColour(cdl::pal().inkMuted);
    g.drawText("Detect chops, then press Generate.", area, juce::Justification::centred);
  }

  // Ghost outline of the hit being dragged (the edit is applied on release).
  if (dragMode_ != DragMode::None)
    for (const auto& r : pattern.rects)
      if (r.id == dragId_) {
        const float pw = static_cast<float>(area.getWidth());
        const float lh = static_cast<float>(area.getHeight()) / static_cast<float>(std::max<std::size_t>(1, v.chops.size()));
        const float dx = static_cast<float>(dragNow_.x - dragStart_.x);
        float x = static_cast<float>(r.x0) * pw;
        float width = static_cast<float>(r.x1 - r.x0) * pw;
        if (dragMode_ == DragMode::Move) x += dx; else width = std::max(2.0f, width + dx);
        g.setColour(cdl::pal().ink.withAlpha(0.8f));
        g.drawRect(x, static_cast<float>(r.chopIndex) * lh + 2.0f, width, lh - 4.0f, 1.5f);
        break;
      }

  if (selecting_) {
    const auto rectangle = juce::Rectangle<int>(start_, end_);
    g.setColour(editor_theme::accentBlue().withAlpha(0.2f));
    g.fillRect(rectangle);
    g.setColour(editor_theme::accentBlue());
    g.drawRect(rectangle, 1);
  }
}

void PatternPanel::updateSelection(juce::Point<int> point) {
  end_ = {juce::jlimit(0, getWidth() - 1, point.x), juce::jlimit(0, getHeight() - 1, point.y)};
  const auto rect = juce::Rectangle<int>(start_, end_);
  const bool click = rect.getWidth() < 3 && rect.getHeight() < 3;
  model_.selectSectionsInPattern(rect, end_, click, getLocalBounds());
  repaint();
}

void PatternPanel::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  const auto& v = model_.view();
  selecting_ = false;
  if (!e.mods.isShiftDown() && !e.mods.isAltDown() && !e.mods.isPopupMenu() && !model_.editMode()) {
    grabEditorFocus();
    dragMode_ = DragMode::None;
    selecting_ = true;
    start_ = end_ = pt;
    updateSelection(pt);
    return;
  }
  grabEditorFocus();
  model_.clearSelection();
  dragMode_ = DragMode::None;
  const double w = static_cast<double>(getWidth());
  const double fx = static_cast<double>(pt.x) / w;
  const int lane = cf::ui::laneAtY(static_cast<double>(pt.y), static_cast<double>(getHeight()), static_cast<int>(v.chops.size()));
  for (auto it = v.pattern.rects.rbegin(); it != v.pattern.rects.rend(); ++it)
    if (it->chopIndex == lane && fx >= it->x0 && fx <= it->x1) {
      model_.select(it->id);
      if (it->depth == 0 && !it->locked && !e.mods.isPopupMenu()) {  // only top-level, unlocked hits can be dragged
        const double edgePx = (it->x1 - fx) * w;
        const double edgeZone = std::min(6.0, (it->x1 - it->x0) * w / 3.0);  // narrow hits keep a move zone
        dragMode_ = edgePx <= edgeZone ? DragMode::Resize : DragMode::Move;
        dragId_ = it->id;
        dragStart_ = dragNow_ = pt;
        dragLane_ = lane;
        dragCopy_ = e.mods.isAltDown();
      }
      break;
    }
  if (!model_.selected().valid() && e.mods.isShiftDown() && lane >= 0 && v.gridTicks > 0 && v.barTicks > 0 && v.barCount > 0) {  // Shift-click on empty space adds a hit
    const cf::Ticks total = v.barTicks * v.barCount;
    const auto pos = cf::ui::splitTick(cf::ui::snapDown(cf::ui::xToTick(static_cast<double>(pt.x), w, total), v.gridTicks), v.barTicks, v.barCount);
    const cf::Ticks dur = std::min<cf::Ticks>(v.gridTicks, v.barTicks - pos.startInBar);
    cmd_.editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> cf::Result<cf::pattern::Pattern> {
      if (lane >= static_cast<int>(chops.chops.size())) return cf::makeError(cf::ErrorCode::OutOfRange, "no chop in that lane");
      return cf::pattern::addEvent(p, pos.bar, chops.chops[static_cast<std::size_t>(lane)].id, pos.startInBar, dur, chops);
    });
  }
  repaint();
}

void PatternPanel::mouseDrag(const juce::MouseEvent& e) {
  if (selecting_) {
    updateSelection(e.getPosition());
    return;
  }
  if (dragMode_ == DragMode::None) return;
  dragNow_ = e.getPosition();
  repaint();
}

void PatternPanel::mouseUp(const juce::MouseEvent& e) {
  if (selecting_) {
    updateSelection(e.getPosition());
    selecting_ = false;
    model_.rebuild();
    return;
  }
  if (dragMode_ == DragMode::None) return;
  const DragMode mode = dragMode_;
  dragMode_ = DragMode::None;
  dragNow_ = e.getPosition();
  const int dx = dragNow_.x - dragStart_.x;
  const int dy = dragNow_.y - dragStart_.y;
  repaint();
  if (std::abs(dx) < 3 && std::abs(dy) < 3) return;  // a click, not a drag
  const auto& v = model_.view();
  if (v.gridTicks <= 0 || v.barTicks <= 0 || v.barCount <= 0) return;
  const cf::EventId id = dragId_;
  const cf::Ticks total = v.barTicks * v.barCount;
  const double w = static_cast<double>(getWidth());
  const cf::Ticks grid = v.gridTicks, barT = v.barTicks;
  const int bars = v.barCount;
  const cf::Ticks dTicks = static_cast<cf::Ticks>(std::llround(static_cast<double>(dx) / w * static_cast<double>(total)));
  const int newLane = cf::ui::laneAtY(static_cast<double>(dragNow_.y), static_cast<double>(getHeight()), static_cast<int>(v.chops.size()));
  const bool copy = dragCopy_;
  const int oldLane = dragLane_;
  cmd_.editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> cf::Result<cf::pattern::Pattern> {
    const cf::Event* ev = cf::pattern::findEvent(p, id);
    const auto loc = cf::pattern::locate(p, id);
    if (!ev || !loc) return cf::makeError(cf::ErrorCode::NotFound, "that hit no longer exists");
    const cf::Ticks absStart = static_cast<cf::Ticks>(loc->bar) * barT + ev->start;
    if (mode == DragMode::Resize) {
      const cf::Ticks absEnd = cf::ui::snapNearest(absStart + ev->duration + dTicks, grid);
      const cf::Ticks minDur = std::min(grid, barT - ev->start);  // never shrink a hit to an inaudible sliver
      const cf::Ticks dur = std::max(minDur, std::min(absEnd - absStart, barT - ev->start));
      return cf::pattern::resizeEvent(p, id, dur);
    }
    const cf::Ticks lastSlot = std::max<cf::Ticks>(0, barT * bars - grid);  // snapping may round up to the very end: keep a full cell
    const auto pos = cf::ui::splitTick(std::min(lastSlot, cf::ui::snapNearest(std::max<cf::Ticks>(0, absStart + dTicks), grid)), barT, bars);
    cf::Result<cf::pattern::Pattern> r = cf::makeError(cf::ErrorCode::InvalidArgument, "unreachable");
    if (copy) {
      if (pos.bar != loc->bar) return cf::makeError(cf::ErrorCode::InvalidArgument, "Alt-drag copies within the same bar.");
      r = cf::pattern::duplicateEvent(p, id, pos.startInBar);
    } else {
      r = cf::pattern::moveEvent(p, id, pos.bar, pos.startInBar);
    }
    if (!r.ok() || copy || newLane < 0 || newLane == oldLane || newLane >= static_cast<int>(chops.chops.size())) return r;
    // Vertical drag re-assigns the hit to the chop of the lane it was dropped on.
    const cf::Event* moved = cf::pattern::findEvent(r.value(), id);
    return moved ? cf::pattern::setEventChop(r.value(), id, chops.chops[static_cast<std::size_t>(newLane)].id, chops) : r;
  });
}
