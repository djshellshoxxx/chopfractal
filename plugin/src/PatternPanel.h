#pragma once
// Pattern grid: one lane per chop, lanes scroll vertically when there are more than fit. Direct editing (G2 / F02):
//   Press a hit        select it; move the pointer 4 px to drag it (time and lane), the last 6 px resize it
//   Alt-drag           copy the hit to the drop position;  Shift-drag keeps it in its lane;  Cmd/Ctrl bypasses snapping
//   Double-click       a hit opens the inspector, empty lane space adds a hit;  Shift-click on empty space adds one too
//   Right-click        hit menu (edit, mute, duplicate, lock, reset, delete) or "Add hit here"
//   Drag on empty      rubber-band selection of hits;  wheel scrolls the lanes
// Every finished gesture is one undo step (EditorCommands::editHits).
#include <functional>
#include <memory>

#include "EditorPanels.h"

class PatternPanel : public EditorPanelBase {
 public:
  PatternPanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseMove(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;
  void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
  void resized() override;
  // Starts / stops the per-frame playhead refresh (VBlank); no repaint source exists while the transport is stopped.
  void setPlaying(bool on);

  // Set by the editor: a double-click on a hit and the menu's "Edit hit..." open the inspector for it.
  std::function<void(chopfractal::EventId)> onEditHit;

  // Menus are built here so tests can read the items. Ids: see MenuId.
  juce::PopupMenu buildHitMenu(const chopfractal::ui::EventRect&) const;
  juce::PopupMenu buildEmptyMenu() const;
  void handleMenuChoice(int choice, chopfractal::EventId hit, int bar, chopfractal::Ticks startInBar, int lane);

  // Geometry (panel pixels) shared with the tests.
  const chopfractal::ui::EventRect* hitAt(juce::Point<int>) const;      // topmost hit under the point, nested hits included
  juce::Rectangle<float> rectFor(const chopfractal::ui::EventRect&) const;
  int laneAt(double y) const;
  bool dragging() const { return dragMode_ != DragMode::None && dragActive_; }

 private:
  enum MenuId { kEdit = 1, kMute, kDuplicate, kLock, kReset, kDelete, kAdd };
  struct PlayheadLayer : juce::Component {
    explicit PlayheadLayer(EditorModel& m) : model(m) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics&) override;
    EditorModel& model;
  };
  PlayheadLayer playhead_;
  std::unique_ptr<juce::VBlankAttachment> vblank_;

  enum class DragMode { None, Move, Resize };

  void modelChanged(Change) override;
  void updateSelection(juce::Point<int> point);
  void showContextMenu(juce::Point<int>);
  void addHitAt(juce::Point<int>);
  bool snapOn(const juce::ModifierKeys&) const;
  juce::Rectangle<float> scrollBarArea() const;

  bool selecting_ = false;
  juce::Point<int> start_, end_;
  DragMode dragMode_ = DragMode::None;
  bool dragActive_ = false;        // the pointer moved far enough to count as a drag
  bool scrollDrag_ = false;
  double scrollGrab_ = 0.0;
  chopfractal::ui::EventRect dragRect_;
  juce::Point<int> dragStart_, dragNow_;
  chopfractal::Ticks grabOffset_ = 0;
  bool dragCopy_ = false, dragSnap_ = true, dragLaneLock_ = false;
  bool dragRefused_ = false;
  int dragLane_ = 0;
  // The planned drop (ghost) while a drag is active.
  int planBar_ = 0, planLane_ = 0;
  chopfractal::Ticks planStart_ = 0, planDuration_ = 0;
};
