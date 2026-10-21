#include "PatternPanel.h"

#include <algorithm>
#include <chopfractal/chop_contracts/time.hpp>
#include <chopfractal/plugin_ui_adapter/edit_geometry.hpp>
#include <chopfractal/plugin_ui_adapter/hit_edit.hpp>
#include <cmath>
#include <cstdlib>

#include "CdlLookAndFeel.h"
#include "EditorTheme.h"

namespace cf = chopfractal;
using editor_theme::laneColour;

namespace {
constexpr int kDragStartPx = 4;
constexpr int kCancelPx = 2;
constexpr float kScrollBarW = 8.0f;
}  // namespace

PatternPanel::PatternPanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "PatternPanel"), playhead_(model) {
  setOpaque(true);
  setWantsKeyboardFocus(true);
  setTitle("Pattern grid");
  addAndMakeVisible(playhead_);
}

void PatternPanel::resized() {
  playhead_.setBounds(getLocalBounds());
  model_.setLaneView(static_cast<double>(getHeight()), 22.0 * static_cast<double>(cdl::uiScale()));
}

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
  if (c == Change::Selection) {
    const int lane = model_.selectedChopIndex();
    if (lane >= 0) model_.revealLane(lane);  // keep the selected hit's lane visible
  }
  if (c == Change::View || c == Change::Selection || c == Change::Scroll) repaint();
}

int PatternPanel::laneAt(double y) const { return cf::ui::laneAtScrolledY(model_.lanes(), y); }

juce::Rectangle<float> PatternPanel::rectFor(const cf::ui::EventRect& r) const {
  const auto lanes = model_.lanes();
  const float w = static_cast<float>(getWidth());
  const float laneH = static_cast<float>(lanes.laneHeight > 0.0 ? lanes.laneHeight : getHeight());
  const float inset = static_cast<float>(r.depth) * 3.0f;
  const float y = static_cast<float>(cf::ui::laneTop(lanes, std::max(0, r.chopIndex))) + inset + 1.0f;
  return {static_cast<float>(r.x0) * w, y, std::max(2.0f, static_cast<float>(r.x1 - r.x0) * w - 1.0f), std::max(4.0f, laneH - 2.0f - 2.0f * inset)};
}

const cf::ui::EventRect* PatternPanel::hitAt(juce::Point<int> pt) const {
  const auto& v = model_.view();
  const double w = static_cast<double>(std::max(1, getWidth()));
  const double fx = static_cast<double>(pt.x) / w;
  const int lane = laneAt(static_cast<double>(pt.y));
  for (auto it = v.pattern.rects.rbegin(); it != v.pattern.rects.rend(); ++it)
    if (it->chopIndex == lane && fx >= it->x0 && fx <= it->x1) return &*it;
  return nullptr;
}

juce::Rectangle<float> PatternPanel::scrollBarArea() const {
  const auto lanes = model_.lanes();
  if (!lanes.scrollable()) return {};
  return {static_cast<float>(getWidth()) - kScrollBarW, 0.0f, kScrollBarW, static_cast<float>(getHeight())};
}

bool PatternPanel::snapOn(const juce::ModifierKeys& mods) const { return model_.extras().ui.gridSnap && !mods.isCommandDown(); }

void PatternPanel::paint(juce::Graphics& g) {
  const auto& v = model_.view();
  const auto& pattern = v.pattern;
  const auto area = getLocalBounds();
  const auto lanes = model_.lanes();
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  g.setColour(editor_theme::background());
  g.fillRect(area);
  // Lanes by chop, numbered so colour is never the only indicator.
  const float w = static_cast<float>(area.getWidth());
  const float laneH = static_cast<float>(lanes.laneHeight > 0.0 ? lanes.laneHeight : area.getHeight());
  g.setColour(cdl::pal().edge.withAlpha(0.55f));  // lane separators
  for (int lane = 1; lane < lanes.lanes; ++lane) {
    const int y = static_cast<int>(cf::ui::laneTop(lanes, lane));
    if (y >= 0 && y < area.getHeight()) g.drawHorizontalLine(y, 0.0f, w);
  }
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
  const bool ghosting = dragActive_ && dragMode_ != DragMode::None && !dragRefused_;
  for (const auto& r : pattern.rects) {
    const auto rect = rectFor(r);
    if (rect.getBottom() < 0.0f || rect.getY() > static_cast<float>(area.getHeight())) continue;
    const bool muted = !r.enabled;
    float alpha = r.depth > 0 ? 0.95f : 0.75f;
    if (muted) alpha = 0.25f;
    if (ghosting && r.id == dragRect_.id && !dragCopy_) alpha = std::min(alpha, 0.35f);  // the original stays dimmed while its ghost moves
    g.setColour(laneColour(r.chopIndex).withAlpha(alpha));
    g.fillRect(rect);
    const bool selected = r.id == model_.selected() || std::find(selectedHits.begin(), selectedHits.end(), r.id) != selectedHits.end();
    if (selected) {
      g.setColour(cdl::pal().ink);
      g.drawRect(rect, 2.0f);
    } else if (muted) {  // dashed outline: the shape cue for muted, not only the lower alpha
      g.setColour(cdl::pal().inkMuted);
      const float dash[] = {3.0f, 2.0f};
      juce::Path outline;
      outline.addRectangle(rect.reduced(0.5f));
      juce::PathStrokeType stroke(1.0f);
      juce::Path dashed;
      stroke.createDashedStroke(dashed, outline, dash, 2);
      g.fillPath(dashed);
    } else if (r.locked || r.hasChild) {
      g.setColour(r.locked ? editor_theme::accentRed() : cdl::pal().ink);
      g.drawRect(rect, 1.0f);
    }
    if (rect.getWidth() > 14.0f && rect.getHeight() > 12.0f) {
      g.setColour(cdl::pal().bg);
      g.setFont(cdl::font(cdl::FontRole::Value, 0.9f * cdl::uiScale()));
      g.drawText(muted ? juce::String("m") : juce::String(r.chopIndex + 1), rect.toNearestInt(), juce::Justification::centred);
      if (r.hasFx && !muted && rect.getWidth() > 26.0f) {
        g.setFont(cdl::font(cdl::FontRole::MonoXs, cdl::uiScale()));
        g.drawText("fx", rect.toNearestInt().reduced(2, 1), juce::Justification::bottomRight);
      }
    }
  }
  if (pattern.rects.empty()) {
    g.setColour(cdl::pal().inkMuted);
    g.drawText("Detect chops, then press Generate.", area, juce::Justification::centred);
  }

  // A preview (the A/B morph, a playing scene) is not the pattern in the project: say so on the grid.
  if (v.previewLabel.isNotEmpty()) {
    const auto text = v.previewLabel;
    g.setFont(cdl::font(cdl::FontRole::Tag, cdl::uiScale()));
    const int tw = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 16;
    const auto badge = juce::Rectangle<int>(area.getRight() - tw - 6, area.getY() + 6, tw, static_cast<int>(18 * cdl::uiScale()));
    g.setColour(cdl::pal().sprayWash);
    g.fillRoundedRectangle(badge.toFloat(), 3.0f);
    g.setColour(cdl::pal().spray);
    g.drawRoundedRectangle(badge.toFloat().reduced(0.5f), 3.0f, 1.0f);
    g.setColour(cdl::pal().ink);
    g.drawText(text, badge, juce::Justification::centred, false);
  }

  // Ghost outline of the hit being dragged: it shows the snapped drop position; the edit is applied on release.
  if (ghosting) {
    float x = static_cast<float>(planStart_ + static_cast<cf::Ticks>(planBar_) * model_.view().barTicks) / static_cast<float>(std::max<cf::Ticks>(1, pattern.lengthTicks)) * w;
    float width = static_cast<float>(planDuration_) / static_cast<float>(std::max<cf::Ticks>(1, pattern.lengthTicks)) * w;
    float y = static_cast<float>(cf::ui::laneTop(lanes, planLane_)) + 2.0f;
    if (dragMode_ == DragMode::Resize) {
      x = static_cast<float>(dragRect_.x0) * w;
      y = static_cast<float>(cf::ui::laneTop(lanes, std::max(0, dragRect_.chopIndex))) + 2.0f;
    }
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawRect(x, y, std::max(2.0f, width), std::max(4.0f, laneH - 4.0f), 1.0f);
  }

  if (selecting_) {
    const auto rectangle = juce::Rectangle<int>(start_, end_);
    g.setColour(editor_theme::accentBlue().withAlpha(0.2f));
    g.fillRect(rectangle);
    g.setColour(editor_theme::accentBlue());
    g.drawRect(rectangle, 1);
  }

  if (lanes.scrollable()) {  // vertical scroll bar for the lanes
    const auto thumb = cf::ui::laneScrollThumb(lanes);
    g.setColour(cdl::pal().edge.withAlpha(0.6f));
    g.fillRect(scrollBarArea());
    g.setColour(cdl::pal().inkMuted);
    g.fillRoundedRectangle(static_cast<float>(getWidth()) - kScrollBarW + 1.0f, static_cast<float>(thumb.top), kScrollBarW - 2.0f, static_cast<float>(thumb.height), 3.0f);
  }
}

void PatternPanel::updateSelection(juce::Point<int> point) {
  end_ = {juce::jlimit(0, getWidth() - 1, point.x), juce::jlimit(0, getHeight() - 1, point.y)};
  const auto rect = juce::Rectangle<int>(start_, end_);
  const bool click = rect.getWidth() < 3 && rect.getHeight() < 3;
  model_.selectSectionsInPattern(rect, end_, click, getLocalBounds());
  repaint();
}

void PatternPanel::addHitAt(juce::Point<int> pt) {
  const auto& v = model_.view();
  const int lane = laneAt(static_cast<double>(pt.y));
  if (lane < 0 || v.gridTicks <= 0 || v.barTicks <= 0 || v.barCount <= 0) return;
  const cf::Ticks total = v.barTicks * v.barCount;
  cf::Ticks tick = cf::ui::xToTick(static_cast<double>(pt.x), static_cast<double>(getWidth()), total);
  if (model_.extras().ui.gridSnap) tick = cf::ui::snapDown(tick, cmd_.snapGridTicks());  // the cell under the pointer
  const auto pos = cf::ui::splitTick(tick, v.barTicks, v.barCount);
  cmd_.addHit(pos.bar, pos.startInBar, lane);
}

void PatternPanel::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  const auto& v = model_.view();
  selecting_ = dragActive_ = scrollDrag_ = dragRefused_ = false;
  dragMode_ = DragMode::None;
  grabEditorFocus();
  const auto bar = scrollBarArea();
  if (!bar.isEmpty() && bar.contains(pt.toFloat())) {  // drag the lane scroll bar
    const auto lanes = model_.lanes();
    const auto thumb = cf::ui::laneScrollThumb(lanes);
    scrollDrag_ = true;
    scrollGrab_ = (pt.y >= thumb.top && pt.y <= thumb.top + thumb.height) ? pt.y - thumb.top : thumb.height / 2.0;
    model_.setLaneScroll(cf::ui::scrollForThumbTop(lanes, pt.y - scrollGrab_));
    return;
  }
  if (e.mods.isPopupMenu()) {
    showContextMenu(pt);
    return;
  }
  if (const auto* hit = hitAt(pt)) {
    model_.clearSelection();
    model_.select(hit->id);
    dragRect_ = *hit;
    dragStart_ = dragNow_ = pt;
    dragLane_ = hit->chopIndex;
    const double w = static_cast<double>(std::max(1, getWidth()));
    const double edgePx = (hit->x1 - static_cast<double>(pt.x) / w) * w;
    const double edgeZone = std::min(6.0, (hit->x1 - hit->x0) * w / 3.0);  // narrow hits keep a move zone
    dragMode_ = edgePx <= edgeZone ? DragMode::Resize : DragMode::Move;
    const cf::Ticks total = std::max<cf::Ticks>(1, v.barTicks * v.barCount);
    grabOffset_ = cf::ui::xToTick(static_cast<double>(pt.x), w, total) - hit->startTick;
    dragCopy_ = e.mods.isAltDown() && dragMode_ == DragMode::Move;
    dragRefused_ = dragCopy_ ? !hit->topLevel : cmd_.hitGuard(hit->id).isNotEmpty();  // a locked hit can still be copied
    planBar_ = hit->bar;
    planStart_ = hit->startInBar;
    planDuration_ = hit->durationTick;
    planLane_ = hit->chopIndex;
    repaint();
    return;
  }
  model_.clearSelection();
  if (e.mods.isShiftDown()) {  // Shift-click on empty space adds a one-cell hit
    addHitAt(pt);
    repaint();
    return;
  }
  selecting_ = true;
  start_ = end_ = pt;
  updateSelection(pt);
}

void PatternPanel::mouseDrag(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  if (scrollDrag_) {
    const auto lanes = model_.lanes();
    model_.setLaneScroll(cf::ui::scrollForThumbTop(lanes, pt.y - scrollGrab_));
    return;
  }
  if (selecting_) {
    updateSelection(pt);
    return;
  }
  if (dragMode_ == DragMode::None) return;
  dragNow_ = pt;
  if (!dragActive_) {
    if (std::abs(pt.x - dragStart_.x) < kDragStartPx && std::abs(pt.y - dragStart_.y) < kDragStartPx) return;
    dragActive_ = true;
    if (dragRefused_) {
      model_.setStatus(cmd_.hitGuard(dragRect_.id).isNotEmpty() ? cmd_.hitGuard(dragRect_.id) : juce::String(EditorCommands::kMsgNested));
      setMouseCursor(juce::MouseCursor::NormalCursor);  // the status line says why
    }
  }
  if (dragRefused_) return;
  const auto& v = model_.view();
  dragSnap_ = snapOn(e.mods);
  const double w = static_cast<double>(std::max(1, getWidth()));
  if (dragMode_ == DragMode::Move) {
    cf::ui::HitMoveInput in;
    in.barTicks = v.barTicks;
    in.barCount = v.barCount;
    in.gridTicks = cmd_.snapGridTicks();
    in.snap = dragSnap_;
    in.hitDuration = dragRect_.durationTick;
    in.grabOffsetTicks = grabOffset_;
    in.pointerX = static_cast<double>(pt.x);
    in.widthPx = w;
    in.pointerY = static_cast<double>(pt.y);
    in.originalLane = dragLane_;
    const auto plan = cf::ui::planHitMove(in, model_.lanes());
    planBar_ = plan.bar;
    planStart_ = plan.startInBar;
    planLane_ = e.mods.isShiftDown() ? dragLane_ : plan.lane;  // Shift keeps the lane
    planDuration_ = std::min<cf::Ticks>(dragRect_.durationTick, v.barTicks - plan.startInBar);
  } else {
    planDuration_ = cf::ui::planHitResize(v.barTicks, v.barCount, cmd_.snapGridTicks(), dragSnap_, dragRect_.startInBar, dragRect_.bar, static_cast<double>(pt.x), w);
  }
  repaint();
}

void PatternPanel::mouseUp(const juce::MouseEvent& e) {
  if (scrollDrag_) {
    scrollDrag_ = false;
    return;
  }
  if (selecting_) {
    updateSelection(e.getPosition());
    selecting_ = false;
    model_.rebuild();
    return;
  }
  if (dragMode_ == DragMode::None) return;
  mouseDrag(e);  // makes sure the plan reflects the release position
  const DragMode mode = dragMode_;
  const bool active = dragActive_, refused = dragRefused_, copy = dragCopy_;
  const cf::ui::EventRect rect = dragRect_;
  const int bar = planBar_, lane = planLane_;
  const cf::Ticks start = planStart_, duration = planDuration_;
  const bool nearStart = std::abs(e.getPosition().x - dragStart_.x) <= kCancelPx && std::abs(e.getPosition().y - dragStart_.y) <= kCancelPx;
  dragMode_ = DragMode::None;
  dragActive_ = false;
  setMouseCursor(juce::MouseCursor::NormalCursor);
  repaint();
  if (!active || refused || nearStart) return;  // a click, a refused drag, or a drag brought back to its start
  if (mode == DragMode::Resize) {
    if (duration != rect.durationTick) cmd_.resizeHit(rect.id, duration);
    return;
  }
  if (copy) {
    cmd_.duplicateHit(rect.id, bar, start, lane == rect.chopIndex ? -1 : lane);
    return;
  }
  if (bar == rect.bar && start == rect.startInBar && lane == rect.chopIndex) return;
  cmd_.moveHit(rect.id, bar, start, lane == rect.chopIndex ? -1 : lane);
}

void PatternPanel::mouseMove(const juce::MouseEvent& e) {
  if (const auto* hit = hitAt(e.getPosition())) {
    if (!hit->topLevel || hit->locked) {
      setMouseCursor(juce::MouseCursor::NormalCursor);  // the status line says why
      return;
    }
    const double w = static_cast<double>(std::max(1, getWidth()));
    const double edgePx = (hit->x1 - static_cast<double>(e.x) / w) * w;
    setMouseCursor(edgePx <= std::min(6.0, (hit->x1 - hit->x0) * w / 3.0) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    return;
  }
  setMouseCursor(juce::MouseCursor::NormalCursor);
}

void PatternPanel::mouseDoubleClick(const juce::MouseEvent& e) {
  if (e.mods.isPopupMenu()) return;
  if (const auto* hit = hitAt(e.getPosition())) {
    model_.select(hit->id);
    if (onEditHit) onEditHit(hit->id);
    return;
  }
  addHitAt(e.getPosition());
}

void PatternPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
  const auto lanes = model_.lanes();
  if (!lanes.scrollable()) return;
  model_.setLaneScroll(model_.laneScroll() - static_cast<double>(wheel.deltaY) * 4.0 * lanes.laneHeight);
}

// ---------------------------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------------------------

juce::PopupMenu PatternPanel::buildHitMenu(const cf::ui::EventRect& r) const {
  juce::PopupMenu m;
  m.addItem(kEdit, "Edit hit...");
  m.addItem(kMute, r.enabled ? "Mute hit" : "Unmute hit");
  m.addItem(kDuplicate, "Duplicate hit");
  m.addItem(kLock, r.locked ? "Unlock hit" : "Lock hit");
  m.addSeparator();
  m.addItem(kReset, "Reset effects and transform");
  m.addSeparator();
  m.addItem(kDelete, "Delete hit");
  return m;
}

juce::PopupMenu PatternPanel::buildEmptyMenu() const {
  juce::PopupMenu m;
  m.addItem(kAdd, "Add hit here");
  return m;
}

void PatternPanel::showContextMenu(juce::Point<int> where) {
  const auto& v = model_.view();
  const int lane = laneAt(static_cast<double>(where.y));
  const cf::Ticks total = std::max<cf::Ticks>(1, v.barTicks * v.barCount);
  cf::Ticks tick = cf::ui::xToTick(static_cast<double>(where.x), static_cast<double>(std::max(1, getWidth())), total);
  if (model_.extras().ui.gridSnap) tick = cf::ui::snapDown(tick, cmd_.snapGridTicks());
  const auto pos = cf::ui::splitTick(tick, v.barTicks, v.barCount);
  juce::PopupMenu menu;
  cf::EventId id;
  if (const auto* hit = hitAt(where)) {
    model_.clearSelection();
    model_.select(hit->id);
    id = hit->id;
    menu = buildHitMenu(*hit);
  } else {
    menu = buildEmptyMenu();
  }
  juce::Component::SafePointer<PatternPanel> alive(this);
  const int bar = pos.bar;
  const cf::Ticks start = pos.startInBar;
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(juce::Rectangle<int>(where.x, where.y, 1, 1).translated(getScreenX(), getScreenY())),
                     [alive, id, bar, start, lane](int choice) {
                       if (alive != nullptr && choice != 0) alive->handleMenuChoice(choice, id, bar, start, lane);
                     });
}

void PatternPanel::handleMenuChoice(int choice, cf::EventId hit, int bar, cf::Ticks startInBar, int lane) {
  const auto& v = model_.view();
  const cf::ui::EventRect* rect = nullptr;
  for (const auto& r : v.pattern.rects)
    if (r.id == hit) rect = &r;
  switch (choice) {
    case kEdit:
      if (hit.valid() && onEditHit) onEditHit(hit);
      break;
    case kMute:
      if (rect) cmd_.setHitEnabled(hit, !rect->enabled);
      break;
    case kDuplicate:
      if (rect) {  // the copy goes right after the hit; at the end of the bar it starts the next bar, or lands on the original in the last one
        int toBar = rect->bar;
        cf::Ticks at = rect->startInBar + std::max<cf::Ticks>(cmd_.snapGridTicks(), rect->durationTick);
        if (at + cf::ui::kMinHitTicks > v.barTicks) {
          if (rect->bar + 1 < v.barCount) {
            toBar = rect->bar + 1;
            at = 0;
          } else {
            at = rect->startInBar;
          }
        }
        cmd_.duplicateHit(hit, toBar, at, -1);
      }
      break;
    case kLock:
      if (rect) cmd_.setHitLocked(hit, !rect->locked);
      break;
    case kReset:
      if (hit.valid()) cmd_.resetHit(hit);
      break;
    case kDelete:
      if (hit.valid()) cmd_.deleteHit(hit);
      break;
    case kAdd:
      cmd_.addHit(bar, startInBar, lane);
      break;
    default:
      break;
  }
}
