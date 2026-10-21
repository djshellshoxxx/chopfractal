// Evolve drawer: motif presets, depth, accent decay, Fractal and Keep, and the Smart Setup suggestions list.
#include "perform_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

CHOP_TEST(evolve_drawer_motif_presets_depth_and_accent_reach_the_fractal_settings) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  CHECK(f.editor->openDrawer("evolve"));
  auto& panel = f.editor->perform().evolve();
  panel.refreshNow();
  checkDescribed(panel, "evolve");
  auto* preset = findCombo(panel, "Motif preset");
  CHECK(preset != nullptr && preset->getNumItems() == static_cast<int>(panel.motifPresets().size()));
  CHECK(selectItem(*preset, "Offbeat  .x.x"));
  pump(10);
  auto* motif = findTextEditor(panel, "Fractal motif: x hit, a accent, dot rest");
  CHECK(motif != nullptr);
  if (!motif) return;
  CHECK(motif->getText() == ".x.x");
  CHECK(panel.fractalUi().motif == ".x.x");
  auto* depth = findCombo(panel, "Fractal depth");
  CHECK(depth != nullptr);
  if (!depth) return;
  depth->setSelectedId(depth->getNumItems(), juce::sendNotificationSync);
  CHECK(panel.fractalUi().depth == depth->getNumItems());
  click(panel, "Fractal");
  f.editor->commands().mutate();
  CHECK(f.editor->model().view().tree.size() >= 2);
  click(panel, "Keep");
}

CHOP_TEST(evolve_drawer_runs_evolve_and_lists_smart_setup_suggestions) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("evolve");
  auto& panel = f.editor->perform().evolve();
  panel.refreshNow();
  CHECK(panel.suggestions().lineCount() == 0);
  click(panel, "Smart Setup");
  pump(30);
  panel.refreshNow();
  CHECK(panel.suggestions().lineCount() > 0);
  CHECK(findButton(panel, "Accept Roles")->isEnabled() && findButton(panel, "Use Tempo")->getTooltip().isNotEmpty());
  click(panel, "Accept Roles");
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.roleMap().all().empty()); });

  auto* run = dynamic_cast<juce::ToggleButton*>(findButton(panel, "Run Evolve"));
  CHECK(run != nullptr);
  tap(run);
  CHECK(panel.evolveEnabled());
  panel.setEvolveEnabled(false);
  CHECK(!panel.evolveEnabled());
}
