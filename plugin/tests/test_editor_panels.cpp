// Mouse routing: each canvas panel handles its own clicks in its own coordinates, driven through MouseSim.
#include <algorithm>
#include <cmath>
#include <memory>

#include "EditorModel.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
std::size_t markerCount(ChopFractalProcessor& p) {
  std::size_t n = 0;
  p.withSession([&](cf::composition::ProjectSession& s) { n = s.chopMap() ? s.chopMap()->markers().size() : 0; });
  return n;
}
}  // namespace

CHOP_TEST(history_panel_click_recalls_a_node_and_the_orbit_click_selects_the_same_hit_as_the_pattern) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  const auto generated = patternBytes(p);
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::MutateOptions o;
    o.seed = 77;
    o.amount = 0.9;
    CHECK(s.mutate(o).ok());
  });
  CHECK(patternBytes(p) != generated);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->setVisible(true);
  pump(300);
  auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get());
  CHECK(ed != nullptr);
  if (!ed) return;
  MouseSim sim(*editor);

  // History: click the node of the first Generate (the root of the family tree).
  EditorModel probe(p);
  probe.refresh(true, 920);
  const auto& tree = probe.view().tree;
  CHECK(tree.size() == 2);
  ed->openDrawer("tools");  // the family tree and the orbit view live in the Tools drawer
  pump(60);
  auto* history = findById(*editor, "HistoryPanel");
  CHECK(history != nullptr);
  if (!history || tree.size() != 2) return;
  const auto& root = *std::find_if(tree.begin(), tree.end(), [](const auto& n) { return !n.active; });
  const int stepX = probe.view().treeCols > 0 ? std::min(64, std::max(6, (history->getWidth() - 60) / probe.view().treeCols)) : 64;
  const int stepY = probe.view().treeRows > 0 ? std::min(30, std::max(6, (history->getHeight() - 40) / probe.view().treeRows)) : 30;
  sim.click(editor->getLocalPoint(history, juce::Point<int>(24 + root.column * stepX, 22 + root.depth * stepY)));
  pump(100);
  CHECK(patternBytes(p) == generated);

  // Pattern click, then Orbit click: both write the one selection the editor exposes.
  const auto rects = ed->hitRects();
  CHECK(rects.size() > 1);
  if (rects.size() < 2) return;
  const auto b = ed->patternBounds();
  const float laneH = static_cast<float>(b.getHeight()) / static_cast<float>(std::max(1, ed->laneCount()));
  const auto& first = rects.front();
  sim.click({b.getX() + static_cast<int>((first.x0 + (first.x1 - first.x0) * 0.5) * b.getWidth()), b.getY() + static_cast<int>((static_cast<float>(first.chopIndex) + 0.5f) * laneH)});
  CHECK(ed->selectedHit() == first.id);

  auto* orbit = findById(*editor, "OrbitPanel");
  CHECK(orbit != nullptr);
  if (!orbit) return;
  if (auto* tools = dynamic_cast<juce::Viewport*>(findById(*editor, "ToolsDrawer"))) tools->setViewPosition(2000, 400);  // scroll to the far corner (clamped) so the orbit is in view
  pump(30);
  const auto& view = probe.view().orbit;
  const double R = std::min(orbit->getWidth(), orbit->getHeight()) / 2.0 - 4.0;
  const auto centre = orbit->getLocalBounds().getCentre();
  bool clickedAnother = false;
  for (const auto& arc : view.arcs) {
    if (arc.id == first.id) continue;
    const auto radii = cf::ui::arcRadii(view, arc, R);
    const double r = (radii.inner + radii.outer) / 2.0, a = arc.startAngle + arc.sweep / 2.0;
    const juce::Point<int> local(centre.x + static_cast<int>(std::lround(r * std::sin(a))), centre.y - static_cast<int>(std::lround(r * std::cos(a))));
    sim.click(editor->getLocalPoint(orbit, local));
    if (ed->selectedHit() == arc.id) {
      clickedAnother = true;
      break;
    }
  }
  CHECK(clickedAnother);
  CHECK(ed->selectedHit() != first.id);
}

CHOP_TEST(wave_panel_shift_click_adds_a_marker_alt_click_removes_it_and_double_click_auditions) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->setVisible(true);
  pump(300);
  auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get());
  CHECK(ed != nullptr);
  if (!ed) return;
  MouseSim sim(*editor);
  const auto wave = ed->waveformBounds();
  const std::size_t before = markerCount(p);
  const juce::Point<int> spot(wave.getX() + 57, wave.getCentreY());  // between two existing markers

  sim.click(spot, juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
  pump(100);
  CHECK(markerCount(p) == before + 1);

  sim.click(spot + juce::Point<int>(3, 0), juce::ModifierKeys(juce::ModifierKeys::altModifier));
  pump(100);
  CHECK(markerCount(p) == before);

  // A plain click only selects (and auditions); it never edits the project.
  const auto history = [&] {
    std::size_t n = 0;
    p.withSession([&](cf::composition::ProjectSession& s) { n = s.history().size(); });
    return n;
  };
  const std::size_t nodes = history();
  sim.click(spot);
  sim.doubleClick(spot);
  pump(100);
  CHECK(markerCount(p) == before);
  CHECK(history() == nodes);
  CHECK(!ed->selectedChops().empty());
}
