#pragma once
// Per-user data folder shared by presets and drag-out files. JUCE's userApplicationDataDirectory is ~/Library on macOS,
// but Apple's convention is ~/Library/Application Support.
#include <juce_core/juce_core.h>

namespace apppaths {
inline juce::File dataRoot() {
  juce::File root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  root = root.getChildFile("Application Support");
#endif
  return root.getChildFile("ChopFractal");
}
}  // namespace apppaths
