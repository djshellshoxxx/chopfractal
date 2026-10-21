#pragma once
// Circuit Drift Labs theme: palette tokens (docs/design/cdl-tokens.json), fonts and the per-user UI settings.
// Night is the default, Concrete the daylight alternative. The current theme is a process-wide choice.
#include <juce_gui_basics/juce_gui_basics.h>

namespace cdl {
enum class ThemeId { Night = 0, Concrete = 1 };

struct Palette {
  juce::Colour bg, panel, edge, edgeHi, spray, sprayHi, sprayWash, onSpray, rain, ink, inkMuted, ok, warn, clip, knobTop, knobBottom;
};

const Palette& palette(ThemeId);
ThemeId currentTheme();
void setCurrentTheme(ThemeId);
inline const Palette& pal() { return palette(currentTheme()); }

constexpr float kNotch = 11.0f;  // hand mark: 11, not 12

// Font roles from the token file. `px` is the CSS-like size; fonts fall back to the JUCE defaults when a typeface is missing.
enum class FontRole { Title, Section, Label, Body, Hint, Value, ValueLg, MonoXs, Tag, Button };
juce::Font font(FontRole role, float scale = 1.0f);
bool brandFontsLoaded();

// Per-user settings (not per project): theme, window size, simple/advanced. Stored as JSON next to the presets.
struct UserSettings {
  ThemeId theme = ThemeId::Night;
  int width = 1100, height = 720;
  bool advanced = false;
};
UserSettings loadUserSettings();
void saveUserSettings(const UserSettings&);
void setSettingsFileOverride(const juce::File&);  // tests: keep the real user file untouched
juce::File settingsFile();
}  // namespace cdl
