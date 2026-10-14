#pragma once
// User preset files (.cfpreset) in the per-user application data folder, plus the factory list. All parsing and
// validation lives in the portable preset_store module; this class only does the filesystem work.
#include <juce_core/juce_core.h>

#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/preset_store/presets.hpp>
#include <vector>

class PresetManager {
 public:
  struct Entry {
    juce::String label;  // "Factory: Tight Groove" or the user preset's name
    bool factory = true;
    chopfractal::presets::Preset preset;
    juce::File file;  // user presets only
  };

  // <userApplicationDataDirectory>/ChopFractal/Presets, or the override below (used by tests).
  static juce::File userFolder();
  static void setFolderOverride(const juce::File& folder);

  // Factory presets first, then user presets sorted by name. Unreadable or corrupt files are skipped and counted.
  static std::vector<Entry> list(int* skipped = nullptr);
  // Writes <sanitized name>.cfpreset atomically; refuses to replace an existing file unless `overwrite`.
  static chopfractal::Status save(const chopfractal::presets::Preset& p, bool overwrite);
  static chopfractal::Status remove(const juce::File& file);  // only files inside userFolder() are deleted
};
