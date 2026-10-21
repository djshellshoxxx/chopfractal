#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::host;

// Golden manifest. Shipped parameter IDs are permanent: this test fails if one changes or disappears.
// To add a parameter, append it here, append it to the manifest, and bump nothing else.
namespace {
struct Golden {
  const char* id;
  ParamKind kind;
  double minV, maxV, def;
  int since;
};
const Golden kGolden[] = {
    {"variation_amount", ParamKind::Float, 0.0, 1.0, 0.5, 1},
    {"density", ParamKind::Float, 0.0, 1.0, 0.5, 1},
    {"swing", ParamKind::Float, 0.0, 1.0, 0.0, 1},
    {"pattern_bars", ParamKind::Choice, 0.0, 3.0, 2.0, 1},
    {"dry_mix", ParamKind::Float, 0.0, 1.0, 0.0, 1},
    {"output_gain_db", ParamKind::Float, -60.0, 12.0, 0.0, 1},
    {"effect_enable", ParamKind::Bool, 0.0, 1.0, 1.0, 1},
    {"allow_reverse", ParamKind::Bool, 0.0, 1.0, 0.0, 1},
    {"allow_pitch", ParamKind::Bool, 0.0, 1.0, 0.0, 1},
    {"allow_retrigger", ParamKind::Bool, 0.0, 1.0, 0.0, 1},
    {"allow_filter", ParamKind::Bool, 0.0, 1.0, 0.0, 2},
    {"allow_glide", ParamKind::Bool, 0.0, 1.0, 0.0, 2},
    {"allow_crunch", ParamKind::Bool, 0.0, 1.0, 0.0, 2},
    {"fx_intensity", ParamKind::Float, 0.0, 1.0, 0.5, 2},
    {"chaos", ParamKind::Float, 0.0, 1.0, 0.0, 3},
    {"engine_version", ParamKind::Choice, 0.0, 1.0, 1.0, 3},
    {"break_profile", ParamKind::Choice, 0.0, 5.0, 1.0, 3},
    {"break_backbeat", ParamKind::Choice, 0.0, 1.0, 0.0, 3},
    {"break_anchors", ParamKind::Choice, 0.0, 2.0, 0.0, 3},
    {"break_groove", ParamKind::Float, 0.0, 1.0, 0.5, 3},
    {"break_humanize", ParamKind::Float, 0.0, 1.0, 0.5, 3},
    {"break_fill", ParamKind::Float, 0.0, 1.0, 0.5, 3},
    {"break_glitch", ParamKind::Float, 0.0, 1.0, 0.5, 3},
    {"groove_enable", ParamKind::Bool, 0.0, 1.0, 0.0, 3},
    {"groove_template", ParamKind::Choice, 0.0, 8.0, 0.0, 3},
    {"groove_swing", ParamKind::Float, 50.0, 75.0, 50.0, 3},
    {"groove_hum_time", ParamKind::Float, 0.0, 30.0, 0.0, 3},
    {"groove_hum_vel", ParamKind::Float, 0.0, 25.0, 0.0, 3},
    {"level_match", ParamKind::Bool, 0.0, 1.0, 0.0, 3},
    {"match_trim_db", ParamKind::Float, -6.0, 6.0, 0.0, 3},
    {"limiter_enable", ParamKind::Bool, 0.0, 1.0, 0.0, 3},
    {"limiter_ceiling_db", ParamKind::Float, -6.0, 0.0, -1.0, 3},
    {"allow_echo", ParamKind::Bool, 0.0, 1.0, 0.0, 3},
    {"echo_amount", ParamKind::Float, 0.0, 100.0, 40.0, 3},
    {"echo_rate", ParamKind::Choice, 0.0, 3.0, 1.0, 3},
    {"echo_decay", ParamKind::Float, 10.0, 90.0, 55.0, 3},
    {"echo_tone", ParamKind::Float, 0.0, 100.0, 50.0, 3},
    {"allow_tremolo", ParamKind::Bool, 0.0, 1.0, 0.0, 3},
    {"trem_amount", ParamKind::Float, 0.0, 100.0, 30.0, 3},
    {"trem_mode", ParamKind::Choice, 0.0, 2.0, 2.0, 3},
    {"trem_rate", ParamKind::Choice, 0.0, 3.0, 1.0, 3},
    {"trem_depth", ParamKind::Float, 0.0, 100.0, 60.0, 3},
    {"allow_stutter", ParamKind::Bool, 0.0, 1.0, 0.0, 3},
    {"stutter_amount", ParamKind::Float, 0.0, 100.0, 25.0, 3},
    {"stutter_rate", ParamKind::Choice, 0.0, 9.0, 4.0, 3},
    {"stutter_gate", ParamKind::Float, 10.0, 100.0, 80.0, 3},
    {"stutter_decay_db", ParamKind::Float, -12.0, 6.0, -1.5, 3},
    {"stutter_pitch", ParamKind::Float, -12.0, 12.0, 0.0, 3},
    {"allow_roll", ParamKind::Bool, 0.0, 1.0, 0.0, 3},
    {"roll_amount", ParamKind::Float, 0.0, 100.0, 20.0, 3},
    {"roll_rate", ParamKind::Choice, 0.0, 9.0, 4.0, 3},
    {"roll_rate_end", ParamKind::Choice, 0.0, 9.0, 8.0, 3},
    {"roll_freeze", ParamKind::Float, 0.0, 100.0, 0.0, 3},
    {"slice_polish", ParamKind::Bool, 0.0, 1.0, 1.0, 3},
    {"perf_mode", ParamKind::Choice, 0.0, 2.0, 0.0, 3},
    {"perf_velocity_curve", ParamKind::Choice, 0.0, 3.0, 0.0, 3},
};
}  // namespace

CHOP_TEST(manifest_matches_the_golden_list_and_indices) {
  const auto& m = parameterManifest();
  CHECK_EQ(m.size(), kParamCount);
  CHECK_EQ(m.size(), sizeof(kGolden) / sizeof(kGolden[0]));
  std::set<std::string> ids;
  for (std::size_t i = 0; i < m.size() && i < sizeof(kGolden) / sizeof(kGolden[0]); ++i) {
    CHECK(std::string(m[i].id) == kGolden[i].id);
    CHECK(m[i].kind == kGolden[i].kind);
    CHECK_NEAR(m[i].minValue, kGolden[i].minV, 0.0);
    CHECK_NEAR(m[i].maxValue, kGolden[i].maxV, 0.0);
    CHECK_NEAR(m[i].defaultValue, kGolden[i].def, 0.0);
    CHECK_EQ(m[i].sinceVersion, kGolden[i].since);
    CHECK(m[i].sinceVersion <= kManifestVersion);
    CHECK(m[i].defaultValue >= m[i].minValue && m[i].defaultValue <= m[i].maxValue);
    CHECK(ids.insert(m[i].id).second);  // IDs are unique
  }
  CHECK_EQ(std::string(m[kOutputGainDb].id), "output_gain_db");
  CHECK_EQ(std::string(m[kAllowRetrigger].id), "allow_retrigger");
  CHECK_EQ(std::string(m[kFxIntensity].id), "fx_intensity");
  CHECK_EQ(kManifestVersion, 3);
  CHECK_EQ(std::string(m[kChaos].id), "chaos");  // the ENG and F17 rows are appended after the last shipped row
  CHECK_EQ(std::string(m[kEngineVersion].id), "engine_version");
  CHECK_EQ(std::string(m[kGrooveHumVel].id), "groove_hum_vel");
  CHECK_EQ(kEngineVersion, kChaos + 1);
  CHECK_EQ(std::string(m[kPerfMode].id), "perf_mode");  // F07 rows follow the last shipped row (slice_polish)
  CHECK_EQ(kPerfMode, kSlicePolish + 1);
  CHECK_EQ(kPerfVelocityCurve, kPerfMode + 1);
  CHECK_EQ(std::string(m[kPerfMode].choices), "Poly|Mono|Gate");
  CHECK_EQ(std::string(m[kPerfVelocityCurve].choices), "Linear|Soft|Hard|Fixed");
  CHECK(findParam("density") == &m[kDensity]);
  CHECK(findParam("seed") == nullptr);  // the seed is never an automatable parameter
  CHECK(findParam("nope") == nullptr);
}

CHOP_TEST(normalization_roundtrips_and_snaps) {
  const ParamDef& gain = *findParam("output_gain_db");
  CHECK_NEAR(normalize(gain, 0.0), 60.0 / 72.0, 1e-12);
  CHECK_NEAR(denormalize(gain, normalize(gain, -12.5)), -12.5, 1e-9);
  CHECK_NEAR(snapValue(gain, 99.0), 12.0, 0.0);
  CHECK_NEAR(snapValue(gain, std::nan("")), 0.0, 0.0);  // invalid input falls back to the default
  const ParamDef& bars = *findParam("pattern_bars");
  CHECK_NEAR(snapValue(bars, 1.6), 2.0, 0.0);
  CHECK_NEAR(denormalize(bars, 1.0), 3.0, 0.0);
  CHECK_NEAR(denormalize(bars, 0.34), 1.0, 0.0);
  const ParamDef& en = *findParam("effect_enable");
  CHECK_NEAR(snapValue(en, 0.4), 0.0, 0.0);
  CHECK_NEAR(snapValue(en, 0.6), 1.0, 0.0);
  CHECK_NEAR(denormalize(en, -4.0), 0.0, 0.0);
}

CHOP_TEST(values_default_set_and_map_to_render_params) {
  ParamValues p = ParamValues::defaults();
  CHECK_NEAR(p.get(kDensity), 0.5, 0.0);
  const render::RenderParams defaults = toRenderParams(p);
  CHECK(defaults.enabled && defaults.dryMix == 0.f && defaults.outputGainDb == 0.f);
  CHECK(defaults.slicePolish && !defaults.limiterEnabled && defaults.limiterCeilingDb == -1.f && defaults.matchGainDb == 0.f);
  CHECK_EQ(defaults.fxMask, 0);
  p.set(kAllowEcho, 1.0);
  p.set(kAllowRoll, 1.0);
  p.set(kLimiterEnable, 1.0);
  const render::RenderParams fx = toRenderParams(p, 3.5f);
  CHECK_EQ(fx.fxMask, render::fxbit::kEcho | render::fxbit::kRoll);
  CHECK(fx.limiterEnabled && fx.matchGainDb == 3.5f);
  p.set(kAllowEcho, 0.0);
  p.set(kAllowRoll, 0.0);
  p.set(kLimiterEnable, 0.0);
  const render::PlaybackOptions po = toPlaybackOptions(p);
  CHECK_EQ(po.echoRate, 1);
  CHECK_EQ(po.echoDecay, 55);
  CHECK_EQ(po.tremMode, 2);
  CHECK_EQ(po.tremDepth, 60);
  p.set(kDryMix, 2.0);
  p.set(kOutputGainDb, -6.0);
  p.set(kEffectEnable, 0.0);
  const render::RenderParams r = toRenderParams(p);
  CHECK(!r.enabled && r.dryMix == 1.f && r.outputGainDb == -6.f);
  CHECK_EQ(barsFromChoice(0), 1);
  CHECK_EQ(barsFromChoice(3), 8);
  CHECK_EQ(barsFromChoice(99), 8);
  CHECK_EQ(barsFromChoice(std::nan("")), 4);
}

CHOP_TEST(host_time_uses_host_data_when_valid) {
  HostTimeInfo h;
  h.bpm = 140.0;
  h.ppqPosition = 12.5;
  h.timeSignature = TimeSignature{3, 4};
  h.isPlaying = true;
  const TimeTranslation t = translateHostTime(h, 100.0);
  CHECK(t.block.playing && t.block.positionValid && !t.freeRunning);
  CHECK_NEAR(t.block.bpm, 140.0, 0.0);
  CHECK_NEAR(t.block.ppq, 12.5, 0.0);
  CHECK(!t.usedFallbackTempo && !t.usedFallbackMeter);
  CHECK(t.timeSignature == (TimeSignature{3, 4}));
}

CHOP_TEST(host_time_falls_back_and_says_so_when_data_is_missing_or_invalid) {
  HostTimeInfo none;
  none.isPlaying = true;
  TimeTranslation t = translateHostTime(none, 100.0);
  CHECK(t.usedFallbackTempo && t.usedFallbackMeter && t.freeRunning && !t.block.positionValid);
  CHECK_NEAR(t.block.bpm, 100.0, 0.0);
  CHECK(t.timeSignature == (TimeSignature{4, 4}));  // assumed, and flagged

  HostTimeInfo bad;
  bad.bpm = std::nan("");
  bad.ppqPosition = std::numeric_limits<double>::infinity();
  bad.timeSignature = TimeSignature{0, 7};
  t = translateHostTime(bad, std::nan(""));
  CHECK(t.usedFallbackTempo && t.usedFallbackMeter && !t.block.positionValid);
  CHECK_NEAR(t.block.bpm, 120.0, 0.0);  // even a bad manual tempo cannot produce an invalid one
  bad.bpm = 5000.0;
  CHECK(translateHostTime(bad, 90.0).usedFallbackTempo);
  bad.ppqPosition = 1e12;  // absurd position is rejected rather than trusted
  CHECK(!translateHostTime(bad, 90.0).block.positionValid);

  HostTimeInfo stopped;
  stopped.ppqPosition = 3.0;
  t = translateHostTime(stopped, 120.0);
  CHECK(!t.block.playing && !t.freeRunning && t.block.positionValid);
}

CHOP_TEST(bus_layouts) {
  CHECK(isSupportedBusLayout(2, 2) && isSupportedBusLayout(1, 1));
  CHECK(!isSupportedBusLayout(1, 2) && !isSupportedBusLayout(0, 0) && !isSupportedBusLayout(6, 6));
}

CHOP_TEST(bus_layout_rules_with_aux) {
  const int none[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  const int one[8] = {2, 0, 0, 0, 0, 0, 0, 0};
  const int gap[8] = {0, 0, 2, 0, 0, 0, 0, 2};
  const int all[8] = {2, 2, 2, 2, 2, 2, 2, 2};
  const int mono[8] = {1, 0, 0, 0, 0, 0, 0, 0};
  const int three[8] = {0, 3, 0, 0, 0, 0, 0, 0};
  CHECK(isSupportedBusLayout(2, 2, none, 8) && isSupportedBusLayout(1, 1, none, 8));
  CHECK(isSupportedBusLayout(2, 2, one, 8) && isSupportedBusLayout(2, 2, gap, 8) && isSupportedBusLayout(2, 2, all, 8));
  CHECK(!isSupportedBusLayout(1, 1, one, 8));    // a mono main bus rejects every aux bus
  CHECK(!isSupportedBusLayout(2, 2, mono, 8));   // aux buses are stereo or off
  CHECK(!isSupportedBusLayout(2, 2, three, 8));
  CHECK(!isSupportedBusLayout(2, 1, none, 8));   // the main rules are unchanged
  CHECK(!isSupportedBusLayout(0, 0, none, 8) && !isSupportedBusLayout(6, 6, none, 8));
  CHECK(isSupportedBusLayout(2, 2, nullptr, 0));
  CHECK(!isSupportedBusLayout(2, 2, nullptr, 8) && !isSupportedBusLayout(2, 2, none, -1));
}

// ---- loop position (extracted from ChopFractalProcessor::processBlock) ----
#include <chopfractal/plugin_host_adapter/loop_position.hpp>

namespace {
// Characterization oracle: the original inline logic, copied verbatim from processBlock() before the
// extraction. The extracted function must reproduce it exactly (bit for bit) for every input.
struct OldResult {
  bool boundary = false, midpoint = false;
  double playhead = -1.0;
};
OldResult oldInline(const render::TransportBlock& tb, int frames, double sampleRate, double quarters) {
  OldResult r;
  if (tb.playing && tb.positionValid && quarters > 0.0) {
    const double end = tb.ppq + static_cast<double>(frames) * tb.bpm / (60.0 * sampleRate);
    if (std::floor(tb.ppq / quarters) != std::floor(end / quarters)) r.boundary = true;
    if (std::floor((tb.ppq - 0.5 * quarters) / quarters) != std::floor((end - 0.5 * quarters) / quarters)) r.midpoint = true;
    double ph = std::fmod(tb.ppq, quarters);
    if (ph < 0.0) ph += quarters;
    r.playhead = ph;
  }
  return r;
}
}  // namespace

CHOP_TEST(loop_position_is_bit_identical_to_the_original_inline_logic) {
  const double ppqs[] = {0.0, 1e-9, 0.5, 3.999999, 4.0, 4.000001, 7.9999, 8.0, 15.99, 16.0, 1234.5678, -0.25, -8.0, 1e6, 1e9};
  const double bpms[] = {20.0, 60.0, 97.3, 120.0, 174.0, 999.0};
  const double rates[] = {44100.0, 48000.0, 96000.0, 192000.0};
  const double loops[] = {-1.0, 0.0, 1e-3, 1.0, 4.0, 7.5, 8.0, 16.0, 32.0};
  const int frameCounts[] = {1, 7, 64, 512, 2048, 16384};
  long compared = 0;
  for (bool playing : {false, true})
    for (bool valid : {false, true})
      for (double ppq : ppqs)
        for (double bpm : bpms)
          for (double sr : rates)
            for (double q : loops)
              for (int n : frameCounts) {
                render::TransportBlock tb;
                tb.playing = playing;
                tb.positionValid = valid;
                tb.ppq = ppq;
                tb.bpm = bpm;
                const OldResult want = oldInline(tb, n, sr, q);
                const LoopPosition got = computeLoopPosition(tb, n, sr, q);
                CHECK(got.boundary == want.boundary && got.midpoint == want.midpoint);
                CHECK(std::memcmp(&got.playheadQuarters, &want.playhead, sizeof(double)) == 0);
                ++compared;
              }
  CHECK_EQ(compared, 2 * 2 * 15 * 6 * 4 * 9 * 6);  // 77,760 input combinations
}

CHOP_TEST(loop_position_reports_expected_crossings_and_playhead) {
  render::TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  tb.bpm = 120.0;
  tb.ppq = 7.99;  // an 8-quarter loop ends at 8.0; 512 frames at 48 kHz is about 0.021 quarters
  auto a = computeLoopPosition(tb, 512, 48000.0, 8.0);
  CHECK(a.boundary && !a.midpoint);
  CHECK_NEAR(a.playheadQuarters, 7.99, 1e-12);
  tb.ppq = 3.99;  // the midpoint is 4.0
  auto b = computeLoopPosition(tb, 512, 48000.0, 8.0);
  CHECK(!b.boundary && b.midpoint);
  tb.ppq = 1.0;
  auto c = computeLoopPosition(tb, 512, 48000.0, 8.0);
  CHECK(!c.boundary && !c.midpoint);
  tb.ppq = -2.0;  // before the start wraps into the loop
  CHECK_NEAR(computeLoopPosition(tb, 512, 48000.0, 8.0).playheadQuarters, 6.0, 1e-12);
  tb.playing = false;
  CHECK(computeLoopPosition(tb, 512, 48000.0, 8.0).playheadQuarters == -1.0);
  tb.playing = true;
  CHECK(computeLoopPosition(tb, 512, 48000.0, 0.0).playheadQuarters == -1.0);  // no pattern
  tb.positionValid = false;
  CHECK(!computeLoopPosition(tb, 512, 48000.0, 8.0).boundary);
}

CHOP_TEST(choice_label_counts_match_their_ranges) {
  for (const ParamDef& d : parameterManifest()) {
    if (d.kind != ParamKind::Choice) continue;
    int labels = 1;
    for (const char* c = d.choices; c && *c; ++c) labels += *c == '|';
    CHECK_EQ(labels, static_cast<int>(d.maxValue) + 1);
    CHECK_EQ(d.minValue, 0.0);
  }
  // The engine defaults: new projects use the break engine, Jungle profile, standard backbeat, profile anchors.
  const ParamValues v = ParamValues::defaults();
  CHECK_EQ(v.get(kEngineVersion), 1.0);
  CHECK_EQ(v.get(kBreakProfile), 1.0);
  CHECK_EQ(v.get(kBreakBackbeat), 0.0);
  CHECK_EQ(v.get(kBreakAnchors), 0.0);
  CHECK_EQ(v.get(kGrooveEnable), 0.0);
  CHECK_NEAR(snapValue(*findParam("groove_swing"), 58.3), 58.5, 1e-9);
  CHECK_NEAR(snapValue(*findParam("groove_hum_time"), 99.0), 30.0, 0.0);
}

CHOP_TEST(pad_parameters_map_to_the_renderer_pad_params) {
  ParamValues v = ParamValues::defaults();
  CHECK(toPadParams(v).mode == render::PadMode::Poly && toPadParams(v).curve == render::VelocityCurve::Linear);  // what a project without the rows gets
  v.set(kPerfMode, 2.0);
  v.set(kPerfVelocityCurve, 3.0);
  CHECK(toPadParams(v).mode == render::PadMode::Gate && toPadParams(v).curve == render::VelocityCurve::Fixed);
  v.set(kPerfMode, 1.0);
  v.set(kPerfVelocityCurve, 2.0);
  CHECK(toPadParams(v).mode == render::PadMode::Mono && toPadParams(v).curve == render::VelocityCurve::Hard);
}
