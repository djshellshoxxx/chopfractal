#pragma once
// Source waveform with chop markers (G2 / F02 direct editing).
//   Click           select and audition the chop under the pointer; drag across sections to select several
//   Drag a marker   move it (snaps to a transient or zero crossing, Cmd/Ctrl bypasses); one undo step per drag
//   Shift-click     add a marker;  double-click on empty waveform adds one too;  Alt-click removes the nearest
//   Right-click     enable / disable, rename, trim, snap mode, delete (on a marker); add marker, fit (elsewhere)
//   Wheel           zoom around the pointer;  Shift+wheel or horizontal wheel scrolls;  middle-drag pans
#include "EditorPanels.h"

class WavePanel : public EditorPanelBase {
 public:
  WavePanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseMove(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;
  void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

  // Menus are built here so tests can read the items; menuMarker is the marker under the pointer (invalid = none).
  juce::PopupMenu buildMarkerMenu(chopfractal::ChopId, std::int64_t atFrame) const;
  juce::PopupMenu buildEmptyMenu() const;
  // Applies a menu result (ids as built above). `atFrame` is where the menu was opened.
  void handleMenuChoice(int choice, chopfractal::ChopId marker, std::int64_t atFrame);
  void showContextMenu(juce::Point<int>);

  // Geometry helpers (also used by the tests).
  chopfractal::ChopId markerAtX(double x, double tolerancePx = 6.0) const;  // the nearest visible marker within the tolerance
  std::int64_t frameAtX(double x) const;
  double framesPerPixel() const;
  bool draggingMarker() const { return dragging_; }
  std::int64_t dragFrame() const { return dragFrame_; }

 private:
  enum MenuId { kEnable = 1, kRename, kTrimStart, kTrimEnd, kClearTrim, kSnapOff, kSnapTransient, kSnapZero, kDelete, kAdd, kFit };

  void modelChanged(Change) override;
  void updateSelection(juce::Point<int> point);
  chopfractal::ui::ViewWindow window() const;
  void askRename(chopfractal::ChopId);
  juce::Rectangle<float> scrollThumb() const;

  bool selecting_ = false;
  juce::Point<int> start_, end_;
  chopfractal::ChopId dragMarker_;
  bool dragging_ = false, panning_ = false, scrollDrag_ = false;
  int pressX_ = 0;
  std::int64_t dragFrame_ = 0;
  double panAnchorX_ = 0.0;
  double thumbGrab_ = 0.0;
  chopfractal::ChopId hoverMarker_;
};
