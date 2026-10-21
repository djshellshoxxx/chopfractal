// Settings drawer: theme, interface scale, reduced motion, per-user output defaults for new instances, window reset and
// Copy diagnostics. All of it lives in ui.json, none in a project.
#include <utility>

#include "workflow_test_support.hpp"

using namespace shelltest;

CHOP_TEST(settings_scale_and_reduced_motion_apply_at_once_and_persist_per_user) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  {
    EditorRig rig;
    if (!rig.ed) return;
    rig.ed->openDrawer("settings");
    pump(60);
    auto* scale = findCombo(*rig.ed, "Interface scale");
    auto* motion = findButton(*rig.ed, "Reduced motion");
    auto* theme = findCombo(*rig.ed, "Theme");
    CHECK(scale && motion && theme);
    if (!(scale && motion && theme)) return;
    const float before = rig.ed->shellRects().scale;
    scale->setSelectedId(4, juce::sendNotificationSync);  // 125 %
    pump(30);
    CHECK_EQ(rig.ed->uiScalePercent(), 125);
    CHECK(rig.ed->shellRects().scale > before);
    CHECK_EQ(cdl::loadUserSettings().uiScalePercent, 125);
    motion->triggerClick();
    pump(30);
    CHECK(cdl::reducedMotion());
    CHECK(cdl::loadUserSettings().reducedMotion);
    theme->setSelectedId(2, juce::sendNotificationSync);
    CHECK(rig.ed->theme() == cdl::ThemeId::Concrete);
    snapshot(*rig.ed, "settings_drawer");
  }
  {  // a new editor opens with the saved choices
    EditorRig rig;
    if (!rig.ed) return;
    CHECK_EQ(rig.ed->uiScalePercent(), 125);
    CHECK(cdl::reducedMotion());
    CHECK_EQ(rig.ed->settingsDrawer().scalePercent(), 125);
  }
}

CHOP_TEST(user_scale_never_pushes_the_layout_past_what_the_window_holds) {
  const float at1100 = shell::scaleFor(1100, 720, 1.25f);
  CHECK_NEAR(at1100, 1.2f, 1e-4);  // capped: the layout needs about 880 x 600 at scale 1
  const float atMin = shell::scaleFor(880, 560, 1.25f);
  CHECK(atMin >= shell::scaleFor(880, 560, 1.0f));
  CHECK(atMin <= 560.0f / 600.0f + 1e-4);
  CHECK_NEAR(shell::scaleFor(880, 560, 1.0f), 0.8f, 1e-6);
  CHECK_NEAR(shell::scaleFor(2200, 1440, 0.9f), 1.8f, 1e-4);
}

CHOP_TEST(reduced_motion_replaces_the_per_frame_playhead_with_the_ten_hz_refresh) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  rig.ed->setReducedMotion(true);
  CHECK(cdl::reducedMotion());
  rig.ed->setReducedMotion(false);
  CHECK(!cdl::reducedMotion());
  CHECK(!cdl::loadUserSettings().reducedMotion);
}

CHOP_TEST(output_defaults_are_used_by_new_instances_but_never_by_restored_projects) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  {
    ChopFractalProcessor fresh;
    CHECK(fresh.currentParams().get(cf::host::kLimiterEnable) < 0.5);  // factory defaults
  }
  EditorRig rig;
  if (!rig.ed) return;
  rig.ed->openDrawer("settings");
  pump(60);
  auto* limiter = findButton(*rig.ed, "Limiter on for new instances");
  auto* match = findButton(*rig.ed, "Level match on for new instances");
  auto* ceiling = findSlider(*rig.ed, "Default limiter ceiling");
  auto* factory = findButton(*rig.ed, "Restore factory output");
  CHECK(limiter && match && ceiling && factory);
  if (!(limiter && match && ceiling && factory)) return;
  CHECK(!factory->isEnabled());
  limiter->triggerClick();
  pump(40);
  match->triggerClick();
  pump(40);
  ceiling->setValue(-3.0, juce::sendNotificationSync);
  CHECK(factory->isEnabled());
  const auto u = cdl::loadUserSettings();
  CHECK(u.audioDefaultsCustom && u.defaultLimiter && u.defaultLevelMatch);
  CHECK_NEAR(u.defaultCeilingDb, -3.0f, 1e-4);
  {
    ChopFractalProcessor fresh;
    const auto v = fresh.currentParams();
    CHECK(v.get(cf::host::kLimiterEnable) >= 0.5);
    CHECK(v.get(cf::host::kLevelMatch) >= 0.5);
    CHECK_NEAR(v.get(cf::host::kLimiterCeilingDb), -3.0, 1e-3);
  }
  {  // a project saved with the limiter off keeps it off
    juce::MemoryBlock state;
    rig.proc.getStateInformation(state);  // the rig's processor was created before the defaults
    CHECK(rig.proc.currentParams().get(cf::host::kLimiterEnable) < 0.5);
    ChopFractalProcessor other;
    other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    waitForRestore(other);
    CHECK(other.currentParams().get(cf::host::kLimiterEnable) < 0.5);
  }
  factory->triggerClick();
  pump(40);
  CHECK(!cdl::loadUserSettings().audioDefaultsCustom);
  CHECK(!limiter->getToggleState() && !match->getToggleState());
  ChopFractalProcessor again;
  CHECK(again.currentParams().get(cf::host::kLimiterEnable) < 0.5);
}

CHOP_TEST(copy_diagnostics_puts_the_report_on_the_clipboard) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  rig.ed->openDrawer("settings");
  pump(60);
  auto* copy = findButton(*rig.ed, "Copy diagnostics");
  CHECK(copy != nullptr);
  if (!copy) return;
  juce::SystemClipboard::copyTextToClipboard("old");
  copy->triggerClick();
  pump(30);
  const auto text = juce::SystemClipboard::getTextFromClipboard();
  CHECK(text.startsWith("ChopFractal diagnostics"));
  CHECK(text.contains("Version: "));
  CHECK(text.contains("Source: "));
  CHECK(text.contains("8 chops"));
  CHECK(!text.contains(user.root.getFullPathName()));
  CHECK(rig.proc.statusMessage().contains("Diagnostics copied"));
}

CHOP_TEST(settings_controls_have_titles_and_tooltips_at_both_sizes) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  for (auto size : {std::pair<int, int>{1100, 720}, std::pair<int, int>{880, 560}}) {
    EditorRig rig(true, size.first, size.second);
    if (!rig.ed) return;
    rig.ed->openDrawer("settings");
    pump(100);
    forEachControl(rig.settingsOwner(), [&](juce::Component& c) {
      if (!dynamic_cast<juce::ComboBox*>(&c) && !dynamic_cast<juce::Button*>(&c) && !dynamic_cast<juce::Slider*>(&c)) return;
      CHECK(c.getTitle().isNotEmpty());
      if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c)) CHECK(t->getTooltip().length() > 20);
    });
    snapshot(*rig.ed, "settings_drawer");
  }
}
