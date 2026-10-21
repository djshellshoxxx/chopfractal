#include <chopfractal/time_stretch/stretch.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>

namespace chopfractal::stretch {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMinSampleRate = 8000;
constexpr int kMaxSampleRate = 384000;

// ---- plan helpers ----
std::int64_t scaled(std::int64_t x, std::int64_t in, std::int64_t out) {
  if (in <= 0 || out <= 0 || in > kMaxPlanFrames || out > kMaxPlanFrames) return 0;
  return (2 * x * out + in) / (2 * in);
}

// ---- progress ----
struct Reporter {
  const Progress* fn = nullptr;
  double total = 1.0;
  double done = 0.0;
  bool cancelled = false;
  std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();

  // Cheap enough to call per frame: the clock is read, the callback only when 15 ms passed (or when forced).
  bool report(bool force) {
    if (!fn || !*fn) return true;
    const auto now = std::chrono::steady_clock::now();
    if (force || now - last >= std::chrono::milliseconds(15)) {
      last = now;
      if (!(*fn)(std::min(1.0, done / total))) cancelled = true;
    }
    return !cancelled;
  }
};

// ---- kernels ----
// Four independent accumulators in a fixed order: deterministic and friendly to vectorization.
inline double dotf(const float* a, const float* b, int n) {
  double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
  int i = 0;
  for (; i + 4 <= n; i += 4) {
    s0 += static_cast<double>(a[i]) * b[i];
    s1 += static_cast<double>(a[i + 1]) * b[i + 1];
    s2 += static_cast<double>(a[i + 2]) * b[i + 2];
    s3 += static_cast<double>(a[i + 3]) * b[i + 3];
  }
  for (; i < n; ++i) s0 += static_cast<double>(a[i]) * b[i];
  return (s0 + s1) + (s2 + s3);
}

struct Candidate {
  std::int64_t p = 0;
  double score = -2.0;
  bool set = false;
};

// Better when the score is larger; ties go to the smaller |offset from center|, then to the negative offset.
inline bool better(double score, std::int64_t p, std::int64_t center, const Candidate& best) {
  if (!best.set) return true;
  if (score != best.score) return score > best.score;
  const std::int64_t da = p > center ? p - center : center - p;
  const std::int64_t db = best.p > center ? best.p - center : center - best.p;
  if (da != db) return da < db;
  return p < center && best.p > center;
}

struct SearchSet {
  const float* x[2] = {nullptr, nullptr};  // signal, indexed by candidate position
  const float* y[2] = {nullptr, nullptr};  // reference
  int channels = 1;
  int w = 0;       // compared length
  double ey = 0;   // reference energy summed over channels
};

// Normalized cross-correlation of every candidate p in [lo, hi] (step 1) against the reference. The window energy
// slides, so the cost per candidate is one dot product per channel. Channel sums are averaged (power-of-two scale
// for stereo) so identical channels give exactly the mono score.
Candidate searchRange(const SearchSet& s, std::int64_t lo, std::int64_t hi, std::int64_t center) {
  Candidate best;
  const double invC = 1.0 / s.channels;
  double e[2] = {0, 0};
  for (int c = 0; c < s.channels; ++c) e[c] = dotf(s.x[c] + lo, s.x[c] + lo, s.w);
  for (std::int64_t p = lo; p <= hi; ++p) {
    double num = 0, ex = 0;
    for (int c = 0; c < s.channels; ++c) {
      num += dotf(s.x[c] + p, s.y[c], s.w);
      ex += e[c] > 0 ? e[c] : 0.0;
    }
    const double score = (num * invC) / std::sqrt((ex * invC) * (s.ey * invC) + 1e-12);
    if (better(score, p, center, best)) {
      best.p = p;
      best.score = score;
      best.set = true;
    }
    if (p < hi)
      for (int c = 0; c < s.channels; ++c) {
        const double add = s.x[c][p + s.w], sub = s.x[c][p];
        e[c] += add * add - sub * sub;
      }
  }
  return best;
}

struct Ctx {
  int channels = 1;
  std::int64_t sr = 48000;
  std::int64_t N = 0, Hs = 0, D = 0;
  int dec = 1;
  std::vector<double> hann;
};

Ctx makeCtx(int channels, std::int32_t sampleRate, Quality q) {
  const QualityParams qp = qualityParams(q);
  Ctx c;
  c.channels = channels;
  c.sr = sampleRate;
  c.N = 2 * std::llround(qp.windowMs * sampleRate / 2000.0);
  c.Hs = c.N / 2;
  c.D = std::llround(qp.toleranceMs * sampleRate / 1000.0);
  c.dec = std::max(1, qp.searchDecimation);
  c.hann.resize(static_cast<std::size_t>(c.N));
  for (std::int64_t n = 0; n < c.N; ++n) c.hann[static_cast<std::size_t>(n)] = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(n) / static_cast<double>(c.N)));
  return c;
}

// Short regions (one drum hit): keep the attack untouched, cut with a short fade or pad with silence.
void hitRegion(const Ctx& c, const float* const* x, std::int64_t Lin, float* const* y, std::int64_t Lout) {
  const std::int64_t n = std::min(Lin, Lout);
  for (int ch = 0; ch < c.channels; ++ch) std::memcpy(y[ch], x[ch], static_cast<std::size_t>(n) * sizeof(float));
  if (Lout < Lin) {
    const std::int64_t f = std::min<std::int64_t>(std::llround(0.005 * static_cast<double>(c.sr)), Lout / 2);
    for (int ch = 0; ch < c.channels; ++ch)
      for (std::int64_t i = 0; i < f; ++i) {
        const double g = 0.5 * (1.0 + std::cos(kPi * static_cast<double>(i + 1) / static_cast<double>(f)));
        float& s = y[ch][Lout - f + i];
        s = static_cast<float>(static_cast<double>(s) * g);
      }
  }
}

// Returns false when cancelled.
bool wsolaRegion(const Ctx& c, const float* const* x, std::int64_t Lin, float* const* y, std::int64_t Lout, Reporter& rep) {
  const std::int64_t N = c.N, Hs = c.Hs;
  const std::int64_t K = (Lout - N + Hs - 1) / Hs + 1;
  const auto tOf = [&](std::int64_t k) { return std::min(k * Hs, Lout - N); };

  // Coarse-search copies (box filter), computed once per region.
  std::vector<float> xd[2];
  const float* xdp[2] = {nullptr, nullptr};
  std::int64_t nd = 0;
  if (c.dec > 1) {
    nd = Lin / c.dec;
    for (int ch = 0; ch < c.channels; ++ch) {
      xd[ch].resize(static_cast<std::size_t>(nd));
      for (std::int64_t i = 0; i < nd; ++i) {
        double s = 0;
        for (int j = 0; j < c.dec; ++j) s += x[ch][i * c.dec + j];
        xd[ch][static_cast<std::size_t>(i)] = static_cast<float>(s / c.dec);
      }
      xdp[ch] = xd[ch].data();
    }
  }

  std::vector<double> acc[2], wsum(static_cast<std::size_t>(N), 0.0);
  for (int ch = 0; ch < c.channels; ++ch) acc[ch].assign(static_cast<std::size_t>(N), 0.0);

  std::int64_t prevP = 0, written = 0;
  for (std::int64_t k = 0; k < K; ++k) {
    const std::int64_t t = tOf(k);
    std::int64_t p = 0;
    if (k == K - 1) {
      p = Lin - N;
    } else if (k > 0) {
      const double q = static_cast<double>(t) * static_cast<double>(Lin - N) / static_cast<double>(Lout - N);
      const std::int64_t qc = std::llround(q);
      const std::int64_t pLo = std::clamp<std::int64_t>(qc - c.D, 0, Lin - N);
      const std::int64_t pHi = std::clamp<std::int64_t>(qc + c.D, 0, Lin - N);
      const std::int64_t ref = prevP + Hs;
      bool done = false;
      if (c.dec > 1) {
        const std::int64_t dLo = (pLo + c.dec - 1) / c.dec, dHi = pHi / c.dec;
        if (dLo <= dHi) {
          SearchSet s;
          s.channels = c.channels;
          s.w = static_cast<int>(Hs / c.dec);
          for (int ch = 0; ch < c.channels; ++ch) {
            s.x[ch] = xdp[ch];
            s.y[ch] = xdp[ch] + ref / c.dec;
            s.ey += dotf(s.y[ch], s.y[ch], s.w);
          }
          const Candidate coarse = searchRange(s, dLo, dHi, (qc + c.dec / 2) / c.dec);
          const std::int64_t pc = coarse.p * c.dec;
          p = 0;
          SearchSet f;
          f.channels = c.channels;
          f.w = static_cast<int>(Hs);
          for (int ch = 0; ch < c.channels; ++ch) {
            f.x[ch] = x[ch];
            f.y[ch] = x[ch] + ref;
            f.ey += dotf(f.y[ch], f.y[ch], f.w);
          }
          p = searchRange(f, std::max(pLo, pc - c.dec), std::min(pHi, pc + c.dec), qc).p;
          done = true;
        }
      }
      if (!done) {
        SearchSet f;
        f.channels = c.channels;
        f.w = static_cast<int>(Hs);
        for (int ch = 0; ch < c.channels; ++ch) {
          f.x[ch] = x[ch];
          f.y[ch] = x[ch] + ref;
          f.ey += dotf(f.y[ch], f.y[ch], f.w);
        }
        p = searchRange(f, pLo, pHi, qc).p;
      }
    }
    prevP = p;

    // Overlap-add: the first frame has a flat first half and the last a flat second half, so the region's first and
    // last output samples equal the source samples exactly.
    for (std::int64_t n = 0; n < N; ++n) {
      double w = c.hann[static_cast<std::size_t>(n)];
      if (k == 0 && n < Hs) w = 1.0;
      if (k == K - 1 && n >= Hs) w = 1.0;
      for (int ch = 0; ch < c.channels; ++ch) acc[ch][static_cast<std::size_t>(n)] += w * x[ch][p + n];
      wsum[static_cast<std::size_t>(n)] += w;
    }

    // Everything before the next frame's start is final.
    const std::int64_t next = k + 1 < K ? tOf(k + 1) : Lout;
    const std::int64_t cnt = next - t;  // buffer index 0 corresponds to output position t
    for (std::int64_t j = 0; j < cnt; ++j) {
      const double ws = wsum[static_cast<std::size_t>(j)];
      for (int ch = 0; ch < c.channels; ++ch)
        y[ch][t + j] = ws > 1e-6 ? static_cast<float>(acc[ch][static_cast<std::size_t>(j)] / ws) : 0.f;
    }
    for (int ch = 0; ch < c.channels; ++ch) {
      std::move(acc[ch].begin() + cnt, acc[ch].end(), acc[ch].begin());
      std::fill(acc[ch].end() - cnt, acc[ch].end(), 0.0);
    }
    std::move(wsum.begin() + cnt, wsum.end(), wsum.begin());
    std::fill(wsum.end() - cnt, wsum.end(), 0.0);

    rep.done += static_cast<double>(next - written);
    written = next;
    if (!rep.report(false)) return false;
  }
  return true;
}

bool allFinite(const float* p, std::int64_t n) {
  for (std::int64_t i = 0; i < n; ++i)
    if (!std::isfinite(p[i])) return false;
  return true;
}

}  // namespace

QualityParams qualityParams(Quality q) {
  switch (q) {
    case Quality::Draft: return {20.0, 5.0, 8};
    case Quality::High: return {60.0, 15.0, 1};
    case Quality::Standard: break;
  }
  return {40.0, 10.0, 4};
}

std::int64_t mapPosition(std::int64_t pos, std::int64_t inFrames, std::int64_t outFrames) {
  return scaled(std::clamp<std::int64_t>(pos, 0, std::max<std::int64_t>(inFrames, 0)), inFrames, outFrames);
}

std::int64_t mapLength(std::int64_t length, std::int64_t inFrames, std::int64_t outFrames) {
  return scaled(std::max<std::int64_t>(length, 0), inFrames, outFrames);
}

double lengthRatio(double sourceBpm, double targetBpm) { return sourceBpm / targetBpm; }

Status checkRatio(double ratio) {
  if (!std::isfinite(ratio) || ratio < kMinLengthRatio - 1e-9 || ratio > kMaxLengthRatio + 1e-9)
    return makeError(ErrorCode::OutOfRange, "Out of range: the tempo change must stay between 0.5x and 2x.");
  return {};
}

Result<Plan> makePlan(std::int64_t inFrames, std::int64_t outFrames, const std::vector<std::int64_t>& anchorsIn) {
  if (inFrames <= 0 || outFrames <= 0 || inFrames > kMaxPlanFrames || outFrames > kMaxPlanFrames)
    return makeError(ErrorCode::InvalidArgument, "the frame counts must be between 1 and 2,000,000,000", kMaxPlanFrames);
  const Status r = checkRatio(static_cast<double>(outFrames) / static_cast<double>(inFrames));
  if (!r.ok()) return r.error();
  std::vector<std::int64_t> anchors;
  for (std::int64_t a : anchorsIn)
    if (a > 0 && a < inFrames) anchors.push_back(a);
  std::sort(anchors.begin(), anchors.end());
  anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
  Plan plan;
  plan.inFrames = inFrames;
  plan.outFrames = outFrames;
  plan.inBounds.push_back(0);
  plan.outBounds.push_back(0);
  for (std::int64_t a : anchors) {
    const std::int64_t m = mapPosition(a, inFrames, outFrames);
    if (m <= plan.outBounds.back() || m >= outFrames) continue;
    plan.inBounds.push_back(a);
    plan.outBounds.push_back(m);
  }
  plan.inBounds.push_back(inFrames);
  plan.outBounds.push_back(outFrames);
  return plan;
}

Result<std::vector<float>> stretch(const Input& in, const Plan& plan, Quality q, const Progress& progress) {
  if (in.numChannels < 1 || in.numChannels > 2 || !in.channels) return makeError(ErrorCode::InvalidArgument, "the input must have 1 or 2 channels");
  for (int c = 0; c < in.numChannels; ++c)
    if (!in.channels[c]) return makeError(ErrorCode::InvalidArgument, "a channel pointer is null");
  if (in.sampleRate < kMinSampleRate || in.sampleRate > kMaxSampleRate) return makeError(ErrorCode::OutOfRange, "unsupported sample rate");
  if (in.frames <= 0 || in.frames != plan.inFrames) return makeError(ErrorCode::InvalidArgument, "the plan does not match the input length");
  if (plan.outFrames <= 0 || plan.inFrames > kMaxPlanFrames || plan.outFrames > kMaxPlanFrames)
    return makeError(ErrorCode::InvalidArgument, "the plan lengths are out of range");
  {
    const Status r = checkRatio(static_cast<double>(plan.outFrames) / static_cast<double>(plan.inFrames));
    if (!r.ok()) return r.error();
  }
  const std::size_t nb = plan.inBounds.size();
  if (nb < 2 || plan.outBounds.size() != nb || plan.inBounds.front() != 0 || plan.outBounds.front() != 0 || plan.inBounds.back() != plan.inFrames ||
      plan.outBounds.back() != plan.outFrames)
    return makeError(ErrorCode::InvalidArgument, "the plan bounds are malformed");
  for (std::size_t i = 1; i < nb; ++i)
    if (plan.inBounds[i] <= plan.inBounds[i - 1] || plan.outBounds[i] <= plan.outBounds[i - 1])
      return makeError(ErrorCode::InvalidArgument, "the plan bounds must be strictly increasing");

  try {
    const Ctx ctx = makeCtx(in.numChannels, in.sampleRate, q);
    std::vector<float> out(static_cast<std::size_t>(in.numChannels) * static_cast<std::size_t>(plan.outFrames), 0.f);
    Reporter rep;
    rep.fn = &progress;
    rep.total = static_cast<double>(plan.outFrames);
    if (!rep.report(true)) return makeError(ErrorCode::Cancelled, "Stretching cancelled; nothing was changed.");

    for (std::size_t i = 0; i + 1 < nb; ++i) {
      const std::int64_t a = plan.inBounds[i], Lin = plan.inBounds[i + 1] - a;
      const std::int64_t b = plan.outBounds[i], Lout = plan.outBounds[i + 1] - b;
      const float* x[2] = {nullptr, nullptr};
      float* y[2] = {nullptr, nullptr};
      std::vector<float> clean[2];  // only filled when the region holds non-finite samples
      for (int c = 0; c < in.numChannels; ++c) {
        x[c] = in.channels[c] + a;
        y[c] = out.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(plan.outFrames) + b;
        if (!allFinite(x[c], Lin)) {
          clean[c].assign(x[c], x[c] + Lin);
          for (float& s : clean[c])
            if (!std::isfinite(s)) s = 0.f;
          x[c] = clean[c].data();
        }
      }
      if (Lin < 3 * ctx.N || Lout < 3 * ctx.N) {
        hitRegion(ctx, x, Lin, y, Lout);
        rep.done += static_cast<double>(Lout);
        if (!rep.report(false)) return makeError(ErrorCode::Cancelled, "Stretching cancelled; nothing was changed.");
      } else if (!wsolaRegion(ctx, x, Lin, y, Lout, rep)) {
        return makeError(ErrorCode::Cancelled, "Stretching cancelled; nothing was changed.");
      }
    }
    rep.done = rep.total;
    rep.report(true);
    return out;
  } catch (const std::bad_alloc&) {
    return makeError(ErrorCode::LimitExceeded, "Not enough memory to stretch this source.");
  }
}

}  // namespace chopfractal::stretch
