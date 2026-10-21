// Outputs drawer (F11 section 12): the mode box, per-role and per-chop bus choices, the auto routing buttons and the
// off-in-host marking, driven through the real processor with host aux outputs enabled.
#include "perform_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

namespace {
bool enableAux(ChopFractalProcessor& p, int n) {
  auto layout = p.getBusesLayout();
  for (int i = 1; i <= n; ++i) layout.outputBuses.getReference(i) = juce::AudioChannelSet::stereo();
  if (!p.setBusesLayout(layout)) return false;
  p.prepareToPlay(48000.0, 512);
  return true;
}
void giveRoles(ChopFractalProcessor& p) {
  p.withSession([](cf::composition::ProjectSession& s) {
    const auto chops = s.chops();
    if (!chops) return;
    const char* names[] = {"kick", "snare", "hat"};
    for (std::size_t i = 0; i < chops->chops.size() && i < 6; ++i) (void)s.assignRole(chops->chops[i].id, names[i % 3]);
  });
}
cf::composition::OutputRouting routing(ChopFractalProcessor& p) {
  cf::composition::OutputRouting r;
  p.withSession([&](cf::composition::ProjectSession& s) { r = s.outputRouting(); });
  return r;
}
}  // namespace

CHOP_TEST(outputs_drawer_without_a_source_says_what_is_missing) {
  if (!haveDisplay()) return;
  PerformFixture f(false);
  CHECK(f.editor->openDrawer("outputs"));
  auto& panel = f.editor->perform().outputs();
  panel.refreshNow();
  CHECK(!panel.view().enabled && !panel.view().emptyText.empty());
  CHECK(!findButton(panel, "Auto: by role")->isEnabled() && !findButton(panel, "Reset to Main")->isEnabled());
}

CHOP_TEST(outputs_drawer_routes_by_role_by_chop_and_per_row) {
  if (!haveDisplay()) return;
  ChopFractalProcessor proc;
  CHECK(enableAux(proc, 2));
  makeSession(proc);
  giveRoles(proc);
  ChopFractalEditor editor(proc);
  editor.setSize(1100, 720);
  CHECK(editor.openDrawer("outputs"));
  auto& panel = editor.perform().outputs();
  panel.refreshNow();
  checkDescribed(panel, "outputs");
  CHECK(panel.view().enabled && panel.view().activeAux == 2 && panel.view().modeSelected == 0);
  CHECK(!panel.view().roleRows.empty() && !panel.view().chopRows.empty());
  CHECK(panel.view().resetEnabled == false && panel.view().autoByRoleEnabled);

  click(panel, "Auto: by role");
  CHECK(routing(proc).mode == cf::composition::RoutingMode::ByRole);
  panel.refreshNow();
  CHECK(panel.view().modeSelected == 1 && panel.view().resetEnabled);

  // A row change writes one role to the bus; the combobox shows it.
  auto* kick = panel.roleCombo("kick");
  CHECK(kick != nullptr);
  if (kick) {
    kick->setSelectedId(2, juce::sendNotificationSync);  // Out 2
    CHECK(routing(proc).roleBus.at("kick") == 1);
    kick->setSelectedId(4, juce::sendNotificationSync);  // Out 4: the host enabled only two
    CHECK(routing(proc).roleBus.at("kick") == 3);
    panel.refreshNow();
    CHECK(kick->getItemText(3).contains("(off in host)") && kick->getTooltip().length() > 20);
  }

  click(panel, "Auto: by chop");
  CHECK(routing(proc).mode == cf::composition::RoutingMode::ByChop);
  auto* chop = panel.chopCombo(1);
  CHECK(chop != nullptr);
  if (chop) {
    chop->setSelectedId(3, juce::sendNotificationSync);
    panel.refreshNow();
    CHECK(panel.view().chopRows[0].choice.selected == 2);
    if (chop->getNumItems() >= 10) chop->setSelectedId(10, juce::sendNotificationSync);  // Role default clears the override
  }

  // The mode box itself: Main only resets everything.
  editor.perform().outputs().refreshNow();
  click(panel, "Reset to Main");
  CHECK(routing(proc).mode == cf::composition::RoutingMode::MainOnly);
  panel.refreshNow();
  CHECK(panel.view().modeSelected == 0 && !panel.view().resetEnabled);
}
