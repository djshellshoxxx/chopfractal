// Arrangement drawer (F18 section 12): the section list built from scenes, bars and repeats, the 8 to 32 bar rule, the
// chain and auto fill buttons, and the export with its confirmation of the output names.
#include "perform_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

namespace {
juce::Slider* findSlider(juce::Component& root, const juce::String& title) {
  juce::Slider* found = nullptr;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* s = dynamic_cast<juce::Slider*>(&c))
      if (s->getTitle() == title) found = s;
  });
  return found;
}
}  // namespace

CHOP_TEST(arrangement_drawer_builds_sections_from_scenes_and_enforces_the_bar_rule) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  auto& perform = f.editor->perform();
  f.editor->openDrawer("scenes");
  auto& scenes = perform.scenes();
  click(scenes, "Capture Scene");
  f.editor->commands().mutate();
  click(scenes, "Capture Scene");
  CHECK(f.editor->openDrawer("arrange"));
  auto& panel = perform.arrangement();
  panel.refreshNow();
  checkDescribed(panel, "arrange");
  CHECK(panel.list().count() == 0 && !findButton(panel, "Export Arrangement")->isEnabled());
  CHECK(findButton(panel, "Add Section")->isEnabled());

  click(panel, "Add Section");
  click(panel, "Add Section");
  panel.refreshNow();
  CHECK(panel.list().count() == 2 && panel.list().selected() == 1);
  CHECK(findButton(panel, "Export Arrangement")->isEnabled() == panel.view().valid);  // export follows the 8 to 32 bar rule
  CHECK(panel.view().valid || !panel.view().statusText.empty());

  // Repeats lengthen a section until the total reaches the 8 to 32 bar range.
  panel.select(0);
  auto* repeats = findSlider(panel, "Section repeats");
  CHECK(repeats != nullptr);
  if (repeats) repeats->setValue(repeats->getMaximum(), juce::sendNotificationSync);
  panel.select(1);
  if (repeats) repeats->setValue(repeats->getMaximum(), juce::sendNotificationSync);
  panel.refreshNow();
  const int total = [&] {
    int t = 0;
    f.proc.withSession([&](cf::composition::ProjectSession& s) { t = s.arrangement().totalBars(); });
    return t;
  }();
  CHECK(panel.view().valid == (total >= 8 && total <= 32));

  // Order and removal.
  panel.select(1);
  click(panel, "Section Up");
  CHECK(panel.selectedRow() == 0);
  click(panel, "Remove Section");
  CHECK(panel.list().count() == 1);

  // Chain and auto fill produce valid, editable lists.
  click(panel, "Use scene chain");
  CHECK(panel.list().count() >= 1);
  click(panel, "Auto fill 16 bars");
  panel.refreshNow();
  CHECK(panel.view().valid && panel.list().count() >= 1);
  CHECK(findButton(panel, "Export Arrangement")->isEnabled());
}

CHOP_TEST(arrangement_export_writes_files_and_deleting_a_scene_removes_its_section) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  auto& perform = f.editor->perform();
  const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_g4_arr_" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30)));
  perform.commands().exportFolder = [dir] { return dir; };
  f.editor->openDrawer("scenes");
  auto& scenes = perform.scenes();
  click(scenes, "Capture Scene");
  f.editor->commands().mutate();
  click(scenes, "Capture Scene");
  f.editor->openDrawer("arrange");
  auto& panel = perform.arrangement();
  click(panel, "Auto fill 16 bars");
  panel.refreshNow();
  CHECK(panel.view().valid);
  auto* name = findTextEditor(panel, "Arrangement name");
  CHECK(name != nullptr);
  if (name) name->setText("verse", false);
  click(panel, "Export Arrangement");
  CHECK(waitFor([&] {
    perform.commands().background();
    return !perform.commands().arrangementState().running;
  }, 20000));
  panel.refreshNow();
  CHECK(f.editor->model().statusLine().startsWith("Arrangement exported"));
  CHECK(dir.getChildFile("verse_arrangement.wav").existsAsFile());
  dir.deleteRecursively();

  // Deleting a scene that a section uses takes the section out and says so.
  const int before = panel.list().count();
  f.editor->openDrawer("scenes");
  scenes.select(0);
  click(scenes, "Delete Scene");
  f.editor->openDrawer("arrange");
  panel.refreshNow();
  CHECK(before > 0 && !panel.view().valid && !findButton(panel, "Export Arrangement")->isEnabled());
  CHECK(panel.view().rows[0].find("Scene deleted") != std::string::npos || panel.view().rows.back().find("Scene deleted") != std::string::npos);
}
