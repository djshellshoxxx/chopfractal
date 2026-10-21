#include <chopfractal/audio_renderer/slice_quality.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::render {
namespace {

constexpr double kPi = 3.14159265358979323846;

float monoAt(const SourceData& s, std::int64_t i) {
  const float a = s.channel(0)[i];
  return s.channels > 1 ? 0.5f * (a + s.channel(1)[i]) : a;
}

double toleranceDb(SliceClass c) {
  switch (c) {
    case SliceClass::Kick: return 2.0;
    case SliceClass::Snare: return 1.5;
    case SliceClass::Hat: return 2.0;
    case SliceClass::Cymbal: return 2.0;
    default: return 3.0;
  }
}

}  // namespace

const char* sliceClassName(SliceClass c) {
  switch (c) {
    case SliceClass::Kick: return "kick";
    case SliceClass::Snare: return "snare";
    case SliceClass::Hat: return "hat";
    case SliceClass::Cymbal: return "cymbal";
    default: return "other";
  }
}

SliceAnalysis analyzeSlice(const SourceData& src, SampleRange region) {
  SliceAnalysis out;
  out.start = region.start;
  if (src.frames <= 0 || src.samples.empty() || region.empty() || region.start < 0 || region.end > src.frames) return out;
  const double msFrames = static_cast<double>(src.sampleRate) / 1000.0;
  auto ms = [&](double m) { return static_cast<std::int64_t>(std::llround(m * msFrames)); };

  // 1. peak of the attack region.
  const std::int64_t e10 = std::min(region.end, region.start + ms(10.0));
  double peak = 0.0;
  for (std::int64_t i = region.start; i < e10; ++i) peak = std::max(peak, static_cast<double>(std::fabs(monoAt(src, i))));
  out.peak10 = peak;

  // 2. Transient-aware start (research 7.1): when the attack is already under way at the chop start, back up to a quiet
  // zero crossing within 2 ms before it. Never move later; without a crossing the renderer's fade does the job.
  std::int64_t start = region.start;
  if (peak > 1e-4 && std::fabs(monoAt(src, start)) >= 0.25 * peak) {
    const std::int64_t lo = std::max<std::int64_t>(0, start - ms(2.0));
    for (std::int64_t i = start; i > lo; --i) {
      const float a = monoAt(src, i - 1), b = monoAt(src, i);
      if ((a <= 0.f && b >= 0.f) || (a >= 0.f && b <= 0.f)) {
        if (std::fabs(a) <= 0.1 * peak) {
          start = i - 1;
          break;
        }
      }
    }
  }
  out.start = start;
  out.moved = start < region.start;

  // 3. Loudness of the first 50 ms after the onset, and band balance for the class.
  const std::int64_t e50 = std::min(region.end, start + ms(50.0));
  const std::int64_t n = e50 - start;
  if (n <= 0) return out;
  const double aLo = 1.0 - std::exp(-2.0 * kPi * 250.0 / src.sampleRate);
  const double aHi = 1.0 - std::exp(-2.0 * kPi * 4000.0 / src.sampleRate);
  double lpLo = 0.0, lpHi = 0.0, eTot = 0.0, eLo = 0.0, eHi = 0.0, peak50 = 0.0, eFirst = 0.0, eLast = 0.0;
  for (std::int64_t i = start; i < e50; ++i) {
    const double x = monoAt(src, i);
    if (i - start < n / 3) eFirst += x * x;
    if (i - start >= n - n / 3) eLast += x * x;
    lpLo += aLo * (x - lpLo);
    lpHi += aHi * (x - lpHi);
    const double hi = x - lpHi;
    eTot += x * x;
    eLo += lpLo * lpLo;
    eHi += hi * hi;
    peak50 = std::max(peak50, std::fabs(x));
  }
  out.rms50 = std::sqrt(eTot / static_cast<double>(n));
  if (eTot <= 1e-12) return out;
  const double lo = eLo / eTot, hi = eHi / eTot;
  const double regionSeconds = static_cast<double>(region.length()) / static_cast<double>(src.sampleRate);
  if (lo > 0.6) {
    out.cls = SliceClass::Kick;
  } else if (hi > 0.4) {
    out.cls = regionSeconds >= 0.2 ? SliceClass::Cymbal : SliceClass::Hat;
  } else if (peak50 > 0.0 && out.peak10 >= 0.6 * peak50 && eLast <= 0.8 * eFirst) {  // an attack that decays, not a steady tone
    out.cls = SliceClass::Snare;
  }
  return out;
}

std::vector<float> matchTrims(const std::vector<SliceAnalysis>& slices) {
  std::vector<float> trims(slices.size(), 1.f);
  for (int c = 0; c < 5; ++c) {
    const SliceClass cls = static_cast<SliceClass>(c);
    std::vector<double> levels;
    for (const SliceAnalysis& s : slices)
      if (s.cls == cls && s.rms50 > 1e-5) levels.push_back(20.0 * std::log10(s.rms50));
    if (levels.size() < 2) continue;
    std::sort(levels.begin(), levels.end());
    const double target = levels.size() % 2 ? levels[levels.size() / 2] : 0.5 * (levels[levels.size() / 2 - 1] + levels[levels.size() / 2]);
    const double tol = toleranceDb(cls);
    for (std::size_t i = 0; i < slices.size(); ++i) {
      if (slices[i].cls != cls || slices[i].rms50 <= 1e-5) continue;
      const double diff = target - 20.0 * std::log10(slices[i].rms50);
      const double outside = diff - std::min(tol, std::max(-tol, diff));  // only the part beyond the tolerance is corrected
      const double db = std::min(9.0, std::max(-9.0, outside));
      trims[i] = static_cast<float>(std::pow(10.0, db / 20.0));
    }
  }
  return trims;
}

}  // namespace chopfractal::render
