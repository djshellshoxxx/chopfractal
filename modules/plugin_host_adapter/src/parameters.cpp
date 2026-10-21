#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::host {

const std::vector<ParamDef>& parameterManifest() {
  // Append-only. Order matches ParamIndex.
  static const std::vector<ParamDef> manifest{
      {"variation_amount", "Variation Amount", "%", ParamKind::Float, 0.0, 1.0, 0.5, 0.0, 20.0, 1, nullptr},
      {"density", "Density", "%", ParamKind::Float, 0.0, 1.0, 0.5, 0.0, 20.0, 1, nullptr},
      {"swing", "Swing", "%", ParamKind::Float, 0.0, 1.0, 0.0, 0.0, 20.0, 1, nullptr},
      {"pattern_bars", "Pattern Length", "bars", ParamKind::Choice, 0.0, 3.0, 2.0, 1.0, 0.0, 1, "1|2|4|8"},
      {"dry_mix", "Dry / Source Mix", "%", ParamKind::Float, 0.0, 1.0, 0.0, 0.0, 10.0, 1, nullptr},
      {"output_gain_db", "Output Gain", "dB", ParamKind::Float, -60.0, 12.0, 0.0, 0.0, 10.0, 1, nullptr},  // 0 dB = unity
      {"effect_enable", "Effect Enable", "", ParamKind::Bool, 0.0, 1.0, 1.0, 1.0, 0.0, 1, nullptr},
      {"allow_reverse", "Allow Reverse", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 1, nullptr},
      {"allow_pitch", "Allow Pitch", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 1, nullptr},
      {"allow_retrigger", "Allow Retrigger", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 1, nullptr},
      {"allow_filter", "Allow Filter", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 2, nullptr},
      {"allow_glide", "Allow Tape Glide", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 2, nullptr},
      {"allow_crunch", "Allow Crunch", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 2, nullptr},
      {"fx_intensity", "Effect Intensity", "%", ParamKind::Float, 0.0, 1.0, 0.5, 0.0, 20.0, 2, nullptr},
      {"chaos", "Chaos", "%", ParamKind::Float, 0.0, 1.0, 0.0, 0.0, 20.0, 3, nullptr},
      {"level_match", "Level Match", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 3, nullptr},
      {"match_trim_db", "Match Trim", "dB", ParamKind::Float, -6.0, 6.0, 0.0, 0.0, 10.0, 3, nullptr},
      {"limiter_enable", "Safety Limiter", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 3, nullptr},
      {"limiter_ceiling_db", "Limiter Ceiling", "dB", ParamKind::Float, -6.0, 0.0, -1.0, 0.0, 10.0, 3, nullptr},
      {"allow_echo", "Allow Echo", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 3, nullptr},
      {"echo_amount", "Echo Amount", "%", ParamKind::Float, 0.0, 100.0, 40.0, 0.0, 10.0, 3, nullptr},
      {"echo_rate", "Echo Rate", "", ParamKind::Choice, 0.0, 3.0, 1.0, 1.0, 0.0, 3, "1/16|1/8|Dotted 1/8|1/4"},
      {"echo_decay", "Echo Decay", "%", ParamKind::Float, 10.0, 90.0, 55.0, 0.0, 10.0, 3, nullptr},
      {"echo_tone", "Echo Tone", "%", ParamKind::Float, 0.0, 100.0, 50.0, 0.0, 10.0, 3, nullptr},
      {"allow_tremolo", "Allow Tremolo Pan", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 3, nullptr},
      {"trem_amount", "Tremolo Pan Amount", "%", ParamKind::Float, 0.0, 100.0, 30.0, 0.0, 10.0, 3, nullptr},
      {"trem_mode", "Tremolo Pan Mode", "", ParamKind::Choice, 0.0, 2.0, 2.0, 1.0, 0.0, 3, "Tremolo|Pan|Both"},
      {"trem_rate", "Tremolo Pan Rate", "", ParamKind::Choice, 0.0, 3.0, 1.0, 1.0, 0.0, 3, "1/16|1/8|1/4|1/2"},
      {"trem_depth", "Tremolo Pan Depth", "%", ParamKind::Float, 0.0, 100.0, 60.0, 0.0, 10.0, 3, nullptr},
      {"allow_stutter", "Allow Stutter", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 3, nullptr},
      {"stutter_amount", "Stutter Amount", "%", ParamKind::Float, 0.0, 100.0, 25.0, 0.0, 10.0, 3, nullptr},
      {"stutter_rate", "Stutter Rate", "", ParamKind::Choice, 0.0, 9.0, 4.0, 1.0, 0.0, 3, "1/4|1/4T|1/8|1/8T|1/16|1/16T|1/32|1/32T|1/64|1/64T"},
      {"stutter_gate", "Stutter Gate", "%", ParamKind::Float, 10.0, 100.0, 80.0, 0.0, 10.0, 3, nullptr},
      {"stutter_decay_db", "Stutter Decay", "dB", ParamKind::Float, -12.0, 6.0, -1.5, 0.0, 10.0, 3, nullptr},
      {"stutter_pitch", "Stutter Pitch Ramp", "st", ParamKind::Float, -12.0, 12.0, 0.0, 0.0, 10.0, 3, nullptr},
      {"allow_roll", "Allow Roll", "", ParamKind::Bool, 0.0, 1.0, 0.0, 1.0, 0.0, 3, nullptr},
      {"roll_amount", "Roll Amount", "%", ParamKind::Float, 0.0, 100.0, 20.0, 0.0, 10.0, 3, nullptr},
      {"roll_rate", "Roll Start Rate", "", ParamKind::Choice, 0.0, 9.0, 4.0, 1.0, 0.0, 3, "1/4|1/4T|1/8|1/8T|1/16|1/16T|1/32|1/32T|1/64|1/64T"},
      {"roll_rate_end", "Roll End Rate", "", ParamKind::Choice, 0.0, 9.0, 8.0, 1.0, 0.0, 3, "1/4|1/4T|1/8|1/8T|1/16|1/16T|1/32|1/32T|1/64|1/64T"},
      {"roll_freeze", "Roll Freeze Point", "%", ParamKind::Float, 0.0, 100.0, 0.0, 0.0, 10.0, 3, nullptr},
      {"slice_polish", "Slice Polish", "", ParamKind::Bool, 0.0, 1.0, 1.0, 1.0, 0.0, 3, nullptr},
  };
  return manifest;
}

const ParamDef* findParam(std::string_view id) {
  for (const ParamDef& d : parameterManifest())
    if (id == d.id) return &d;
  return nullptr;
}

double snapValue(const ParamDef& def, double plain) {
  if (!std::isfinite(plain)) return def.defaultValue;
  double v = std::min(def.maxValue, std::max(def.minValue, plain));
  if (def.step > 0.0) v = def.minValue + std::round((v - def.minValue) / def.step) * def.step;
  return std::min(def.maxValue, std::max(def.minValue, v));
}

double normalize(const ParamDef& def, double plain) {
  const double range = def.maxValue - def.minValue;
  return range > 0.0 ? (snapValue(def, plain) - def.minValue) / range : 0.0;
}

double denormalize(const ParamDef& def, double normalized) {
  const double n = std::isfinite(normalized) ? std::min(1.0, std::max(0.0, normalized)) : 0.0;
  return snapValue(def, def.minValue + n * (def.maxValue - def.minValue));
}

ParamValues ParamValues::defaults() {
  ParamValues p;
  const auto& m = parameterManifest();
  for (std::size_t i = 0; i < kParamCount && i < m.size(); ++i) p.v[i] = m[i].defaultValue;
  return p;
}

void ParamValues::set(ParamIndex i, double plain) { v[i] = snapValue(parameterManifest()[i], plain); }

int barsFromChoice(double choiceIndex) {
  static const int bars[4] = {1, 2, 4, 8};
  const long i = std::lround(std::isfinite(choiceIndex) ? choiceIndex : 2.0);
  return bars[std::min(3L, std::max(0L, i))];
}

render::RenderParams toRenderParams(const ParamValues& values, float matchGainDb) {
  render::RenderParams p;
  p.slicePolish = values.get(kSlicePolish) >= 0.5;
  p.limiterEnabled = values.get(kLimiterEnable) >= 0.5;
  p.limiterCeilingDb = static_cast<float>(values.get(kLimiterCeilingDb));
  p.matchGainDb = std::isfinite(matchGainDb) ? matchGainDb : 0.f;
  std::uint8_t mask = 0;
  if (values.get(kAllowEcho) >= 0.5) mask |= render::fxbit::kEcho;
  if (values.get(kAllowTremolo) >= 0.5) mask |= render::fxbit::kTremPan;
  if (values.get(kAllowStutter) >= 0.5) mask |= render::fxbit::kStutter;
  if (values.get(kAllowRoll) >= 0.5) mask |= render::fxbit::kRoll;
  p.fxMask = mask;
  p.enabled = values.get(kEffectEnable) >= 0.5;
  p.dryMix = static_cast<float>(values.get(kDryMix));
  p.outputGainDb = static_cast<float>(values.get(kOutputGainDb));
  return p;
}

render::PlaybackOptions toPlaybackOptions(const ParamValues& values) {
  render::PlaybackOptions o;
  o.echoRate = static_cast<std::uint8_t>(std::lround(values.get(kEchoRate)));
  o.echoDecay = static_cast<std::uint8_t>(std::lround(values.get(kEchoDecay)));
  o.echoTone = static_cast<std::uint8_t>(std::lround(values.get(kEchoTone)));
  o.tremMode = static_cast<std::uint8_t>(std::lround(values.get(kTremMode)));
  o.tremRate = static_cast<std::uint8_t>(std::lround(values.get(kTremRate)));
  o.tremDepth = static_cast<std::uint8_t>(std::lround(values.get(kTremDepth)));
  return o;
}

TimeTranslation translateHostTime(const HostTimeInfo& host, double manualBpm, TimeSignature fallbackMeter) {
  TimeTranslation t;
  t.block.playing = host.isPlaying;
  const bool hostBpmOk = host.bpm && std::isfinite(*host.bpm) && *host.bpm >= 20.0 && *host.bpm <= 999.0;
  if (hostBpmOk) {
    t.block.bpm = *host.bpm;
  } else {
    t.usedFallbackTempo = true;
    t.block.bpm = (std::isfinite(manualBpm) && manualBpm >= 20.0 && manualBpm <= 999.0) ? manualBpm : 120.0;
  }
  if (host.ppqPosition && std::isfinite(*host.ppqPosition) && std::fabs(*host.ppqPosition) < 1e9) {
    t.block.positionValid = true;
    t.block.ppq = *host.ppqPosition;
  } else {
    t.block.positionValid = false;
    t.freeRunning = host.isPlaying;
  }
  if (host.timeSignature && host.timeSignature->valid()) {
    t.timeSignature = *host.timeSignature;
  } else {
    t.usedFallbackMeter = true;
    t.timeSignature = fallbackMeter.valid() ? fallbackMeter : TimeSignature{4, 4};
  }
  return t;
}

bool isSupportedBusLayout(int inputChannels, int outputChannels) {
  return (inputChannels == 1 || inputChannels == 2) && inputChannels == outputChannels;
}

}  // namespace chopfractal::host
