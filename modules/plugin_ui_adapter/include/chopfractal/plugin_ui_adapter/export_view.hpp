#pragma once
// View model of the export drawer: the choices of the WAV format controls, what is enabled and why not, and the size
// estimate under the format row. Pure functions, unit tested; the JUCE drawer only draws them.
#include <string>
#include <vector>

namespace chopfractal::ui {

const std::vector<std::string>& exportFormatItems();  // "16-bit", "24-bit", "32-bit float"
const std::vector<std::string>& exportRateItems();    // "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"
const std::vector<std::string>& exportLoopItems();    // "1", "2", "4", "8", "16"
int exportRateHz(int rateIndex);                      // clamped index -> 44100 ... 96000
int exportLoopCount(int loopsIndex);                  // clamped index -> 1 ... 16
int exportBitsPerSample(int formatIndex);             // 16, 24, 32

struct ExportViewInput {
  bool hasSource = false, hasPattern = false, sourceMissing = false;
  bool exporting = false;            // a WAV / kit export runs
  bool arrangementRunning = false;   // an arrangement export runs
  int bars = 2;                      // pattern length
  int beatsPerBar = 4;
  double bpm = 120.0;
  int formatIndex = 1, rateIndex = 0, loopsIndex = 0;
};
struct ExportView {
  bool wavEnabled = false, kitEnabled = false, dragEnabled = false;
  bool formatEnabled = true;         // the format controls lock while an export runs
  bool cancelVisible = false;
  std::string wavTooltip, kitTooltip, dragTooltip;
  std::string estimate;              // "About 8.5 s, 1.5 MB" or empty without a pattern
};
ExportView buildExportView(const ExportViewInput& in);

// Duration of loops x pattern at the tempo, plus the 0.5 s tail the exporter appends.
double exportSeconds(int bars, int beatsPerBar, double bpm, int loops);
// "About 8.5 s, 1.5 MB" (stereo, the chosen word size).
std::string exportEstimateText(double seconds, int rateHz, int bitsPerSample);

}  // namespace chopfractal::ui
