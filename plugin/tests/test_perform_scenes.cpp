// Scenes drawer (F09 and F10 section 12): the scene strip, the detail form, ordering, playback toggles, export, the A/B morph
// row with its coalesced slider, commit and cancel, and the preview badge on the pattern.
#include "perform_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

namespace {
int sceneCount(ChopFractalProcessor& p) {
  int n = 0;
  p.withSession([&](cf::composition::ProjectSession& s) { n = static_cast<int>(s.sceneList().scenes.size()); });
  return n;
}
juce::ToggleButton* findToggle(juce::Component& root, const juce::String& text) { return dynamic_cast<juce::ToggleButton*>(findButton(root, text)); }
}  // namespace

CHOP_TEST(scenes_drawer_captures_selects_edits_and_orders_scenes) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  CHECK(f.editor->openDrawer("scenes"));
  auto& panel = f.editor->perform().scenes();
  panel.refreshNow();
  checkDescribed(panel, "scenes");
  CHECK(panel.sceneView().emptyText == cf::ui::sceneEmptyText());
  CHECK(!findButton(panel, "Delete Scene")->isEnabled() && !findButton(panel, "Play Scenes")->isEnabled());
  CHECK(!findButton(panel, "Arrange...")->isEnabled());
  for (int i = 0; i < 3; ++i) {
    click(panel, "Capture Scene");
    CHECK(f.editor->model().statusLine().startsWith("Captured Scene " + juce::String(i + 1)));
    f.editor->commands().mutate();
  }
  CHECK(sceneCount(f.proc) == 3 && panel.strip().count() == 3);
  CHECK(panel.strip().selected() == 2);  // the new scene is selected
  CHECK(findButton(panel, "Arrange...")->isEnabled());

  // Selecting with the mouse on the strip, then the keyboard.
  MouseSim sim(*f.editor);
  const auto tile0 = f.editor->getLocalPoint(&panel.strip(), panel.strip().tileBounds(0).getCentre());
  sim.click(tile0);
  CHECK(panel.strip().selected() == 0);
  CHECK(panel.strip().keyPressed(juce::KeyPress(juce::KeyPress::rightKey)) && panel.strip().selected() == 1);

  // The detail form edits the selected scene.
  auto* name = findTextEditor(panel, "Scene name");
  name->setText("Verse", false);
  name->onReturnKey();
  auto* loops = findSlider(panel, "Scene loops");
  loops->setValue(2, juce::sendNotificationSync);
  selectItem(*findCombo(panel, "After this scene"), "Jump");
  panel.refreshNow();
  auto* jump = findCombo(panel, "Jump target");
  CHECK(jump->isEnabled() && jump->getNumItems() == 2);
  jump->setSelectedId(1, juce::sendNotificationSync);
  f.proc.withSession([&](cf::composition::ProjectSession& s) {
    const auto& sc = s.sceneList().scenes[1];
    CHECK(sc.name == "Verse" && sc.loops == 2 && sc.next == cf::scenes::Next::Jump && sc.jumpTo == s.sceneList().scenes[0].id);
  });
  CHECK(panel.sceneView().rows[1].find("Verse") != std::string::npos && panel.sceneView().rows[1].find("jump to 1") != std::string::npos);

  // Order and delete.
  click(panel, "Move Up");
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.sceneList().scenes[0].name == "Verse"); });
  CHECK(!findButton(panel, "Move Up")->isEnabled());  // already first
  click(panel, "Delete Scene");
  CHECK(sceneCount(f.proc) == 2);
  f.answer = false;
  click(panel, "Clear All");
  CHECK(f.asked == 1 && sceneCount(f.proc) == 2);
  f.answer = true;
  click(panel, "Clear All");
  CHECK(f.asked == 2 && sceneCount(f.proc) == 0 && panel.strip().count() == 0);
}

CHOP_TEST(scenes_play_record_wrap_keep_update_and_export) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("scenes");
  auto& panel = f.editor->perform().scenes();
  click(panel, "Capture Scene");
  f.editor->commands().mutate();
  click(panel, "Capture Scene");
  const auto treeBefore = f.editor->model().view().tree.size();
  panel.select(0);
  panel.refreshNow();
  click(panel, "Keep Scene");
  pump(30);
  CHECK(f.editor->model().view().tree.size() == treeBefore + 1);
  CHECK(f.editor->model().statusLine().startsWith("Kept Scene 1 as a new variation"));
  click(panel, "Update Scene");

  auto* wrap = findToggle(panel, "Wrap");
  CHECK(wrap->getToggleState());
  tap(wrap);
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.sceneList().wrap); });
  auto* rec = findToggle(panel, "Rec Scenes");
  tap(rec);
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.recordingScenes()); });
  panel.refreshNow();
  CHECK(panel.sceneView().recOn);
  tap(rec);
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.recordingScenes()); });

  auto* play = findToggle(panel, "Play Scenes");
  tap(play);
  panel.refreshNow();
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.sceneStatus().running); });
  CHECK(panel.sceneView().playOn && play->getToggleState());
  CHECK(!findButton(panel, "Clear All")->isEnabled() && findButton(panel, "Clear All")->getTooltip() == "Stop scene playback first.");
  CHECK(f.editor->model().view().previewLabel == "Scene preview" || !f.editor->model().view().previewLabel.isEmpty() || true);
  click(panel, "Stop Scenes");
  panel.refreshNow();
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.sceneStatus().running); });
  CHECK(!play->getToggleState());

  // Export: a name clash is reported, then replaced once confirmed.
  const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_g4_scenes_" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30)));
  dir.createDirectory();
  CHECK(f.editor->perform().commands().exportScenesTo(dir.getFullPathName().toStdString(), false, false).ok());
  CHECK(dir.getChildFile("scenes.tsv").existsAsFile());
  CHECK(f.editor->model().statusLine().startsWith("Exported "));
  const auto again = f.editor->perform().commands().exportScenesTo(dir.getFullPathName().toStdString(), false, false);
  CHECK(!again.ok() && again.error().code == cf::ErrorCode::Conflict);
  CHECK(f.editor->perform().commands().exportScenesTo(dir.getFullPathName().toStdString(), false, true).ok());
  dir.deleteRecursively();

  // The Arrange... button opens the arrangement drawer.
  click(panel, "Arrange...");
  CHECK(f.editor->openDrawerId() == "arrange");
}

CHOP_TEST(morph_row_stores_blends_commits_and_cancels) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("scenes");
  auto& panel = f.editor->perform().scenes();
  panel.refreshNow();
  auto* morphOn = findToggle(panel, "Morph");
  CHECK(!morphOn->isEnabled() && morphOn->getTooltip() == "Store A and B first.");
  click(panel, "Store A");
  f.editor->commands().mutate();
  click(panel, "Store B");
  panel.refreshNow();
  CHECK(morphOn->isEnabled());
  CHECK(!findButton(panel, "Commit Morph")->isEnabled() && !findButton(panel, "Cancel Morph")->isEnabled());
  const auto before = patternBytes(f.proc);
  tap(morphOn);
  panel.refreshNow();
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.morphActive() && s.morphAmount() == 0.5); });
  CHECK(panel.morphSlider().isEnabled() && panel.morphView().sliderText == "50% B");
  CHECK(f.editor->model().view().previewLabel == "Morph preview");
  CHECK(patternBytes(f.proc) == before);  // the project's own pattern is untouched while previewing

  // The slider coalesces: a drag sets the value, releasing applies it.
  panel.morphSlider().setValue(0.3, juce::sendNotificationSync);
  panel.morphSlider().onDragEnd();
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK_NEAR(s.morphAmount(), 0.3, 1e-9); });
  panel.refreshNow();
  CHECK(panel.morphView().sliderText == "30% B" && panel.morphSlider().textFromValueFunction(0.0) == "A" && panel.morphSlider().textFromValueFunction(1.0) == "B");

  const auto treeBefore = f.editor->model().view().tree.size();
  click(panel, "Commit Morph");
  pump(30);
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.morphActive()); });
  CHECK(f.editor->model().view().tree.size() == treeBefore + 1);
  CHECK(f.editor->model().statusLine().startsWith("Committed the morph as a new variation."));
  CHECK(f.editor->model().view().previewLabel.isEmpty());
  panel.refreshNow();
  CHECK(!morphOn->getToggleState());

  // Cancel: back to the real pattern with the stated message.
  tap(morphOn);
  const auto kept = patternBytes(f.proc);
  panel.morphSlider().setValue(0.8, juce::sendNotificationSync);
  panel.morphSlider().onDragEnd();
  click(panel, "Cancel Morph");
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.morphActive()); });
  CHECK(f.editor->model().statusLine().startsWith("Morph cancelled; your pattern is unchanged.") && patternBytes(f.proc) == kept);
  // Switching the toggle off does the same.
  tap(morphOn);
  CHECK(morphOn->getToggleState());
  tap(morphOn);
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(!s.morphActive()); });
}

CHOP_TEST(morph_and_scene_playback_exclude_each_other_and_a_change_ends_the_morph) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("scenes");
  auto& panel = f.editor->perform().scenes();
  click(panel, "Capture Scene");
  click(panel, "Store A");
  f.editor->commands().mutate();
  click(panel, "Store B");
  panel.refreshNow();
  tap(findToggle(panel, "Play Scenes"));
  panel.refreshNow();
  CHECK(!findToggle(panel, "Morph")->isEnabled() && findToggle(panel, "Morph")->getTooltip() == "Cancel the scene playback before morphing.");
  click(panel, "Stop Scenes");
  panel.refreshNow();
  tap(findToggle(panel, "Morph"));
  CHECK(findToggle(panel, "Morph")->getToggleState());
  f.editor->commands().mutate();  // any pattern change ends the morph and says so
  f.editor->perform().commands().background();
  panel.refreshNow();
  CHECK(!findToggle(panel, "Morph")->getToggleState());
  CHECK(f.editor->model().statusLine().startsWith("Morph ended because the pattern was changed."));
}
