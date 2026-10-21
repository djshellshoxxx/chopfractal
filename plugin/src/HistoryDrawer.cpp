#include "HistoryDrawer.h"

#include <initializer_list>

#include "CdlLookAndFeel.h"
#include "EditorTheme.h"

namespace cf = chopfractal;

void HistoryDrawer::Thumb::paint(juce::Graphics& g) {
  const auto area = getLocalBounds().toFloat();
  g.setColour(cdl::pal().bg);
  g.fillRect(area);
  g.setColour(cdl::pal().edge);
  g.drawRect(area, 1.0f);
  if (!valid) {
    g.setColour(cdl::pal().inkMuted);
    g.setFont(cdl::font(cdl::FontRole::Hint, cdl::uiScale()));
    g.drawText("no pattern", getLocalBounds(), juce::Justification::centred);
    return;
  }
  const float laneH = area.getHeight() / static_cast<float>(std::max(1, lanes));
  for (const auto& r : view.rects) {
    if (r.depth != 0 || r.chopIndex < 0) continue;
    g.setColour(editor_theme::laneColour(r.chopIndex).withAlpha(r.userOwned ? 1.0f : 0.85f));
    g.fillRect(juce::Rectangle<float>(area.getX() + static_cast<float>(r.x0) * area.getWidth(), area.getY() + static_cast<float>(r.chopIndex) * laneH + 1.0f,
                                      std::max(2.0f, static_cast<float>(r.x1 - r.x0) * area.getWidth() - 1.0f), std::max(2.0f, laneH - 2.0f)));
  }
}

HistoryDrawer::HistoryDrawer(EditorModel& model, EditorCommands& commands, HistoryPanel& tree) : EditorPanelBase(model, commands, "HistoryDrawer"), tree_(tree) {
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&body_, false);
  viewport_.setScrollBarsShown(true, false);
  viewport_.setComponentID("HistoryViewport");
  treeView_.setViewedComponent(&tree_, false);
  treeView_.setScrollBarsShown(true, true);
  treeView_.setComponentID("FamilyTreeViewport");
  body_.addAndMakeVisible(treeView_);
  tree_.onNode = [this](cf::history::NodeId id) { selectNode(id); };

  for (juce::Label* l : {&nodeTitle_, &nodeInfo_, &compareText_, &abInfo_}) {
    l->setInterceptsMouseClicks(false, false);
    body_.addAndMakeVisible(*l);
  }
  compareText_.setJustificationType(juce::Justification::topLeft);
  name_.setTitle("Variation name");
  name_.setTooltip("Type a new name for the variation and press Rename");
  name_.setInputRestrictions(120);
  name_.onReturnKey = [this] { rename_.onClick(); };
  body_.addAndMakeVisible(name_);
  rename_.setTooltip("Give the clicked variation this name");
  rename_.onClick = [this] {
    if (selected_ != cf::history::kNoNode) cmd_.renameNode(selected_, name_.getText().trim().toStdString());
  };
  recall_.setTooltip("Return to the clicked variation (clicking a dot in the tree does the same)");
  recall_.onClick = [this] {
    if (selected_ != cf::history::kNoNode) cmd_.activateNode(selected_);
  };
  delete_.setTooltip("Remove the clicked variation and everything that grew from it; favorites and the active one are kept");
  delete_.onClick = [this] {
    if (selected_ != cf::history::kNoNode) cmd_.deleteBranch(selected_);
  };
  compare_.setTooltip("Compare the active variation with the clicked one: event counts and the bars that differ");
  compare_.onClick = [this] {
    if (selected_ == cf::history::kNoNode) return;
    const auto active = model_.activeNode();
    compareText_.setText(cmd_.compareText(active, selected_), juce::dontSendNotification);
  };
  favorite_.setTitle("Favorite");
  favorite_.setTooltip("Protect the variation from being pruned or deleted with its branch");
  favorite_.onClick = [this] {
    if (!syncing_ && selected_ != cf::history::kNoNode) cmd_.setNodeFavorite(selected_, favorite_.getToggleState());
  };
  for (juce::Component* c : std::initializer_list<juce::Component*>{&rename_, &recall_, &delete_, &compare_, &favorite_, &activeThumb_, &selectedThumb_}) body_.addAndMakeVisible(*c);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&rename_, &recall_, &delete_, &compare_}) b->setTitle(b->getButtonText());

  keepA_.setTooltip("Store the current pattern in comparison slot A (saved with the project)");
  keepA_.onClick = [this] { cmd_.storeSnapshot(0); };
  goA_.setTooltip("Recall comparison pattern A as the current pattern");
  goA_.onClick = [this] { cmd_.recallSnapshot(0); };
  keepB_.setTooltip("Store the current pattern in comparison slot B (saved with the project)");
  keepB_.onClick = [this] { cmd_.storeSnapshot(1); };
  goB_.setTooltip("Recall comparison pattern B as the current pattern");
  goB_.onClick = [this] { cmd_.recallSnapshot(1); };
  morph_.setSliderStyle(juce::Slider::LinearHorizontal);
  morph_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
  morph_.setRange(0.0, 1.0, 0.01);
  morph_.setTitle("Morph A to B");
  morph_.setTooltip("Blend from pattern A (left) to pattern B (right); the blend plays but is not saved until you keep it");
  morph_.textFromValueFunction = [](double v) { return juce::String(static_cast<int>(std::lround(v * 100.0))) + " %"; };
  morph_.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.").getDoubleValue() / 100.0; };
  morph_.onValueChange = [this] {
    if (syncing_) return;
    if (!model_.snapshotState().morphActive) cmd_.beginMorph(morph_.getValue());
    else cmd_.setMorph(morph_.getValue());
  };
  startMorph_.setTooltip("Start blending A and B at the slider position");
  startMorph_.onClick = [this] { cmd_.beginMorph(morph_.getValue()); };
  commitMorph_.setTooltip("Freeze the blend as a new variation in the family tree");
  commitMorph_.onClick = [this] { cmd_.commitMorph(); };
  cancelMorph_.setTooltip("Drop the blend and return to the real pattern");
  cancelMorph_.onClick = [this] { cmd_.cancelMorph(); };
  for (juce::Component* c : std::initializer_list<juce::Component*>{&keepA_, &goA_, &keepB_, &goB_, &morph_, &startMorph_, &commitMorph_, &cancelMorph_}) body_.addAndMakeVisible(*c);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&keepA_, &goA_, &keepB_, &goB_, &startMorph_, &commitMorph_, &cancelMorph_}) b->setTitle(b->getButtonText());
  treeEmpty_.setText("", juce::dontSendNotification);
  refresh();
}

void HistoryDrawer::modelChanged(Change c) {
  if (c == Change::View) refresh();
}

void HistoryDrawer::selectNode(cf::history::NodeId id) {
  selected_ = id;
  shownThumb_ = cf::history::kNoNode;
  compareText_.setText({}, juce::dontSendNotification);
  refresh();
}

void HistoryDrawer::refresh() {
  const auto& v = model_.view();
  // Keep the tree's size in step with its shape: one step per column and row.
  const int cols = std::max(1, v.treeCols), rows = std::max(1, v.treeRows);
  tree_.setSize(std::max(treeView_.getWidth() - 14, 60 + cols * 64), std::max(110, 40 + rows * 30));
  tree_.repaint();
  bool have = false;
  for (const auto& n : v.tree)
    if (n.id == selected_) have = true;
  if (!have) selected_ = cf::history::kNoNode;
  syncing_ = true;
  cf::history::NodeSummary info;
  const bool known = selected_ != cf::history::kNoNode && model_.nodeSummary(selected_, info);
  nodeTitle_.setText(known ? juce::String(info.label.empty() ? "Variation " + std::to_string(info.id) : info.label) : juce::String("No variation clicked"), juce::dontSendNotification);
  nodeInfo_.setText(known ? "Seed " + juce::String(static_cast<juce::int64>(info.seed)) + ", " + juce::String(static_cast<int>(info.childCount)) + (info.childCount == 1 ? " branch" : " branches") + ", " +
                                juce::String(static_cast<int>((info.payloadBytes + 512) / 1024)) + " KB"
                          : juce::String("Click a dot in the tree to see it here."),
                    juce::dontSendNotification);
  favorite_.setToggleState(known && info.favorite, juce::dontSendNotification);
  if (known && !name_.hasKeyboardFocus(true)) name_.setText(juce::String(info.label), false);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&name_, &rename_, &recall_, &delete_, &compare_, &favorite_}) c->setEnabled(known);
  // thumbnails: the active variation and the clicked one
  const auto active = model_.activeNode();
  if (active != shownActive_ || shownThumb_ != selected_ || v.revision != 0) {
    activeThumb_.valid = active != cf::history::kNoNode && model_.nodePatternView(active, activeThumb_.view);
    selectedThumb_.valid = known && model_.nodePatternView(selected_, selectedThumb_.view);
    activeThumb_.lanes = selectedThumb_.lanes = std::max(1, static_cast<int>(v.chops.size()));
    shownActive_ = active;
    shownThumb_ = selected_;
    activeThumb_.repaint();
    selectedThumb_.repaint();
  }
  const auto st = model_.snapshotState();
  keepA_.setButtonText(st.hasA ? "Re-keep A" : "Keep as A");
  keepB_.setButtonText(st.hasB ? "Re-keep B" : "Keep as B");
  goA_.setEnabled(st.hasA);
  goB_.setEnabled(st.hasB);
  const bool both = st.hasA && st.hasB;
  morph_.setEnabled(both);
  startMorph_.setEnabled(both && !st.morphActive);
  commitMorph_.setEnabled(st.morphActive);
  cancelMorph_.setEnabled(st.morphActive);
  if (st.morphActive) morph_.setValue(st.morphAmount, juce::dontSendNotification);
  abInfo_.setText(both ? (st.morphActive ? "Morphing; keep it to add it to the tree." : "Both slots are stored.") : "Keep a pattern as A and another as B to compare or blend them.", juce::dontSendNotification);
  syncing_ = false;
}

void HistoryDrawer::paint(juce::Graphics& g) { g.fillAll(cdl::pal().panel); }

void HistoryDrawer::resized() {
  viewport_.setBounds(getLocalBounds());
  layoutBody();
  refresh();
}

void HistoryDrawer::layoutBody() {
  const int w = std::max(300, viewport_.getMaximumVisibleWidth());
  const int pad = 10;
  int y = 8;
  auto row = [&](int h) {
    juce::Rectangle<int> r(pad, y, w - 2 * pad, h);
    y += h + 4;
    return r;
  };
  treeView_.setBounds(row(176));
  nodeTitle_.setBounds(row(22));
  nodeInfo_.setBounds(row(18));
  auto r = row(28);
  name_.setBounds(r.removeFromLeft(r.getWidth() - 84));
  r.removeFromLeft(4);
  rename_.setBounds(r);
  r = row(28);
  recall_.setBounds(r.removeFromLeft(r.getWidth() / 3 - 3));
  r.removeFromLeft(4);
  favorite_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 2));
  r.removeFromLeft(4);
  delete_.setBounds(r);
  y += 6;
  compare_.setBounds(row(28).removeFromLeft(120));
  auto t = row(70);
  activeThumb_.setBounds(t.removeFromLeft(t.getWidth() / 2 - 4));
  t.removeFromLeft(8);
  selectedThumb_.setBounds(t);
  auto cap = row(16);
  (void)cap;
  compareText_.setBounds(row(34));
  y += 6;
  abInfo_.setBounds(row(18));
  r = row(28);
  keepA_.setBounds(r.removeFromLeft(r.getWidth() / 4 - 3));
  r.removeFromLeft(4);
  goA_.setBounds(r.removeFromLeft(r.getWidth() / 3 - 3));
  r.removeFromLeft(4);
  keepB_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 2));
  r.removeFromLeft(4);
  goB_.setBounds(r);
  morph_.setBounds(row(28));
  r = row(28);
  startMorph_.setBounds(r.removeFromLeft(r.getWidth() / 3 - 3));
  r.removeFromLeft(4);
  commitMorph_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 2));
  r.removeFromLeft(4);
  cancelMorph_.setBounds(r);
  body_.setSize(w, y + 8);
}
