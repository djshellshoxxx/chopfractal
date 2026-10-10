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
  CHECK_EQ(kManifestVersion, 2);
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
