#include "chopfractal/plugin_ui_adapter/status_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::ui {

namespace {
std::string fmt1(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", v);
  return b;
}
}  // namespace

std::string describeSync(double bpm, int numerator, int denominator, bool fallbackTempo, bool fallbackMeter, bool playing, bool compact) {
  const std::string meter = std::to_string(numerator) + "/" + std::to_string(denominator);
  const std::string state = playing ? "Playing" : "Stopped";
  if (compact) return fmt1(bpm) + " | " + meter + " | " + state;
  return fmt1(bpm) + " BPM" + (fallbackTempo ? " (manual)" : "") + "  " + meter + (fallbackMeter ? " (assumed)" : "") + "  " + state;
}

std::string syncTooltip(double bpm, bool fallbackTempo, bool fallbackMeter) {
  std::string t;
  if (fallbackTempo) t = "No host tempo: using " + fmt1(bpm) + " BPM";
  if (fallbackMeter) t += std::string(t.empty() ? "" : ", ") + "Assuming 4/4";
  return t.empty() ? "Following the host tempo and time signature" : t;
}

std::string severityPrefix(int level) { return level == 1 ? "[!] " : level >= 2 ? "[x] " : ""; }
std::string severityName(int level) { return level == 1 ? "Warning" : level >= 2 ? "Error" : "Info"; }

float peakToDb(float linear) {
  if (!(linear > 0.f)) return -60.f;  // zero, negative and NaN
  return std::clamp(20.f * std::log10(linear), -60.f, 6.f);
}
float dbToFraction(float db) { return std::clamp((db + 60.f) / 66.f, 0.f, 1.f); }

std::string dbText(float db) {
  if (db <= -59.95f) return "-inf dB";
  char b[32];
  std::snprintf(b, sizeof b, "%.1f dB", std::fabs(db) < 0.05f ? 0.0 : std::fabs(static_cast<double>(db)));
  return (db <= -0.05f ? std::string("\xE2\x88\x92") : std::string()) + b;
}

bool updateMeter(MeterState& s, const MeterInput& in, double dt, bool reducedMotion) {
  constexpr float kFall = 20.f;   // dB per second
  constexpr double kHold = 1.5;   // seconds
  const float fall = kFall * static_cast<float>(std::max(0.0, dt));
  bool changed = false;
  for (int c = 0; c < 2; ++c) {
    const float peakDb = peakToDb(in.peak[c]);
    float db = s.db[c];
    if (peakDb >= db) {
      db = peakDb;
    } else {
      db = reducedMotion ? peakDb : std::max(peakDb, db - fall);  // reduced motion: no easing, the bar follows the block peaks
    }
    float hold = s.holdDb[c];
    double age = s.holdAge[c];
    if (db >= hold) {
      hold = db;
      age = 0.0;
    } else {
      age += dt;
      if (age > kHold) hold = std::max(db, hold - fall);
    }
    if (std::fabs(db - s.db[c]) >= 0.25f || std::fabs(hold - s.holdDb[c]) >= 0.25f) {
      changed = true;
      s.db[c] = db;
      s.holdDb[c] = hold;
    } else if (db != s.db[c] && db <= -59.9f) {  // settle onto the floor without reporting a visible change
      s.db[c] = db;
    }
    s.holdAge[c] = age;
  }
  {
    const float target = std::isfinite(in.grDb) ? std::min(0.f, in.grDb) : 0.f;
    float gr = s.grDb;
    if (target <= gr) gr = target;
    else gr = reducedMotion ? target : std::min(target, gr + fall);
    if (std::fabs(gr - s.grDb) >= 0.1f || (gr == 0.f && s.grDb != 0.f)) changed = true;
    s.grDb = gr;
  }
  if (in.clipSamples > 0 && !s.clipLatched) {
    s.clipLatched = true;
    changed = true;
  }
  return changed;
}

std::string grText(float grDb) { return grDb > -0.05f ? std::string() : "GR " + dbText(grDb); }

std::string meterTooltip(const MeterState& s) {
  std::string t = "Output peak: L " + dbText(s.holdDb[0] > s.db[0] ? s.holdDb[0] : s.db[0]) + ", R " + dbText(s.holdDb[1] > s.db[1] ? s.holdDb[1] : s.db[1]);
  if (s.grDb <= -0.05f) t += ". Limiter gain reduction " + dbText(s.grDb);
  return t + ". CLIP lights when the output exceeds 0 dBFS, click it to reset";
}

std::string voiceText(int active, int maxVoices) { return "V " + std::to_string(std::max(0, active)) + "/" + std::to_string(std::max(1, maxVoices)); }

}  // namespace chopfractal::ui
