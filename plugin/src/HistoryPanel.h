#pragma once
// Variation family tree: click a node to recall it, drag it into a DAW as MIDI (Alt: audio), right-click for the node menu.
#include <functional>

#include "EditorPanels.h"

class HistoryPanel : public EditorPanelBase, public juce::TooltipClient {
 public:
  HistoryPanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  juce::String getTooltip() override;
  // For tests: what the last node drag produced (empty when none started).
  juce::File lastDragFile() const { return lastDragFile_; }
  chopfractal::history::NodeId pendingNode() const { return model_.pendingNode(); }
  // Test seam: when set, called with the dragged file instead of handing it to the operating system.
  std::function<void(const juce::File&)> onDragFile;
  std::function<void(chopfractal::history::NodeId)> onNode;  // the node under a click (the drawer shows its details)

 private:
  void modelChanged(Change) override;
  // A press on a node recalls it on release; moving beyond the drag threshold first drags the variation out as a MIDI file (Alt: WAV).
  chopfractal::history::NodeId pressNode_ = chopfractal::history::kNoNode;
  bool dragStarted_ = false, pressAlt_ = false;
  juce::File lastDragFile_;
  juce::Point<int> treePos(const chopfractal::ui::TreeNodeView&) const;
};
