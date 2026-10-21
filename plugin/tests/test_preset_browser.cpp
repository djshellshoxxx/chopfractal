// Preset browser drawer: factory break styles, search and scopes, preview vs load, user presets (save, rename, delete),
// favorites, undo and the protected output controls.
#include <chopfractal/composition/preset_apply.hpp>

#include "workflow_test_support.hpp"

using namespace shelltest;

namespace {
void setParam(ChopFractalProcessor& p, const char* id, float plain) {
  auto* prm = p.apvts.getParameter(id);
  prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}
}  // namespace

CHOP_TEST(browser_lists_the_factory_break_styles_and_filters_by_scope_and_search) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& b = rig.ed->presetBrowser();
  CHECK_EQ(b.items().size(), cf::presets::factoryPresets().size());
  CHECK_EQ(b.visibleRows().size(), 17u);
  b.setQuery("jungle");
  CHECK(b.visibleRows().size() == 2 && b.items()[static_cast<std::size_t>(b.visibleRows()[0])].category == "Jungle");
  b.setQuery("stutter edit");
  CHECK_EQ(b.visibleRows().size(), 4u);
  b.setQuery("");
  b.setScope(cf::ui::PresetScope::User);
  CHECK(b.visibleRows().empty());
  b.setScope(cf::ui::PresetScope::Favorites);
  CHECK(b.visibleRows().empty());
  b.setScope(cf::ui::PresetScope::All);
  CHECK_EQ(b.visibleRows().size(), 17u);
}

CHOP_TEST(up_and_down_preview_without_applying_and_enter_loads_a_factory_preset) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& b = rig.ed->presetBrowser();
  setParam(rig.proc, "dry_mix", 0.4f);
  setParam(rig.proc, "output_gain_db", -6.0f);
  const double densityBefore = rig.proc.currentParams().get(cf::host::kDensity);
  rig.ed->openDrawer("presets");
  pump(60);
  CHECK(b.isVisible() && rig.ed->openDrawerId() == "presets");
  CHECK(b.selectKey("factory:Amen Classic"));
  CHECK(b.moveSelection(1));
  CHECK(b.selectedKey() == "factory:Funky Drummer");
  CHECK(b.detailsText().contains("Amen / Funk"));
  CHECK_NEAR(rig.proc.currentParams().get(cf::host::kDensity), densityBefore, 1e-9);  // browsing changed nothing
  CHECK(b.loadSelected());
  const auto& boom = cf::presets::factoryPresets()[1];
  const auto v = rig.proc.currentParams();
  CHECK_NEAR(v.get(cf::host::kDensity), boom.density, 1e-3);
  CHECK_EQ(v.get(cf::host::kBreakProfile), 0.0);
  CHECK_EQ(v.get(cf::host::kEngineVersion), 1.0);
  CHECK_NEAR(v.get(cf::host::kDryMix), 0.4, 1e-3);          // protected controls stay
  CHECK_NEAR(v.get(cf::host::kOutputGainDb), -6.0, 1e-3);
  CHECK_EQ(rig.ed->model().seed(), boom.seed);
  CHECK(b.currentName() == "Funky Drummer");
}

CHOP_TEST(styles_select_their_engine_profile_and_stutter_edit_presets_switch_the_effects_on) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& b = rig.ed->presetBrowser();
  struct Case {
    const char* key;
    double profile;
  } cases[] = {{"factory:Amen Classic", 0}, {"factory:Jungle Roll", 1}, {"factory:Rolling Two-Step", 2}, {"factory:Breakcore Storm", 3}, {"factory:IDM Grid Fracture", 4}, {"factory:Boom Bap Basics", 5}};
  for (const auto& c : cases) {
    CHECK(b.loadKey(c.key));
    CHECK_EQ(rig.proc.currentParams().get(cf::host::kBreakProfile), c.profile);
  }
  CHECK(b.loadKey("factory:Stutter Edit Glitch"));
  const auto v = rig.proc.currentParams();
  CHECK_EQ(v.get(cf::host::kAllowStutter), 1.0);
  CHECK_EQ(v.get(cf::host::kAllowRoll), 1.0);
  CHECK_NEAR(v.get(cf::host::kStutterAmount), 90.0, 1e-3);
  CHECK(!rig.ed->model().extras().motif.empty() && rig.ed->model().extras().gridDivision == 32);
  rig.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.rules().rules.empty()); });
  CHECK(b.loadKey("factory:Boom Bap Basics"));
  rig.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK_EQ(s.rules().rules.size(), 2u); });
  CHECK(rig.proc.currentParams().get(cf::host::kAllowStutter) == 1.0);  // Boom Bap does not mention stutter, so the old value stays
}

CHOP_TEST(apply_and_generate_builds_a_pattern_and_undo_restores_the_controls) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig(false);
  if (!rig.ed) return;
  auto& b = rig.ed->presetBrowser();
  CHECK(patternBytes(rig.proc).empty());
  setParam(rig.proc, "density", 0.33f);
  rig.ed->model().setSeed(5);
  CHECK(b.loadKey("factory:Jungle Roll"));
  CHECK(patternBytes(rig.proc).empty());  // a preset alone never generates
  CHECK_NEAR(rig.proc.currentParams().get(cf::host::kDensity), 0.80, 1e-3);
  CHECK(b.undoLast());
  CHECK_NEAR(rig.proc.currentParams().get(cf::host::kDensity), 0.33, 1e-3);
  CHECK_EQ(rig.ed->model().seed(), 5u);
  CHECK(b.loadKey("factory:Jungle Roll", true));
  CHECK(!patternBytes(rig.proc).empty());
  CHECK(rig.proc.statusMessage().contains("applied and generated"));
}

CHOP_TEST(user_presets_save_rename_delete_and_favorites_persist) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& b = rig.ed->presetBrowser();
  setParam(rig.proc, "chaos", 0.7f);
  CHECK(b.loadKey("factory:Mashed Amen"));
  rig.ed->model().setSeed(99);
  CHECK(b.saveCurrent("My Mash", false));
  CHECK(b.selectedKey() == "user:my_mash");
  CHECK_EQ(b.items().size(), 18u);
  CHECK(!b.saveCurrent("My Mash", false));  // exists
  CHECK(rig.proc.statusMessage().contains("already exists"));
  CHECK(b.saveCurrent("My Mash", true));
  CHECK(!b.saveCurrent("bad/name", false));
  CHECK(rig.proc.statusMessage().contains("cannot contain"));
  // The saved file carries the engine profile, effects, seed and rules.
  const auto entries = PresetManager::list();
  const auto& mine = entries.back();
  CHECK(!mine.factory && mine.preset.seed == 99 && mine.preset.useSeed && mine.preset.category == "User");
  bool hasRoll = false;
  for (const auto& pv : mine.preset.params) hasRoll = hasRoll || (pv.id == "allow_roll" && pv.value == 1.0);
  CHECK(hasRoll);
  // Rename keeps the content.
  CHECK(b.renameSelected("Mash Two"));
  CHECK(b.selectedKey() == "user:mash_two");
  CHECK(PresetManager::list().back().file.existsAsFile() && PresetManager::list().back().preset.name == "Mash Two");
  CHECK(b.toggleFavorite("user:mash_two"));
  CHECK(PresetManager::isFavorite("user:mash_two"));
  b.setScope(cf::ui::PresetScope::Favorites);
  CHECK_EQ(b.visibleRows().size(), 1u);
  b.reload();  // a fresh read of the folder keeps the star
  CHECK_EQ(b.visibleRows().size(), 1u);
  b.setScope(cf::ui::PresetScope::User);
  CHECK(b.selectKey("user:mash_two"));
  CHECK(b.deleteSelected());
  CHECK(!PresetManager::isFavorite("user:mash_two"));
  CHECK_EQ(b.items().size(), 17u);
  CHECK(b.selectKey("factory:Amen Classic"));
  CHECK(!b.deleteSelected());  // factory presets are read-only
  CHECK(!b.renameSelected("x"));
}

CHOP_TEST(preset_browser_screenshots) {
  if (!haveDisplay() || !std::getenv("CHOPFRACTAL_G3_SNAPS")) return;
  IsolatedUser user;
  for (auto size : {std::pair<int, int>{1100, 720}, std::pair<int, int>{880, 560}}) {
    EditorRig rig(true, size.first, size.second);
    if (!rig.ed) return;
    rig.ed->presetBrowser().loadKey("factory:Rolling Two-Step");
    rig.ed->presetBrowser().toggleFavorite("factory:Jungle Roll");
    rig.ed->openDrawer("presets");
    pump(100);
    snapshot(*rig.ed, "presets");
  }
}
