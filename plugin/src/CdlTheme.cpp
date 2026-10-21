#include "CdlTheme.h"

#include <CdlFonts.h>

#include <atomic>

#include "AppPaths.h"

namespace cdl {
namespace {
juce::Colour rgb(juce::uint32 v) { return juce::Colour(0xff000000u | v); }

const Palette kNight{rgb(0x04060B), rgb(0x0B0F17), rgb(0x192031), rgb(0x6A757C), rgb(0xFF2D7A), rgb(0xFF6FA3), juce::Colour::fromFloatRGBA(1.0f, 45 / 255.0f, 122 / 255.0f, 0.15f),
                     rgb(0x04060B), rgb(0x27E0D0), rgb(0xE4E9EA), rgb(0x8C979C), rgb(0x6BFF8E), rgb(0xF5DC00), rgb(0xFF5A44), rgb(0x18202E), rgb(0x070A10)};
const Palette kConcrete{rgb(0xD5D9D8), rgb(0xE6E9E8), rgb(0xB3BABB), rgb(0x6D777B), rgb(0xC0104F), rgb(0xA00D40), juce::Colour::fromFloatRGBA(192 / 255.0f, 16 / 255.0f, 79 / 255.0f, 0.12f),
                        rgb(0xE6E9E8), rgb(0x00756D), rgb(0x101417), rgb(0x4A5559), rgb(0x12662A), rgb(0x6B5A00), rgb(0xB3261A), rgb(0xF4F6F5), rgb(0xC3C9C8)};

std::atomic<int> gTheme{0};
juce::File gOverride;

struct Faces {
  juce::Typeface::Ptr regular, semibold, mono, stencil, tag;
  Faces() {
    regular = juce::Typeface::createSystemTypefaceFor(CdlFonts::InterRegular_ttf, static_cast<size_t>(CdlFonts::InterRegular_ttfSize));
    semibold = juce::Typeface::createSystemTypefaceFor(CdlFonts::InterSemiBold_ttf, static_cast<size_t>(CdlFonts::InterSemiBold_ttfSize));
    mono = juce::Typeface::createSystemTypefaceFor(CdlFonts::JetBrainsMonoMedium_ttf, static_cast<size_t>(CdlFonts::JetBrainsMonoMedium_ttfSize));
    stencil = juce::Typeface::createSystemTypefaceFor(CdlFonts::BigShouldersStencilDisplayBlack_ttf, static_cast<size_t>(CdlFonts::BigShouldersStencilDisplayBlack_ttfSize));
    tag = juce::Typeface::createSystemTypefaceFor(CdlFonts::RubikSprayPaintRegular_ttf, static_cast<size_t>(CdlFonts::RubikSprayPaintRegular_ttfSize));
  }
};
const Faces& faces() {
  static const Faces f;
  return f;
}

juce::Font make(const juce::Typeface::Ptr& tf, float px, float kerning, bool mono) {
  juce::FontOptions o = tf != nullptr ? juce::FontOptions(tf) : juce::FontOptions(mono ? juce::Font::getDefaultMonospacedFontName() : juce::Font::getDefaultSansSerifFontName(), px, juce::Font::plain);
  // JUCE height covers ascent plus descent; the point height is the em size, which is what the token sizes mean.
  o = o.withPointHeight(px);
  if (kerning != 0.0f) o = o.withKerningFactor(kerning);
  return juce::Font(o);
}
}  // namespace

const Palette& palette(ThemeId t) { return t == ThemeId::Concrete ? kConcrete : kNight; }
ThemeId currentTheme() { return gTheme.load() == 1 ? ThemeId::Concrete : ThemeId::Night; }
void setCurrentTheme(ThemeId t) { gTheme = t == ThemeId::Concrete ? 1 : 0; }

bool brandFontsLoaded() { return faces().regular != nullptr && faces().mono != nullptr && faces().stencil != nullptr; }

juce::Font font(FontRole role, float s) {
  const auto& f = faces();
  switch (role) {
    case FontRole::Title: return make(f.stencil, 20.0f * s, 0.06f, false);
    case FontRole::Section: return make(f.semibold, 11.0f * s, 0.08f, false);
    case FontRole::Label: return make(f.regular, 11.0f * s, 0.08f, false);
    case FontRole::Body: return make(f.regular, 13.0f * s, 0.0f, false);
    case FontRole::Hint: return make(f.regular, 11.0f * s, 0.0f, false);
    case FontRole::Value: return make(f.mono, 12.0f * s, 0.0f, true);
    case FontRole::ValueLg: return make(f.mono, 14.0f * s, 0.0f, true);
    case FontRole::MonoXs: return make(f.mono, 9.0f * s, 0.0f, true);
    case FontRole::Tag: return make(f.tag, 16.0f * s, 0.0f, false);
    case FontRole::Button: return make(f.semibold, 13.0f * s, 0.0f, false);
  }
  return make(f.regular, 13.0f * s, 0.0f, false);
}

void setSettingsFileOverride(const juce::File& f) { gOverride = f; }
juce::File settingsFile() { return gOverride != juce::File() ? gOverride : apppaths::dataRoot().getChildFile("ui.json"); }

UserSettings loadUserSettings() {
  UserSettings s;
  const auto file = settingsFile();
  if (!file.existsAsFile()) return s;
  const auto v = juce::JSON::parse(file);
  if (!v.isObject()) return s;
  s.theme = static_cast<int>(v.getProperty("theme", 0)) == 1 ? ThemeId::Concrete : ThemeId::Night;
  s.width = juce::jlimit(880, 2200, static_cast<int>(v.getProperty("width", 1100)));
  s.height = juce::jlimit(560, 1440, static_cast<int>(v.getProperty("height", 720)));
  s.advanced = static_cast<bool>(v.getProperty("advanced", false));
  return s;
}

void saveUserSettings(const UserSettings& s) {
  auto* o = new juce::DynamicObject();
  o->setProperty("theme", s.theme == ThemeId::Concrete ? 1 : 0);
  o->setProperty("width", s.width);
  o->setProperty("height", s.height);
  o->setProperty("advanced", s.advanced);
  const auto file = settingsFile();
  file.getParentDirectory().createDirectory();
  file.replaceWithText(juce::JSON::toString(juce::var(o)));  // best effort; a read-only profile just loses the preference
}
}  // namespace cdl
