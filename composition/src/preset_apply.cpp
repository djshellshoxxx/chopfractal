#include <chopfractal/composition/preset_apply.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::composition {

bool presetProtectsParam(const std::string& id) { return id == "dry_mix" || id == "output_gain_db" || id == "effect_enable"; }

PresetParams presetParams(const presets::Preset& p) {
  PresetParams out;
  auto add = [&out](const std::string& id, double v) {
    const host::ParamDef* def = host::findParam(id);
    if (!def || presetProtectsParam(id)) {
      ++out.ignored;
      return;
    }
    out.values.push_back({id, host::snapValue(*def, v)});
  };
  const int barsIndex = p.bars == 1 ? 0 : p.bars == 2 ? 1 : p.bars == 4 ? 2 : 3;
  add("pattern_bars", barsIndex);
  add("density", p.density);
  add("swing", p.swing);
  add("variation_amount", p.variation);
  add("fx_intensity", p.fxIntensity);
  add("chaos", p.chaos);
  add("allow_reverse", p.allowReverse ? 1.0 : 0.0);
  add("allow_pitch", p.allowPitch ? 1.0 : 0.0);
  add("allow_retrigger", p.allowRetrigger ? 1.0 : 0.0);
  add("allow_filter", p.allowFilter ? 1.0 : 0.0);
  add("allow_glide", p.allowGlide ? 1.0 : 0.0);
  add("allow_crunch", p.allowCrunch ? 1.0 : 0.0);
  for (const presets::ParamValue& v : p.params) {
    bool dup = false;
    for (const presets::ParamValue& o : out.values) dup = dup || o.id == v.id;
    if (!dup) add(v.id, v.value);
  }
  return out;
}

const std::vector<std::string>& presetCapturedParams() {
  static const std::vector<std::string> ids = {"engine_version", "break_profile", "break_backbeat", "break_anchors", "break_groove", "break_humanize", "break_fill", "break_glitch",
                                               "allow_echo", "echo_amount", "echo_rate", "echo_decay", "echo_tone", "allow_tremolo", "trem_amount", "trem_mode", "trem_rate", "trem_depth",
                                               "allow_stutter", "stutter_amount", "stutter_rate", "stutter_gate", "stutter_decay_db", "stutter_pitch", "allow_roll", "roll_amount",
                                               "roll_rate", "roll_rate_end", "roll_freeze"};
  return ids;
}

presets::Preset capturePreset(const std::string& name, const host::ParamValues& v, const ProjectExtras& e, const roles::RuleSet& rules, int evolveEvery, double evolveAmount) {
  presets::Preset p;
  p.name = name;
  p.category = "User";
  static const int kBars[] = {1, 2, 4, 8};
  p.bars = kBars[std::max(0, std::min(3, static_cast<int>(std::lround(v.get(host::kPatternBars)))))];
  p.density = v.get(host::kDensity);
  p.swing = v.get(host::kSwing);
  p.variation = v.get(host::kVariationAmount);
  p.fxIntensity = v.get(host::kFxIntensity);
  p.chaos = v.get(host::kChaos);
  p.allowReverse = v.get(host::kAllowReverse) >= 0.5;
  p.allowPitch = v.get(host::kAllowPitch) >= 0.5;
  p.allowRetrigger = v.get(host::kAllowRetrigger) >= 0.5;
  p.allowFilter = v.get(host::kAllowFilter) >= 0.5;
  p.allowGlide = v.get(host::kAllowGlide) >= 0.5;
  p.allowCrunch = v.get(host::kAllowCrunch) >= 0.5;
  const auto& manifest = host::parameterManifest();
  for (const std::string& id : presetCapturedParams())
    for (std::size_t i = 0; i < manifest.size(); ++i)
      if (id == manifest[i].id) p.params.push_back({id, v.get(static_cast<host::ParamIndex>(i))});
  p.useSeed = true;
  p.seed = e.seed;
  p.gridDivision = e.gridDivision;
  p.gridTriplet = e.gridTriplet;
  p.motif = e.motif;
  p.depth = e.fractalDepth;
  p.mutation = e.fractalMutation >= 0.0 ? e.fractalMutation : std::min(1.0, v.get(host::kVariationAmount) * 0.5);
  p.accentDecay = e.fractalAccentDecay;
  p.evolveEvery = evolveEvery;
  p.evolveAmount = evolveAmount;
  p.rules = rules;
  return p;
}

ProjectExtras extrasWithPreset(ProjectExtras e, const presets::Preset& p) {
  if (p.useSeed) e.seed = p.seed;
  e.motif = p.motif;
  e.fractalDepth = std::max(1, std::min(3, p.depth));
  e.fractalMutation = p.mutation;
  e.fractalAccentDecay = p.accentDecay;
  e.gridDivision = p.gridDivision;
  e.gridTriplet = p.gridTriplet;
  return e;
}

Status applyPresetToSession(ProjectSession& s, const presets::Preset& p) {
  Status v = presets::validate(p);
  if (!v.ok()) return v;
  const ProjectExtras before = s.extras();
  Status st = s.setExtras(extrasWithPreset(before, p));
  if (!st.ok()) return st;
  st = s.setRules(p.rules);
  if (!st.ok()) {
    s.setExtras(before);
    return st;
  }
  return {};
}

}  // namespace chopfractal::composition
