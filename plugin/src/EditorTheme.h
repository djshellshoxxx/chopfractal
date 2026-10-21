#pragma once
// Colours shared by more than one editor panel. They forward to the Circuit Drift Labs palette (CdlTheme.h) so the
// panels follow the Night / Concrete choice; the names are kept for the older panels until the second wave replaces them.
#include <juce_graphics/juce_graphics.h>

#include "CdlTheme.h"

namespace editor_theme {
inline juce::Colour laneColour(int chopIndex) {
  const bool night = cdl::currentTheme() == cdl::ThemeId::Night;
  return juce::Colour::fromHSV(static_cast<float>(((chopIndex % 8) + 8) % 8) / 8.0f, night ? 0.55f : 0.70f, night ? 0.85f : 0.60f, 1.0f);
}
inline juce::Colour background() { return cdl::pal().bg; }
inline juce::Colour canvas() { return cdl::pal().panel; }
inline juce::Colour accentBlue() { return cdl::pal().rain; }
inline juce::Colour accentAmber() { return cdl::pal().warn; }
inline juce::Colour accentRed() { return cdl::pal().clip; }
inline juce::Colour accentGreen() { return cdl::pal().ok; }
inline juce::Colour neutralGrey() { return cdl::pal().inkMuted; }
inline juce::Colour text() { return cdl::pal().ink; }
inline juce::Colour line() { return cdl::pal().edge; }
}  // namespace editor_theme
