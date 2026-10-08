#pragma once
// Host-automation parameter manifest. Pure data: no JUCE or VST3 types. The JUCE shell builds its
// parameter tree from this manifest, and tests pin it, so shipped IDs can never change silently.
//
// Rules: IDs are permanent and append-only (never reuse or repurpose one); ranges only widen; a new
// parameter gets the next index and the manifest version it first shipped in. Seed is deliberately not
// a parameter; Generate/Mutate are explicit GUI actions. Manual markers and event edits are project
// state, not automation.
#include <chopfractal/audio_renderer/renderer.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace chopfractal::host {

constexpr int kManifestVersion = 1;

enum class ParamKind { Float, Choice, Bool };

// Stable indices into ParamValues (append-only, matches the manifest order).
enum ParamIndex : std::size_t {
  kVariationAmount = 0,
  kDensity,
  kSwing,
  kPatternBars,
  kDryMix,
  kOutputGainDb,
  kEffectEnable,
  kAllowReverse,
  kAllowPitch,
  kAllowRetrigger,
  kParamCount
};

struct ParamDef {
  const char* id;
  const char* name;
  const char* unit;
  ParamKind kind;
  double minValue;
  double maxValue;
  double defaultValue;
  double step;           // 0 = continuous; choices and bools use 1
  double smoothingMs;    // 0 = applied at block boundaries only
  int sinceVersion;      // manifest version in which the parameter first shipped
  const char* choices;   // Choice only: '|'-separated labels in index order
};

const std::vector<ParamDef>& parameterManifest();
const ParamDef* findParam(std::string_view id);

double snapValue(const ParamDef& def, double plain);       // clamp to range and round to step/choice
double normalize(const ParamDef& def, double plain);       // plain -> [0, 1]
double denormalize(const ParamDef& def, double normalized);  // [0, 1] -> plain (snapped)

struct ParamValues {
  std::array<double, kParamCount> v{};
  static ParamValues defaults();
  double get(ParamIndex i) const { return v[i]; }
  void set(ParamIndex i, double plain);  // snaps and clamps via the manifest
};

// Choice mapping for the pattern length parameter: index 0..3 -> 1, 2, 4, 8 bars.
int barsFromChoice(double choiceIndex);

// Block-level render parameters from the current automation values.
render::RenderParams toRenderParams(const ParamValues& values);

}  // namespace chopfractal::host
