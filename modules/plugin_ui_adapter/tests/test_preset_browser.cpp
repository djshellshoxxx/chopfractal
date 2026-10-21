#include <chopfractal/plugin_ui_adapter/preset_browser.hpp>

#include "chop_test.hpp"

using namespace chopfractal::ui;

namespace {
std::vector<PresetItem> sample() {
  return {{"factory:Amen Classic", "Amen Classic", "Amen / Funk", "Tight funk break", true, false, true},
          {"factory:Jungle Roll", "Jungle Roll", "Jungle", "Fast rolling chops", true, true, true},
          {"factory:Boom Bap Basics", "Boom Bap Basics", "Boom-bap", "Lazy hip-hop drums", true, false, true},
          {"user:my_chop", "My Chop", "User", "", false, true, true},
          {"user:old", "Old One", "User", "", false, false, false}};
}
}  // namespace

CHOP_TEST(preset_filter_scope_and_search) {
  const auto items = sample();
  CHECK_EQ(filterPresets(items, PresetScope::All, "").size(), 5u);
  CHECK_EQ(filterPresets(items, PresetScope::Factory, "").size(), 3u);
  CHECK_EQ(filterPresets(items, PresetScope::User, "").size(), 2u);
  const auto fav = filterPresets(items, PresetScope::Favorites, "");
  CHECK(fav.size() == 2 && fav[0] == 1 && fav[1] == 3);
  const auto jungle = filterPresets(items, PresetScope::All, "JUNGLE");
  CHECK(jungle.size() == 1 && jungle[0] == 1);
  CHECK_EQ(filterPresets(items, PresetScope::All, "amen funk").size(), 1u);   // every word, in the name or the style
  CHECK_EQ(filterPresets(items, PresetScope::All, "hip hop").size(), 1u);     // each word is matched on its own, anywhere in the text
  CHECK_EQ(filterPresets(items, PresetScope::All, "hip jungle").size(), 0u);  // but every word has to be found
  CHECK_EQ(filterPresets(items, PresetScope::All, "hip-hop").size(), 1u);     // found in the description
  CHECK_EQ(filterPresets(items, PresetScope::All, "user").size(), 2u);        // tags match too
  CHECK_EQ(filterPresets(items, PresetScope::Favorites, "my chop").size(), 1u);  // scope and query combine
  CHECK_EQ(filterPresets(items, PresetScope::All, "zzz").size(), 0u);
}

CHOP_TEST(preset_row_stepping_clamps_and_starts_at_the_ends) {
  CHECK_EQ(stepRow(0, -1, 1), -1);
  CHECK_EQ(stepRow(5, -1, 1), 0);
  CHECK_EQ(stepRow(5, -1, -1), 4);
  CHECK_EQ(stepRow(5, 2, 1), 3);
  CHECK_EQ(stepRow(5, 4, 1), 4);
  CHECK_EQ(stepRow(5, 0, -1), 0);
}

CHOP_TEST(preset_names_are_validated_and_made_unique) {
  CHECK(validatePresetName("Boom Bap 2").empty());
  CHECK(!validatePresetName("").empty());
  CHECK(!validatePresetName(std::string(49, 'a')).empty());
  CHECK(!validatePresetName("a/b").empty());
  CHECK(!validatePresetName("a|b").empty());
  CHECK(!validatePresetName("   ").empty());
  CHECK(!validatePresetName("tab\there").empty());
  const auto items = sample();
  CHECK(uniquePresetName(items, "Fresh") == "Fresh");
  CHECK(uniquePresetName(items, "my chop") == "my chop 2");
  CHECK(presetSubtitle(items[0]) == "Amen / Funk, factory" && presetSubtitle(items[3]) == "User, yours");
}
