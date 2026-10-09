#pragma once
// Model-free analysis for Smart Setup: chop features, role suggestions, loop length / tempo suggestions.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/result.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::insight {

// Planar decoded audio: channels[c][frame].
struct SourceView {
  const float* const* channels = nullptr;
  int numChannels = 0;
  std::int64_t frames = 0;
  int sampleRate = 0;
};

struct ChopFeatures {
  double lowRatio = 0;   // energy share below ~200 Hz
  double midRatio = 0;
  double highRatio = 0;  // energy share above ~5 kHz
  double zcr = 0;        // zero crossings per sample, 0..1
  double peak = 0;
  double rms = 0;
  double decayMs = 0;    // from the envelope peak to the last point within 30 dB of it
  double lengthMs = 0;
};

enum class Role : std::uint8_t { Kick, Snare, Hat, Cymbal, Other };
const char* roleName(Role r);  // "kick", "snare", "hat", "cymbal", "other" (built-in role names)

struct RoleSuggestion {
  ChopId chop;
  Role role = Role::Other;
  double confidence = 0;  // 0..1
  std::string reason;
};

struct LoopSuggestion {
  int beats = 0;
  int bars = 0;           // 4/4 bars (beats / 4)
  double bpm = 0;
  double confidence = 0;  // 0..1, lower when several candidates are plausible
  std::string note;       // e.g. "4 bars at 96.0 BPM"
};

Result<ChopFeatures> analyzeChop(const SourceView& source, SampleRange range);
Role classify(const ChopFeatures& f, double* confidence = nullptr, std::string* reason = nullptr);
std::vector<RoleSuggestion> suggestRoles(const SourceView& source, const ChopSnapshot& chops);
// Best first. `hostBpm` <= 0 means "no preference" (a 100..130 BPM sweet spot is used).
std::vector<LoopSuggestion> suggestLoop(double seconds, double hostBpm = 0.0);

}  // namespace chopfractal::insight
