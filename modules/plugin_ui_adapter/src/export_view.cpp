#include <chopfractal/plugin_ui_adapter/export_view.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::ui {
namespace {
constexpr int kRates[] = {44100, 48000, 88200, 96000};
constexpr int kLoops[] = {1, 2, 4, 8, 16};
constexpr int kBits[] = {16, 24, 32};
std::string fmt1(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", v);
  return b;
}
}  // namespace

const std::vector<std::string>& exportFormatItems() {
  static const std::vector<std::string> v = {"16-bit", "24-bit", "32-bit float"};
  return v;
}
const std::vector<std::string>& exportRateItems() {
  static const std::vector<std::string> v = {"44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"};
  return v;
}
const std::vector<std::string>& exportLoopItems() {
  static const std::vector<std::string> v = {"1", "2", "4", "8", "16"};
  return v;
}
int exportRateHz(int i) { return kRates[std::max(0, std::min(3, i))]; }
int exportLoopCount(int i) { return kLoops[std::max(0, std::min(4, i))]; }
int exportBitsPerSample(int i) { return kBits[std::max(0, std::min(2, i))]; }

double exportSeconds(int bars, int beatsPerBar, double bpm, int loops) {
  if (!(bpm > 0.0) || bars < 1 || beatsPerBar < 1 || loops < 1) return 0.0;
  return static_cast<double>(bars) * beatsPerBar * 60.0 / bpm * loops + 0.5;
}

std::string exportEstimateText(double seconds, int rateHz, int bits) {
  if (seconds <= 0.0) return {};
  const double bytes = seconds * rateHz * 2.0 * (bits / 8.0);
  const std::string size = bytes >= 1024.0 * 1024.0 ? fmt1(bytes / (1024.0 * 1024.0)) + " MB" : fmt1(bytes / 1024.0) + " KB";
  return "About " + fmt1(seconds) + " s, " + size;
}

ExportView buildExportView(const ExportViewInput& in) {
  ExportView v;
  const bool busy = in.exporting || in.arrangementRunning;
  const bool can = in.hasPattern && in.hasSource && !in.sourceMissing;
  v.wavEnabled = v.kitEnabled = can && !busy;
  v.dragEnabled = can;
  v.formatEnabled = !busy;
  v.cancelVisible = busy;
  std::string why;
  if (!in.hasSource) why = "Load a loop first";
  else if (in.sourceMissing) why = "Locate the source audio first";
  else if (!in.hasPattern) why = "Generate a pattern first";
  else if (busy) why = "An export is already running";
  v.wavTooltip = why.empty() ? "Render the pattern to a WAV file" : why;
  v.kitTooltip = why.empty() ? "Write one WAV per chop and a MIDI file that replays the pattern on them" : why;
  v.dragTooltip = can ? "Drag onto a DAW track" : (why.empty() ? "Generate a pattern first" : why);
  if (in.hasPattern) v.estimate = exportEstimateText(exportSeconds(in.bars, in.beatsPerBar, in.bpm, exportLoopCount(in.loopsIndex)), exportRateHz(in.rateIndex), exportBitsPerSample(in.formatIndex));
  return v;
}

}  // namespace chopfractal::ui
