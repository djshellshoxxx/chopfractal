#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

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
      {"perf_mode", "Pad Mode", "", ParamKind::Choice, 0.0, 2.0, 0.0, 1.0, 0.0, 4, "Poly|Mono|Gate"},
      {"perf_velocity_curve", "Velocity Curve", "", ParamKind::Choice, 0.0, 3.0, 0.0, 1.0, 0.0, 4, "Linear|Soft|Hard|Fixed"},
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

render::RenderParams toRenderParams(const ParamValues& values) {
  render::RenderParams p;
  p.enabled = values.get(kEffectEnable) >= 0.5;
  p.dryMix = static_cast<float>(values.get(kDryMix));
  p.outputGainDb = static_cast<float>(values.get(kOutputGainDb));
  return p;
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

namespace {
std::string numberText(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%g", v);
  return buf;
}
std::string choiceLabel(const ParamDef& def, int index) {
  std::string all = def.choices ? def.choices : "";
  int i = 0;
  std::size_t start = 0;
  while (true) {
    const std::size_t bar = all.find('|', start);
    const std::string item = all.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
    if (i == index) return item;
    if (bar == std::string::npos) return {};
    start = bar + 1;
    ++i;
  }
}
}  // namespace

std::string describeParam(const ParamDef& def, double plain) {
  const double v = snapValue(def, plain);
  const std::string unit = def.unit ? def.unit : "";
  const std::string name = def.name;
  if (def.kind == ParamKind::Bool) return name + ": " + (v >= 0.5 ? "on" : "off") + " (default " + (def.defaultValue >= 0.5 ? "on" : "off") + ")";
  if (def.kind == ParamKind::Choice) {
    std::string all = def.choices ? def.choices : "";
    std::string list;
    for (char c : all) list += c == '|' ? std::string(", ") : std::string(1, c);
    return name + ": " + choiceLabel(def, static_cast<int>(std::lround(v))) + (unit.empty() ? "" : " " + unit) + " (choices: " + list + "; default " + choiceLabel(def, static_cast<int>(std::lround(def.defaultValue))) + ")";
  }
  if (unit == "%") {
    return name + ": " + numberText(std::round(v * 100.0)) + "% (range " + numberText(def.minValue * 100.0) + "% to " + numberText(def.maxValue * 100.0) + "%, default " +
           numberText(def.defaultValue * 100.0) + "%)";
  }
  char value[48];
  std::snprintf(value, sizeof value, "%.2f", v);
  const std::string suffix = unit.empty() ? "" : " " + unit;
  return name + ": " + value + suffix + " (range " + numberText(def.minValue) + " to " + numberText(def.maxValue) + suffix + ", default " + numberText(def.defaultValue) + suffix + ")";
}

bool isSupportedBusLayout(int inputChannels, int outputChannels) {
  return (inputChannels == 1 || inputChannels == 2) && inputChannels == outputChannels;
}

bool isSupportedBusLayout(int inputChannels, int outputChannels, const int* aux, int numAux) {
  if (!isSupportedBusLayout(inputChannels, outputChannels)) return false;
  for (int i = 0; aux && i < numAux; ++i) {
    if (aux[i] == 0) continue;
    if (aux[i] != 2 || inputChannels != 2) return false;
  }
  return true;
}

}  // namespace chopfractal::host
