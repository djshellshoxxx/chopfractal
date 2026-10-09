#include <chopfractal/chop_insight/insight.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::insight {
namespace {
constexpr double kPi = 3.14159265358979323846;
double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
}  // namespace

const char* roleName(Role r) {
  switch (r) {
    case Role::Kick: return "kick";
    case Role::Snare: return "snare";
    case Role::Hat: return "hat";
    case Role::Cymbal: return "cymbal";
    default: return "other";
  }
}

Result<ChopFeatures> analyzeChop(const SourceView& s, SampleRange r) {
  if (!s.channels || s.numChannels < 1 || s.sampleRate < 8000 || r.start < 0 || r.end > s.frames || r.empty())
    return makeError(ErrorCode::InvalidArgument, "invalid source or range");
  const std::int64_t n = r.length();
  ChopFeatures f;
  f.lengthMs = 1000.0 * static_cast<double>(n) / s.sampleRate;
  if (n < 2) return makeError(ErrorCode::InvalidArgument, "range is too short to analyze");
  const double aLow = 1.0 - std::exp(-2.0 * kPi * 200.0 / s.sampleRate);
  const double aHigh = 1.0 - std::exp(-2.0 * kPi * 5000.0 / s.sampleRate);
  double lpLow = 0, lpHigh = 0, eLow = 0, eMid = 0, eHigh = 0, sumSq = 0, prev = 0;
  const std::int64_t win = std::max<std::int64_t>(1, static_cast<std::int64_t>(0.005 * s.sampleRate));
  std::vector<double> env;
  double winSum = 0;
  std::int64_t crossings = 0, inWin = 0;
  for (std::int64_t i = 0; i < n; ++i) {
    double x = 0;
    for (int c = 0; c < s.numChannels; ++c) x += static_cast<double>(s.channels[c][r.start + i]);
    x /= s.numChannels;
    if (!std::isfinite(x)) x = 0;
    lpLow += aLow * (x - lpLow);
    lpHigh += aHigh * (x - lpHigh);
    const double hi = x - lpHigh, mid = lpHigh - lpLow;
    eLow += lpLow * lpLow;
    eMid += mid * mid;
    eHigh += hi * hi;
    sumSq += x * x;
    f.peak = std::max(f.peak, std::fabs(x));
    if (i > 0 && ((x >= 0) != (prev >= 0))) ++crossings;
    prev = x;
    winSum += x * x;
    if (++inWin == win) {
      env.push_back(std::sqrt(winSum / static_cast<double>(win)));
      winSum = 0;
      inWin = 0;
    }
  }
  if (inWin > 0) env.push_back(std::sqrt(winSum / static_cast<double>(inWin)));
  const double total = eLow + eMid + eHigh;
  if (!(total > 1e-12) || !(f.peak > 1e-5)) return makeError(ErrorCode::InvalidArgument, "the range is silent");
  f.lowRatio = eLow / total;
  f.midRatio = eMid / total;
  f.highRatio = eHigh / total;
  f.zcr = static_cast<double>(crossings) / static_cast<double>(n - 1);
  f.rms = std::sqrt(sumSq / static_cast<double>(n));
  std::size_t peakIdx = 0;
  for (std::size_t i = 0; i < env.size(); ++i)
    if (env[i] > env[peakIdx]) peakIdx = i;
  const double floorLevel = env[peakIdx] * std::pow(10.0, -30.0 / 20.0);
  std::size_t lastIdx = peakIdx;
  for (std::size_t i = peakIdx; i < env.size(); ++i)
    if (env[i] >= floorLevel) lastIdx = i;
  f.decayMs = std::min(f.lengthMs, 1000.0 * static_cast<double>(lastIdx + 1 - peakIdx) * static_cast<double>(win) / s.sampleRate);
  return f;
}

Role classify(const ChopFeatures& f, double* confidence, std::string* reason) {
  auto set = [&](Role r, double c, const char* why) {
    if (confidence) *confidence = clamp01(c);
    if (reason) *reason = why;
    return r;
  };
  if (f.lengthMs < 5.0) return set(Role::Other, 0.2, "too short to classify");
  if (f.lowRatio > 0.55 && f.zcr < 0.06)
    return set(Role::Kick, 0.55 + (f.lowRatio - 0.55) + (0.06 - f.zcr) * 3.0, "energy is mostly low, with few zero crossings");
  if (f.highRatio >= 0.55) {
    const double c = 0.5 + (f.highRatio - 0.55);
    if (f.decayMs > 300.0) return set(Role::Cymbal, c + std::min(0.3, (f.decayMs - 300.0) / 2000.0), "bright and long decay");
    return set(Role::Hat, c + std::min(0.3, (300.0 - f.decayMs) / 1000.0), "bright and short decay");
  }
  if (f.lowRatio + f.midRatio >= 0.35 && f.zcr >= 0.03 && f.decayMs >= 40.0 && f.decayMs <= 600.0)
    return set(Role::Snare, 0.5 + 0.3 * std::min(1.0, f.zcr / 0.2), "noisy broadband body with a medium decay");
  return set(Role::Other, 0.3, "no clear percussion profile");
}

std::vector<RoleSuggestion> suggestRoles(const SourceView& source, const ChopSnapshot& chops) {
  std::vector<RoleSuggestion> out;
  for (const ChopInfo& c : chops.chops) {
    RoleSuggestion s;
    s.chop = c.id;
    auto f = analyzeChop(source, c.range);
    if (f.ok()) {
      s.role = classify(f.value(), &s.confidence, &s.reason);
    } else {
      s.role = Role::Other;
      s.confidence = 0.1;
      s.reason = "could not analyze: " + f.error().message;
    }
    out.push_back(std::move(s));
  }
  return out;
}

std::vector<LoopSuggestion> suggestLoop(double seconds, double hostBpm) {
  std::vector<LoopSuggestion> out;
  if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 3600.0) return out;
  std::vector<double> score;
  for (int beats : {4, 8, 16, 32}) {
    const double bpm = beats * 60.0 / seconds;
    if (bpm < 60.0 || bpm > 200.0) continue;
    double sc;
    if (std::isfinite(hostBpm) && hostBpm > 0.0) {
      sc = 1.0 / (1.0 + 10.0 * std::fabs(bpm - hostBpm) / hostBpm);
    } else {
      const double d = bpm < 100.0 ? 100.0 - bpm : bpm > 130.0 ? bpm - 130.0 : 0.0;
      sc = 1.0 / (1.0 + d / 30.0);
    }
    LoopSuggestion l;
    l.beats = beats;
    l.bars = std::max(1, beats / 4);
    l.bpm = bpm;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d bar%s at %.1f BPM", l.bars, l.bars == 1 ? "" : "s", bpm);
    l.note = buf;
    out.push_back(l);
    score.push_back(sc);
  }
  double sum = 0;
  for (double s : score) sum += s;
  for (std::size_t i = 0; i < out.size(); ++i) out[i].confidence = clamp01(score[i] / sum);
  std::vector<std::size_t> idx(out.size());
  for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
  std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return score[a] > score[b]; });
  std::vector<LoopSuggestion> sorted;
  for (std::size_t i : idx) sorted.push_back(out[i]);
  return sorted;
}

}  // namespace chopfractal::insight
