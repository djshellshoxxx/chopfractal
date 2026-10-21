// Factory presets applied to a real session: every id exists in the manifest, applying never touches the protected
// parameters or the pattern, and "preset then Generate" produces a musical, deterministic result for all of them.
#include <chopfractal/composition/preset_apply.hpp>
#include <chopfractal/composition/synthetic_kit.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <algorithm>
#include <set>

#include "chop_test.hpp"
#include "session_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {
host::ParamValues valuesFor(const presets::Preset& p, int* ignored = nullptr) {
  host::ParamValues v = host::ParamValues::defaults();
  const PresetParams pp = presetParams(p);
  if (ignored) *ignored = pp.ignored;
  const auto& m = host::parameterManifest();
  for (const presets::ParamValue& pv : pp.values)
    for (std::size_t i = 0; i < m.size(); ++i)
      if (pv.id == m[i].id) v.set(static_cast<host::ParamIndex>(i), pv.value);
  return v;
}

pattern::Settings settingsFor(const ProjectExtras& e, const host::ParamValues& v) {
  pattern::Settings base;
  base.seed = e.seed;
  base.grid = {e.gridDivision, e.gridTriplet};
  const double var = v.get(host::kVariationAmount);
  base.variation = {var, var, var, var};
  pattern::Settings s = settingsFromParams(v, base);
  s.brk.tempoHint = 120.0;
  return s;
}

std::uint64_t generateWith(const presets::Preset& p, std::size_t* events, bool* ok) {
  ProjectSession s;
  *ok = synth::loadKitInto(s, synth::makeKitLoop(48000), false);
  *ok = *ok && applyPresetToSession(s, p).ok();
  const pattern::Settings st = settingsFor(s.extras(), valuesFor(p));
  *ok = *ok && s.generate(st).ok() && s.pattern() != nullptr;
  if (!*ok) return 0;
  *events = 0;
  for (const pattern::Bar& b : s.pattern()->bars)
    for (const pattern::Beat& bt : b.beats) *events += bt.events.size();
  return fixture::fnv1a(pattern::serialize(*s.pattern()));
}
}  // namespace

CHOP_TEST(factory_preset_parameters_exist_in_the_manifest_and_are_in_range) {
  for (const presets::Preset& p : presets::factoryPresets()) {
    std::set<std::string> seen;
    for (const presets::ParamValue& v : p.params) {
      const host::ParamDef* def = host::findParam(v.id);
      CHECK(def != nullptr);
      if (!def) continue;
      CHECK(!presetProtectsParam(v.id));
      CHECK(v.value >= def->minValue && v.value <= def->maxValue);
      CHECK(host::snapValue(*def, v.value) == v.value);  // choices are whole indices, steps are respected
      CHECK(seen.insert(v.id).second);
    }
    int ignored = 0;
    const host::ParamValues pv = valuesFor(p, &ignored);
    CHECK_EQ(ignored, 0);
    CHECK_EQ(pv.get(host::kEngineVersion), 1.0);
    CHECK_NEAR(pv.get(host::kDensity), p.density, 1e-9);
  }
}

CHOP_TEST(every_factory_preset_applies_and_generates_a_deterministic_pattern) {
  std::set<std::uint64_t> hashes;
  for (const presets::Preset& p : presets::factoryPresets()) {
    std::size_t events = 0, again = 0;
    bool ok = false, ok2 = false;
    const std::uint64_t h = generateWith(p, &events, &ok);
    const std::uint64_t h2 = generateWith(p, &again, &ok2);
    CHECK(ok && ok2);
    CHECK(events >= static_cast<std::size_t>(p.bars) * 3);  // never an empty or near-empty bar
    CHECK(h == h2 && events == again);                      // deterministic from (preset, seed, chops)
    hashes.insert(h);
  }
  CHECK(hashes.size() >= 15);  // the styles really differ
}

CHOP_TEST(preset_sets_extras_and_rules_but_not_protected_parameters) {
  ProjectSession s;
  CHECK(synth::loadKitInto(s, synth::makeKitLoop(48000), false));
  const presets::Preset& boom = presets::factoryPresets()[10];
  CHECK(boom.name == "Boom Bap Basics");
  CHECK(applyPresetToSession(s, boom).ok());
  CHECK_EQ(s.extras().seed, boom.seed);
  CHECK(s.extras().motif == boom.motif && s.extras().gridDivision == 16 && !s.extras().gridTriplet);
  CHECK_EQ(s.rules().rules.size(), boom.rules.rules.size());
  CHECK(s.pattern() == nullptr);  // a preset never generates
  presets::Preset sneaky = boom;
  sneaky.params.push_back({"dry_mix", 1.0});
  sneaky.params.push_back({"output_gain_db", 12.0});
  sneaky.params.push_back({"effect_enable", 0.0});
  sneaky.params.push_back({"no_such_parameter", 1.0});
  const PresetParams pp = presetParams(sneaky);
  CHECK_EQ(pp.ignored, 4);
  for (const presets::ParamValue& v : pp.values) CHECK(!presetProtectsParam(v.id) && host::findParam(v.id) != nullptr);
}

CHOP_TEST(preset_without_a_seed_keeps_the_projects_seed_and_a_bad_preset_changes_nothing) {
  ProjectSession s;
  CHECK(synth::loadKitInto(s, synth::makeKitLoop(48000), false));
  ProjectExtras e = s.extras();
  e.seed = 777;
  CHECK(s.setExtras(e).ok());
  presets::Preset user;
  user.name = "Mine";
  user.useSeed = false;
  user.motif = "x.x.";
  CHECK(applyPresetToSession(s, user).ok());
  CHECK_EQ(s.extras().seed, 777u);
  CHECK(s.extras().motif == "x.x.");
  presets::Preset bad = user;
  bad.motif = "zzzz";
  bad.gridDivision = 4;
  const ProjectExtras before = s.extras();
  CHECK(!applyPresetToSession(s, bad).ok());
  CHECK(s.extras() == before);
}

CHOP_TEST(a_captured_preset_round_trips_through_the_file_format_and_reapplies_the_same_state) {
  ProjectSession s;
  CHECK(synth::loadKitInto(s, synth::makeKitLoop(48000), false));
  const presets::Preset& src = presets::factoryPresets()[16];  // Stutter Edit Glitch
  CHECK(applyPresetToSession(s, src).ok());
  const host::ParamValues live = valuesFor(src);
  presets::Preset mine = capturePreset("Mine", live, s.extras(), s.rules(), 4, 0.2);
  CHECK(presets::validate(mine).ok());
  CHECK(mine.params.size() <= presets::kMaxParams);
  const auto bytes = presets::serialize(mine);
  auto back = presets::deserialize(bytes.data(), bytes.size());
  CHECK(back.ok());
  if (!back.ok()) return;
  CHECK(back.value().category == "User" && back.value().seed == src.seed && back.value().useSeed);
  ProjectSession t;
  CHECK(synth::loadKitInto(t, synth::makeKitLoop(48000), false));
  CHECK(applyPresetToSession(t, back.value()).ok());
  CHECK(t.extras().seed == s.extras().seed && t.extras().motif == s.extras().motif && t.extras().gridDivision == s.extras().gridDivision);
  const host::ParamValues again = valuesFor(back.value());
  for (std::size_t i = 0; i < host::kParamCount; ++i) {
    const std::string id = host::parameterManifest()[i].id;
    if (id == "dry_mix" || id == "output_gain_db" || id == "effect_enable") continue;
    const bool captured = std::find(presetCapturedParams().begin(), presetCapturedParams().end(), id) != presetCapturedParams().end();
    if (captured) CHECK_NEAR(again.v[i], live.v[i], 1e-9);
  }
  std::size_t e1 = 0, e2 = 0;
  bool ok1 = false, ok2 = false;
  generateWith(src, &e1, &ok1);
  generateWith(back.value(), &e2, &ok2);
  CHECK(ok1 && ok2 && e1 > 0 && e2 > 0);  // both still generate
}
