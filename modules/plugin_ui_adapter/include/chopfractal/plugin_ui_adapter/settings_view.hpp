#pragma once
// View model of the settings drawer: the interface scale choices and the plain-text diagnostics report that Copy
// diagnostics puts on the clipboard. Pure functions, unit tested.
#include <string>
#include <vector>

namespace chopfractal::ui {

const std::vector<int>& uiScalePercents();      // 90, 100, 110, 125
const std::vector<std::string>& uiScaleItems(); // "90%", "100%", "110%", "125%"
int uiScaleIndex(int percent);                  // the nearest choice
int uiScalePercentAt(int index);                // clamped
std::string limiterCeilingText(double db);      // "-1.0 dB" with a real minus sign

struct DiagnosticsInput {
  std::string version, format, host;   // "0.2.1", "VST3" / "CLAP" / "Standalone", host name or empty
  double sampleRate = 0.0;
  int blockSize = 0;
  std::string theme;                   // "Night" / "Concrete"
  int windowWidth = 0, windowHeight = 0, uiScalePercent = 100;
  bool reducedMotion = false, advanced = false;
  bool hasSource = false;
  std::string sourceName;
  double sourceSeconds = 0.0;
  int chops = 0, events = 0, bars = 0;
  double bpm = 0.0;
  bool localTempo = true;
  int numerator = 4, denominator = 4;
  bool limiterOn = false, levelMatchOn = false;
  double limiterCeilingDb = -1.0;
  std::vector<std::string> notices;    // newest last, already formatted with their severity
};
// Multi-line text; no file system paths of the user's home are included, only the source file name.
std::string buildDiagnosticsText(const DiagnosticsInput&);

}  // namespace chopfractal::ui
