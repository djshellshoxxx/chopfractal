#pragma once
// Screen reader texts for the canvases (F04): what each panel says about itself. Pure formatting, no toolkit.
#include <string>

namespace chopfractal::ui {

inline std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// "3 chops, 2 markers." or "No audio loaded."
inline std::string describeWave(bool hasSource, int chops, int markers) {
  if (!hasSource) return "No audio loaded.";
  return plural(chops, "chop", "chops") + ", " + plural(markers, "marker", "markers") + ".";
}
// "4 bars, 12 hits. Selected: <hit>" or "... No hit selected." / "No pattern."
inline std::string describePattern(bool hasPattern, int bars, int hits, const std::string& selected) {
  if (!hasPattern) return "No pattern.";
  return plural(bars, "bar", "bars") + ", " + plural(hits, "hit", "hits") + ". " + (selected.empty() ? std::string("No hit selected.") : "Selected: " + selected);
}
// "5 variations. Active: Root." (the label, else "none")
inline std::string describeHistory(int variations, const std::string& activeLabel) {
  return plural(variations, "variation", "variations") + ". Active: " + (activeLabel.empty() ? std::string("none") : activeLabel) + ".";
}

}  // namespace chopfractal::ui
