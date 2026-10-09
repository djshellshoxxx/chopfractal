#include <chopfractal/source_chop/analysis.hpp>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace chopfractal::source {
namespace {
constexpr std::int64_t kWindow = 512;
constexpr std::int64_t kHop = 128;

float monoAt(const float* const* ch, int n, std::int64_t i) {
  float s = 0.f;
  for (int c = 0; c < n; ++c) s += ch[c][i];
  return s / static_cast<float>(n);
}
}  // namespace

Proposal detectTransients(const float* const* channels, int numChannels, std::int64_t frames, std::int32_t sampleRate,
                          const AnalysisParams& params) {
  Proposal out;
  if (!channels || numChannels < 1 || frames <= 0 || sampleRate <= 0) return out;
  const std::int64_t begin = std::max<std::int64_t>(0, params.rangeStart);
  const std::int64_t end = (params.rangeEnd < 0) ? frames : std::min(frames, params.rangeEnd);
  if (end - begin < kWindow) {
    out.markers.push_back({begin, 1.f});
    return out;
  }
  const std::int64_t minSpacing = params.minSpacingFrames > 0 ? params.minSpacingFrames : std::max<std::int64_t>(kHop * 2, sampleRate / 50);
  const double sens = std::min(1.0, std::max(0.0, params.sensitivity));

  // Short-time RMS envelope.
  const std::int64_t hops = (end - begin - kWindow) / kHop + 1;
  std::vector<float> env(static_cast<std::size_t>(hops));
  for (std::int64_t k = 0; k < hops; ++k) {
    double e = 0.0;
    const std::int64_t s0 = begin + k * kHop;
    for (std::int64_t i = 0; i < kWindow; ++i) {
      const float v = monoAt(channels, numChannels, s0 + i);
      e += static_cast<double>(v) * v;
    }
    env[static_cast<std::size_t>(k)] = static_cast<float>(std::sqrt(e / static_cast<double>(kWindow)));
  }
  // Onset strength: positive envelope change relative to the local average energy.
  std::vector<float> strength(env.size(), 0.f);
  float maxS = 0.f;
  for (std::size_t k = 1; k < env.size(); ++k) {
    const std::size_t lo = k > 16 ? k - 16 : 0;
    const std::size_t hi = std::min(env.size(), k + 17);
    double mean = 0.0;
    for (std::size_t j = lo; j < hi; ++j) mean += env[j];
    mean /= static_cast<double>(hi - lo);
    const float d = std::max(0.f, env[k] - env[k - 1]);
    strength[k] = static_cast<float>(d / (mean + 1e-6));
    maxS = std::max(maxS, strength[k]);
  }
  if (maxS <= 0.f) {
    out.markers.push_back({begin, 1.f});
    return out;
  }
  for (float& s : strength) s /= maxS;

  const float threshold = static_cast<float>(0.6 - 0.55 * sens);
  struct Cand {
    std::int64_t pos;
    float strength;
  };
  std::vector<Cand> cands;
  for (std::size_t k = 1; k < strength.size(); ++k) {
    if (strength[k] < threshold) continue;
    bool peak = true;
    for (std::size_t j = (k > 2 ? k - 2 : 0); j <= std::min(strength.size() - 1, k + 2); ++j)
      if (strength[j] > strength[k]) peak = false;
    if (!peak) continue;
    // Refine: find the loudest sample after the rise, then walk back to where the attack begins.
    const std::int64_t s0 = begin + static_cast<std::int64_t>(k) * kHop;
    std::int64_t peakPos = s0;
    float peakVal = 0.f;
    for (std::int64_t i = s0; i < std::min(end, s0 + kWindow); ++i) {
      const float a = std::fabs(monoAt(channels, numChannels, i));
      if (a > peakVal) {
        peakVal = a;
        peakPos = i;
      }
    }
    std::int64_t pos = peakPos;
    const std::int64_t floorPos = std::max(begin, s0 - kWindow);
    while (pos > floorPos && std::fabs(monoAt(channels, numChannels, pos - 1)) > 0.1f * peakVal) --pos;
    cands.push_back({pos, strength[k]});
  }
  // Minimum spacing: strongest candidates win.
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.strength != b.strength ? a.strength > b.strength : a.pos < b.pos; });
  std::vector<Cand> kept;
  for (const Cand& c : cands) {
    bool ok = std::llabs(c.pos - begin) >= minSpacing;
    for (const Cand& k : kept)
      if (std::llabs(k.pos - c.pos) < minSpacing) ok = false;
    if (ok) kept.push_back(c);
  }
  std::sort(kept.begin(), kept.end(), [](const Cand& a, const Cand& b) { return a.pos < b.pos; });
  out.markers.push_back({begin, 1.f});
  for (const Cand& c : kept) out.markers.push_back({c.pos, c.strength});
  return out;
}

Proposal evenGrid(std::int64_t frames, const GridSpec& spec) {
  Proposal out;
  if (frames <= 0 || spec.division < 1 || !(spec.loopBeats > 0.0)) return out;
  double cellBeats = 4.0 / spec.division;
  if (spec.kind == GridKind::Triplet) cellBeats *= 2.0 / 3.0;
  if (spec.kind == GridKind::Dotted) cellBeats *= 1.5;
  const std::int64_t cells = static_cast<std::int64_t>(std::floor(spec.loopBeats / cellBeats + 1e-9));
  for (std::int64_t k = 0; k < cells; ++k) {
    const std::int64_t pos = std::llround(static_cast<double>(k) * static_cast<double>(frames) * cellBeats / spec.loopBeats);
    if (pos >= frames) break;
    out.markers.push_back({pos, 1.f});
  }
  return out;
}

std::int64_t snapToNearest(std::int64_t position, const std::vector<std::int64_t>& targets, std::int64_t radius) {
  std::int64_t best = position;
  std::int64_t bestDist = radius + 1;
  for (std::int64_t t : targets) {
    const std::int64_t d = std::llabs(t - position);
    if (d <= radius && d < bestDist) {
      bestDist = d;
      best = t;
    }
  }
  return best;
}

std::int64_t snapToZeroCrossing(const float* channel, std::int64_t frames, std::int64_t position, std::int64_t radius) {
  if (!channel || frames < 2) return position;
  for (std::int64_t d = 0; d <= radius; ++d) {
    for (int sign = -1; sign <= 1; sign += 2) {
      const std::int64_t i = position + sign * d;
      if (i < 0 || i + 1 >= frames) continue;
      const bool cross = (channel[i] <= 0.f && channel[i + 1] > 0.f) || (channel[i] >= 0.f && channel[i + 1] < 0.f);
      if (cross) return std::fabs(channel[i]) <= std::fabs(channel[i + 1]) ? i : i + 1;
    }
  }
  return position;
}

SourceId computeSourceId(const float* const* channels, int numChannels, std::int64_t frames, std::int32_t sampleRate) {
  std::uint64_t h = 0xCBF29CE484222325ull;
  auto mixIn = [&](const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 0x100000001B3ull;
    }
  };
  mixIn(&sampleRate, sizeof(sampleRate));
  mixIn(&numChannels, sizeof(numChannels));
  mixIn(&frames, sizeof(frames));
  for (int c = 0; c < numChannels; ++c)
    if (channels && channels[c] && frames > 0) mixIn(channels[c], static_cast<std::size_t>(frames) * sizeof(float));
  return SourceId{h == 0 ? 1 : h};
}

}  // namespace chopfractal::source
