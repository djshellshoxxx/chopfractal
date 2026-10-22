#pragma once
// Status bar text and meter ballistics (F05): the sync box string, severity prefixes, dB mapping of the output meter and
// its attack / release / hold / clip latch. Pure functions, unit tested; the JUCE widgets only draw them.
#include <cstdint>
#include <string>

namespace chopfractal::ui {

// "120.0 BPM  4/4  Playing"; with fallbacks "98.0 BPM (manual)  4/4 (assumed)  Stopped"; compact "120.0 | 4/4 | Playing".
std::string describeSync(double bpm, int numerator, int denominator, bool fallbackTempo, bool fallbackMeter, bool playing, bool compact);
// The tooltip of the sync box: "No host tempo: using 98.0 BPM", "Assuming 4/4" or both, else the following sentence.
std::string syncTooltip(double bpm, bool fallbackTempo, bool fallbackMeter);

std::string severityPrefix(int level);  // 0 -> "", 1 -> "[!] ", 2 -> "[x] "
std::string severityName(int level);    // "Info", "Warning", "Error"

float peakToDb(float linear);  // 20 log10, zero, negative and NaN -> -60, clamped to [-60, +6]
float dbToFraction(float db);  // -60..+6 -> 0..1, clamped
std::string dbText(float db);  // "-12.3 dB" with a real minus sign, "-inf dB" at the floor

struct MeterInput {
  float peak[2] = {0.f, 0.f};     // linear peak since the last look
  std::uint32_t clipSamples = 0;  // samples above 0 dBFS since the last look
  float grDb = 0.f;               // safety limiter: most negative gain reduction since the last look (<= 0, 0 when it is idle or off)
};
struct MeterState {
  float db[2] = {-60.f, -60.f};
  float holdDb[2] = {-60.f, -60.f};
  double holdAge[2] = {0.0, 0.0};
  float grDb = 0.f;               // limiter gain reduction as shown: instant attack, 20 dB per second release
  bool clipLatched = false;
};
// Attack is instant, release 20 dB per second, the hold line stays 1.5 s then falls at 20 dB per second; a clip latches
// until the owner clears it. Returns true when anything visible moved by at least 0.25 dB (about a pixel) or the latch changed.
bool updateMeter(MeterState&, const MeterInput&, double dtSeconds, bool reducedMotion = false);

// "Output peak: L -12.3 dB, R -11.8 dB. CLIP lights when the output exceeds 0 dBFS, click it to reset"; while the limiter works the
// sentence "Limiter gain reduction -2.1 dB." follows the peaks.
std::string meterTooltip(const MeterState&);
// "GR -2.1 dB" (real minus sign), empty while the reduction is under 0.05 dB. Text, so the reading never depends on colour.
std::string grText(float grDb);
std::string voiceText(int active, int maxVoices);  // "V 3/8"

}  // namespace chopfractal::ui
