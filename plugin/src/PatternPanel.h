#pragma once
// Pattern grid: one lane per chop. Click selects a hit, a drag selects hits with a rubber band, and with
// "Move / resize hits" on a drag moves or resizes one hit (applied on mouse-up as one undo step).
#include <memory>

#include "EditorPanels.h"

class PatternPanel : public EditorPanelBase {
 public:
  PatternPanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void resized() override;
  // Starts / stops the per-frame playhead refresh (VBlank); no repaint source exists while the transport is stopped.
  void setPlaying(bool on);

 private:
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

  bool selecting_ = false;
  juce::Point<int> start_, end_;
  DragMode dragMode_ = DragMode::None;
  chopfractal::EventId dragId_;
  juce::Point<int> dragStart_, dragNow_;
  int dragLane_ = 0;
  bool dragCopy_ = false;
};
