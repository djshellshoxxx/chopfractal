// Circuit Drift Labs theme: palette equals docs/design/cdl-tokens.json, fonts load, settings round trip.
#include <memory>

#include "CdlLookAndFeel.h"
#include "CdlTheme.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
juce::Colour parseToken(const juce::String& v, bool* ok) {
  *ok = true;
  if (v.startsWith("#")) return juce::Colour(0xff000000u | static_cast<juce::uint32>(v.substring(1).getHexValue32()));
  if (v.startsWith("rgba(")) {
    auto t = juce::StringArray::fromTokens(v.fromFirstOccurrenceOf("(", false, false).upToFirstOccurrenceOf(")", false, false), ",", "");
    return juce::Colour::fromFloatRGBA(t[0].getFloatValue() / 255.0f, t[1].getFloatValue() / 255.0f, t[2].getFloatValue() / 255.0f, t[3].getFloatValue());
  }
  *ok = false;  // "{bg}" style reference
  return {};
}
juce::Colour pick(const cdl::Palette& p, const juce::String& name) {
  if (name == "bg") return p.bg;
  if (name == "panel") return p.panel;
  if (name == "edge") return p.edge;
  if (name == "edge-hi") return p.edgeHi;
  if (name == "spray") return p.spray;
  if (name == "spray-hi") return p.sprayHi;
  if (name == "spray-wash") return p.sprayWash;
  if (name == "rain") return p.rain;
  if (name == "ink") return p.ink;
  if (name == "ink-muted") return p.inkMuted;
  if (name == "ok") return p.ok;
  if (name == "warn") return p.warn;
  if (name == "clip") return p.clip;
  if (name == "knob-top") return p.knobTop;
  if (name == "knob-bottom") return p.knobBottom;
  return juce::Colour(0x00010203);
}
}  // namespace

CHOP_TEST(cdl_palette_matches_the_token_file) {
  const auto root = juce::JSON::parse(juce::File(CHOPFRACTAL_TOKENS_FILE));
  CHECK(root.isObject());
  const auto* tokens = root["color"]["tokens"].getArray();
  CHECK(tokens != nullptr);
  if (!tokens) return;
  int compared = 0;
  for (const auto& t : *tokens) {
    const auto name = t["name"].toString();
    for (const char* theme : {"dark", "concrete"}) {
      bool ok = false;
      const auto want = parseToken(t["value"][theme].toString(), &ok);
      if (!ok) continue;
      const auto& p = cdl::palette(std::string(theme) == "dark" ? cdl::ThemeId::Night : cdl::ThemeId::Concrete);
      const auto got = pick(p, name);
      if (got == juce::Colour(0x00010203)) continue;
      if (got.getARGB() != want.getARGB()) std::fprintf(stderr, "  token %s (%s) differs\n", name.toRawUTF8(), theme);
      CHECK(got.getARGB() == want.getARGB());
      ++compared;
    }
  }
  CHECK(compared >= 28);
}

CHOP_TEST(cdl_fonts_are_embedded_and_settings_round_trip) {
  CHECK(cdl::brandFontsLoaded());
  CHECK(cdl::font(cdl::FontRole::Value).getTypefacePtr() != nullptr);
  const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_ui_test_settings.json");
  const auto previous = cdl::settingsFile();
  file.deleteFile();
  cdl::setSettingsFileOverride(file);
  CHECK(cdl::loadUserSettings().theme == cdl::ThemeId::Night && cdl::loadUserSettings().width == 1100);
  cdl::UserSettings s;
  s.theme = cdl::ThemeId::Concrete;
  s.width = 1300;
  s.height = 800;
  s.advanced = true;
  cdl::saveUserSettings(s);
  const auto back = cdl::loadUserSettings();
  CHECK(back.theme == cdl::ThemeId::Concrete && back.width == 1300 && back.height == 800 && back.advanced);
  cdl::UserSettings huge;
  huge.width = 99999;
  huge.height = 1;
  cdl::saveUserSettings(huge);
  CHECK(cdl::loadUserSettings().width == 2200 && cdl::loadUserSettings().height == 560);  // clamped on restore
  file.deleteFile();
  cdl::setSettingsFileOverride(previous);
  cdl::setCurrentTheme(cdl::ThemeId::Night);
}
