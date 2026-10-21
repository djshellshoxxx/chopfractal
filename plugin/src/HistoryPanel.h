#pragma once
// Variation family tree: click a node to recall it, right-click for the node menu.
#include <functional>

#include "EditorPanels.h"

class HistoryPanel : public EditorPanelBase {
 public:
  HistoryPanel(EditorModel&, EditorCommands&);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  chopfractal::history::NodeId pendingNode() const { return model_.pendingNode(); }
  std::function<void(chopfractal::history::NodeId)> onNode;  // the node under a click (the drawer shows its details)

 private:
  void modelChanged(Change) override;
  juce::Point<int> treePos(const chopfractal::ui::TreeNodeView&) const;
};
