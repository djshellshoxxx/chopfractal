#pragma once
// Applying a preset (modules/preset_store) to the headless project: which host parameters it sets, and the project
// extras and rules it replaces. A preset never touches the pattern, the history, the markers, the roles or the
// protected output parameters (dry mix, output gain, effect enable).
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/preset_store/presets.hpp>

#include <string>
#include <vector>

namespace chopfractal::composition {

// Parameters a preset never sets, whatever its file says.
bool presetProtectsParam(const std::string& id);

struct PresetParams {
  std::vector<presets::ParamValue> values;  // manifest id and the snapped plain value, in file order (flat fields first)
  int ignored = 0;                          // unknown or protected ids that were skipped
};
// The flat fields (density, swing, variation, fx intensity, chaos, the six allow flags, bars) followed by the preset's own
// parameters, each snapped to its manifest range.
PresetParams presetParams(const presets::Preset& p);

// The manifest ids a saved user preset captures beyond the flat fields: the engine v2 profile and its controls, Echo,
// Tremolo-Pan, Stutter and Roll (29 ids; a preset holds at most 32).
const std::vector<std::string>& presetCapturedParams();
// A user preset from the live state: flat fields and captured parameters from `values`, the seed, grid, Fractal settings
// from `extras`, the rules, with `evolveEvery` / `evolveAmount` supplied by the caller.
presets::Preset capturePreset(const std::string& name, const host::ParamValues& values, const ProjectExtras& extras, const roles::RuleSet& rules, int evolveEvery,
                              double evolveAmount);

// All or nothing: validates the preset, then stores the seed (when the preset carries one), motif, depth, mutation, accent
// decay and generation grid in the extras and replaces the rules. A refused step leaves the extras and rules as they were.
Status applyPresetToSession(ProjectSession& s, const presets::Preset& p);

// The extras a preset produces from `base` (used by the editor for its own copy of the extras).
ProjectExtras extrasWithPreset(ProjectExtras base, const presets::Preset& p);

}  // namespace chopfractal::composition
