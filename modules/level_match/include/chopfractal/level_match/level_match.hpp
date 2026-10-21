#pragma once
// ITU-R BS.1770-4 integrated loudness and the Level Match gain computation (docs/specs/roadmap/F16). Pure functions over
// planar float buffers; no dependencies, no state, deterministic. Message or worker thread only (allocates).
#include <cstdint>

namespace chopfractal::levelmatch {

constexpr double kSilenceLufs = -70.0;                    // absolute gate of BS.1770-4
constexpr double kMaxBoostDb = 12.0, kMaxCutDb = 12.0;    // the match gain is clamped to [-12, +12]
constexpr double kMinMeasureSeconds = 0.1;

struct Loudness {
  double lufs = -200.0;      // integrated, gated; -200 means "silent / too short"
  double samplePeak = 0.0;   // linear, max |x| over the (stereo-expanded) signal
  bool valid = false;
};

struct MatchResult {
  double gainDb = 0.0;       // rounded to 0.1 dB, 0.0 when !valid
  double requestedDb = 0.0;  // before clamping and headroom
  bool valid = false;
  bool clamped = false;      // hit +/-12 dB
  bool headroomLimited = false;
};

// K-weighting biquads (high shelf, then the RLB high-pass) for a sample rate, in double. Exposed for tests.
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;  // a0 normalized to 1
};
Biquad kWeightShelf(double sampleRate);
Biquad kWeightHighPass(double sampleRate);

// Planar input (`channels` is 1 or 2); mono is measured as if duplicated to both channels (what actually plays).
// 400 ms blocks with 75 % overlap, absolute gate -70 LUFS, relative gate -10 LU. A signal shorter than one block (but at
// least kMinMeasureSeconds) uses one zero-padded block and skips the relative gate. Non-finite samples count as zero.
// The result does not depend on how the caller chunks data (it takes the whole buffer).
Loudness measure(const float* const* planar, int channels, std::int64_t frames, int sampleRate);

// requested = source - pattern + trim, clamped to [-12, +12], rounded to 0.1 dB. When the limiter is off the boost is
// reduced so that pattern.samplePeak * gain <= 10^(-0.1 / 20) (Level match alone can then never clip at unity output).
MatchResult computeMatch(const Loudness& source, const Loudness& pattern, double trimDb, bool limiterOn, double ceilingDb);

}  // namespace chopfractal::levelmatch
