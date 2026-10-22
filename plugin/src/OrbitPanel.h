#pragma once
// Orbit View: one ring per chop with the playhead sweeping clockwise. Clicking a hit selects it; dragging a top-level hit moves it
// along time (angle to tick, snapped like the pattern grid) and to another ring (chop), with a ghost while dragging and one
// undo step on release; Shift keeps the ring and Cmd or Ctrl turns the snap off. A right-click opens the hit menu.
#include <functional>

#include "EditorPanels.h"

class OrbitPanel : public EditorPanelBase {
 public:
  OrbitPanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;

  std::function<void(chopfractal::EventId)> onEditHit;  // "Edit hit..." in the menu: the shell opens the Inspector

  // The drop a drag would make right now (the ghost): empty when no drag is in progress. For tests.
  struct Plan {
    bool active = false;
    int bar = 0;
    chopfractal::Ticks startInBar = 0;
    int ring = 0;
  };
  const Plan& plan() const { return plan_; }

 private:
  void modelChanged(Change) override;
  void updatePlan(const juce::MouseEvent&);
  void showMenu(chopfractal::EventId);
  juce::Point<double> fromCentre(juce::Point<int>) const;
  double radius() const;

  chopfractal::ui::OrbitArc dragArc_;
  chopfractal::EventId dragId_;
  juce::Point<int> dragStart_;
  bool dragRefused_ = false;
  chopfractal::Ticks grabTicks_ = 0;
  Plan plan_;
};
