#pragma once
// History drawer (G2): the family tree with the details of the clicked variation (name, favorite, delete), Compare thumbnails of
// the active and the clicked variation, and the A/B comparison slots with the morph between them. Everything routes through the
// editor commands; the tree itself is the HistoryPanel (click recalls a variation, right-click opens its menu).
#include <chopfractal/plugin_ui_adapter/views.hpp>

#include "EditorPanels.h"
#include "HistoryPanel.h"

class HistoryDrawer : public EditorPanelBase {
 public:
  HistoryDrawer(EditorModel&, EditorCommands&, HistoryPanel& tree);
  void resized() override;
  void paint(juce::Graphics&) override;
  chopfractal::history::NodeId selectedNode() const { return selected_; }

 private:
  // A miniature of one pattern: lanes by chop, hits as bars.
  struct Thumb : juce::Component {
    chopfractal::ui::PatternView view;
    bool valid = false;
    int lanes = 1;
    juce::String caption;
    void paint(juce::Graphics&) override;
  };
  void modelChanged(Change) override;
  void refresh();
  void selectNode(chopfractal::history::NodeId);
  void layoutBody();

  HistoryPanel& tree_;
  juce::Viewport viewport_, treeView_;
  juce::Component body_;
  juce::Label nodeTitle_, nodeInfo_, compareText_, abInfo_, treeEmpty_;
  juce::TextEditor name_;
  juce::TextButton rename_{"Rename"}, recall_{"Recall"}, delete_{"Delete branch"}, compare_{"Compare"};
  juce::ToggleButton favorite_{"Favorite"};
  Thumb activeThumb_, selectedThumb_;
  juce::TextButton keepA_{"Keep as A"}, goA_{"Go to A"}, keepB_{"Keep as B"}, goB_{"Go to B"};
  juce::Slider morph_;
  juce::TextButton startMorph_{"Start morph"}, commitMorph_{"Keep morph"}, cancelMorph_{"Cancel morph"};
  chopfractal::history::NodeId selected_ = chopfractal::history::kNoNode, shownThumb_ = chopfractal::history::kNoNode, shownActive_ = chopfractal::history::kNoNode;
  bool syncing_ = false;
};
