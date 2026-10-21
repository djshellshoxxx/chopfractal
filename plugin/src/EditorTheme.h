#pragma once
// Colours shared by more than one editor panel. The values are the ones the single-file editor used, unchanged.
#include <juce_graphics/juce_graphics.h>

namespace editor_theme {
inline juce::Colour laneColour(int chopIndex) {
  return juce::Colour::fromHSV(static_cast<float>(((chopIndex % 8) + 8) % 8) / 8.0f, 0.55f, 0.85f, 1.0f);
}
inline juce::Colour background() { return juce::Colour(0xff15181d); }
inline juce::Colour canvas() { return juce::Colour(0xff20252d); }
inline juce::Colour accentBlue() { return juce::Colour(0xff6fb1ff); }
inline juce::Colour accentAmber() { return juce::Colour(0xffffc857); }
inline juce::Colour accentRed() { return juce::Colour(0xffff6b6b); }
inline juce::Colour accentGreen() { return juce::Colour(0xff7cf29a); }
inline juce::Colour neutralGrey() { return juce::Colour(0xff9aa4b2); }
}  // namespace editor_theme
