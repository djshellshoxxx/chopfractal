// History drawer (G2): node details, favorite, rename, compare, A/B slots and the morph, through the real editor.
#include <algorithm>
#include <memory>

#include "EditorModel.h"
#include "HistoryDrawer.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "workflow_test_support.hpp"

using namespace shelltest;

CHOP_TEST(history_drawer_details_favorite_rename_compare_and_ab_morph) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::MutateOptions o;
    o.seed = 77;
    o.amount = 0.9;
    CHECK(s.mutate(o).ok());
  });
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->setSize(1100, 720);
  editor->setVisible(true);
  pump(300);
  auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get());
  CHECK(ed != nullptr);
  if (!ed) return;
  MouseSim sim(*editor);
  CHECK(ed->openDrawer("history"));
  pump(100);
  auto* drawer = dynamic_cast<HistoryDrawer*>(findById(*editor, "HistoryDrawer"));
  auto* history = findById(*editor, "HistoryPanel");
  CHECK(drawer && history);
  if (!drawer || !history) return;

  EditorModel probe(p);
  probe.refresh(true, 920);
  const auto& tree = probe.view().tree;
  CHECK(tree.size() == 2);
  if (tree.size() != 2) return;
  const auto root = *std::find_if(tree.begin(), tree.end(), [](const auto& n) { return !n.active; });
  const int stepX = probe.view().treeCols > 0 ? std::min(64, std::max(6, (history->getWidth() - 60) / probe.view().treeCols)) : 64;
  const int stepY = probe.view().treeRows > 0 ? std::min(30, std::max(6, (history->getHeight() - 40) / probe.view().treeRows)) : 30;

  // Nothing clicked yet: the node buttons are disabled.
  auto* fav = findButton(*editor, "Favorite");
  auto* rename = findButton(*editor, "Rename");
  auto* compare = findButton(*editor, "Compare");
  CHECK(fav && rename && compare);
  if (!fav || !rename || !compare) return;
  CHECK(!rename->isEnabled());

  sim.click(editor->getLocalPoint(history, juce::Point<int>(24 + root.column * stepX, 22 + root.depth * stepY)));
  pump(120);
  CHECK(drawer->selectedNode() == root.id);
  CHECK(rename->isEnabled());

  fav->triggerClick();
  pump(80);
  cf::history::NodeSummary info;
  EditorModel m2(p);
  m2.refresh(true, 920);
  CHECK(m2.nodeSummary(root.id, info));
  CHECK(info.favorite);

  auto* name = findTextEditor(*editor, "Variation name");
  CHECK(name != nullptr);
  if (!name) return;
  name->setText("first take", true);
  rename->triggerClick();
  pump(80);
  CHECK(m2.nodeSummary(root.id, info));
  CHECK(info.label == "first take");

  compare->triggerClick();
  pump(60);

  // A/B: slots start empty, so Go to / morph are disabled; keeping fills them.
  auto* keepA = findButton(*editor, "Keep as A");
  auto* goA = findButton(*editor, "Go to A");
  auto* keepB = findButton(*editor, "Keep as B");
  auto* start = findButton(*editor, "Start morph");
  CHECK(keepA && goA && keepB && start);
  if (!keepA || !goA || !keepB || !start) return;
  CHECK(!goA->isEnabled());
  CHECK(!start->isEnabled());
  keepA->triggerClick();
  pump(60);
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::MutateOptions o;
    o.seed = 5;
    o.amount = 0.8;
    CHECK(s.mutate(o).ok());
  });
  pump(200);
  auto* keepB2 = findButton(*editor, "Keep as B");
  CHECK(keepB2 != nullptr);
  if (keepB2) keepB2->triggerClick();
  pump(80);
  auto* goA2 = findButton(*editor, "Go to A");
  auto* start2 = findButton(*editor, "Start morph");
  CHECK(goA2 && start2);
  if (!goA2 || !start2) return;
  CHECK(goA2->isEnabled());
  CHECK(start2->isEnabled());
  start2->triggerClick();
  pump(80);
  CHECK(m2.snapshotState().morphActive);
  auto* cancel = findButton(*editor, "Cancel morph");
  CHECK(cancel != nullptr);
  if (cancel) cancel->triggerClick();
  pump(80);
  CHECK(!m2.snapshotState().morphActive);
}


// MouseSim reports a zero drag distance, so a drag past the threshold is sent to the panel by hand.
static void dragNode(juce::Component& panel, juce::Point<int> at, juce::ModifierKeys mods) {
  auto src = juce::Desktop::getInstance().getMainMouseSource();
  const auto now = juce::Time::getCurrentTime();
  auto ev = [&](juce::Point<int> pt) {
    return juce::MouseEvent(src, pt.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &panel, &panel, now, at.toFloat(), now, 1, true);
  };
  panel.mouseDown(ev(at));
  panel.mouseDrag(ev(at + juce::Point<int>(40, 30)));
  panel.mouseUp(ev(at + juce::Point<int>(40, 30)));
}

// F13: a family-tree node drags out as that variation's MIDI (Alt: WAV) without recalling it; a click still recalls it.
CHOP_TEST(history_node_drag_exports_that_variation_and_a_click_still_recalls_it) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::MutateOptions o;
    o.seed = 91;
    o.amount = 0.9;
    CHECK(s.mutate(o).ok());
  });
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->setSize(1100, 720);
  editor->setVisible(true);
  pump(300);
  auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get());
  CHECK(ed != nullptr);
  if (!ed) return;
  CHECK(ed->openDrawer("history"));
  pump(100);
  auto& panel = ed->historyPanel();
  juce::File dragged;
  panel.onDragFile = [&](const juce::File& f) { dragged = f; };
  EditorModel probe(p);
  probe.refresh(true, 920);
  const auto& tree = probe.view().tree;
  CHECK(tree.size() == 2);
  if (tree.size() != 2) return;
  const auto root = *std::find_if(tree.begin(), tree.end(), [](const auto& n) { return !n.active; });
  const auto active = *std::find_if(tree.begin(), tree.end(), [](const auto& n) { return n.active; });
  const int stepX = probe.view().treeCols > 0 ? std::min(64, std::max(6, (panel.getWidth() - 60) / probe.view().treeCols)) : 64;
  const int stepY = probe.view().treeRows > 0 ? std::min(30, std::max(6, (panel.getHeight() - 40) / probe.view().treeRows)) : 30;
  const juce::Point<int> at(24 + root.column * stepX, 22 + root.depth * stepY);
  const auto rootPt = editor->getLocalPoint(&panel, at);
  MouseSim sim(*editor);

  // A drag: that node's MIDI file, and the active variation did not change.
  dragNode(panel, at, {});
  pump(60);
  CHECK(dragged.existsAsFile() && dragged.getFileExtension() == ".mid");
  std::vector<std::uint8_t> expected;
  cf::history::NodeId activeNow = 0;
  p.withSession([&](cf::composition::ProjectSession& s) {
    activeNow = s.history().active();
    auto r = s.renderVariationMidi(root.id, ed->commands().midiOptions());
    CHECK(r.ok());
    if (r.ok()) expected = r.value();
  });
  CHECK(activeNow == active.id);
  juce::MemoryBlock got;
  dragged.loadFileAsData(got);
  CHECK(got.getSize() == expected.size() && std::memcmp(got.getData(), expected.data(), expected.size()) == 0);

  // Alt-drag: the rendered audio.
  dragged = juce::File();
  juce::ModifierKeys alt(juce::ModifierKeys::altModifier);
  dragNode(panel, at, alt);
  pump(60);
  CHECK(dragged.existsAsFile() && dragged.getFileExtension() == ".wav");
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.history().active() == active.id); });

  // A plain click recalls the node.
  sim.click(rootPt);
  pump(150);
  p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.history().active() == root.id); });
  CHECK(panel.getTooltip().isNotEmpty());
}
