#include <chopfractal/time_stretch/stretch.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace chopfractal::stretch {
namespace {

constexpr double kPi = 3.14159265358979323846;

Error err(ErrorCode c, std::string m, std::int64_t hint = 0) { return makeError(c, std::move(m), hint); }

struct Region {
  std::int64_t inStart, inEnd, outStart, outEnd;
};

// Reads one channel of a region with zeros outside the source.
inline float sampleAt(const float* x, std::int64_t frames, std::int64_t i) {
  if (i < 0 || i >= frames) return 0.f;
  const float v = x[i];
  return std::isfinite(v) ? v : 0.f;
}

class Worker {
 public:
  Worker(const Input& in, Quality q, const Progress& progress, std::int64_t totalOut)
      : in_(in), progress_(progress), totalOut_(std::max<std::int64_t>(1, totalOut)), last_(std::chrono::steady_clock::now()) {
    const QualityParams qp = paramsFor(q);
    N_ = 2 * static_cast<int>(std::llround(qp.windowMs * in.sampleRate / 2000.0));
    N_ = std::max(N_, 16);
    Hs_ = N_ / 2;
    D_ = std::max(1, static_cast<int>(std::llround(qp.toleranceMs * in.sampleRate / 1000.0)));
    dec_ = qp.searchDecimation;
    win_.resize(static_cast<std::size_t>(N_));
    for (int n = 0; n < N_; ++n) win_[static_cast<std::size_t>(n)] = 0.5 * (1.0 - std::cos(2.0 * kPi * n / N_));
  }

  bool cancelled() const { return cancelled_; }

  void region(const Region& r, std::vector<float>& out) {
    const std::int64_t Lin = r.inEnd - r.inStart, Lout = r.outEnd - r.outStart;
    if (Lin < 3 * N_ || Lout < 3 * N_) {
      hit(r, Lin, Lout, out);
    } else {
      wsola(r, Lin, Lout, out);
    }
    done_ += Lout;
    report();
  }

 private:
  void report() {
    if (!progress_ || cancelled_) return;
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double, std::milli>(now - last_).count() < 25.0 && done_ < totalOut_) return;
    last_ = now;
    if (!progress_(std::min(1.0, static_cast<double>(done_) / static_cast<double>(totalOut_)))) cancelled_ = true;
  }

  void hit(const Region& r, std::int64_t Lin, std::int64_t Lout, std::vector<float>& out) {
    const std::int64_t n = std::min(Lin, Lout);
    for (int c = 0; c < in_.numChannels; ++c) {
      float* dst = out.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(totalFrames_) + r.outStart;
      for (std::int64_t i = 0; i < n; ++i) dst[i] = sampleAt(in_.channels[c], in_.frames, r.inStart + i);
      if (Lout < Lin) {  // the cut gets a short fade; the attack is untouched
        const std::int64_t fade = std::min<std::int64_t>(std::llround(0.005 * in_.sampleRate), Lout / 2);
        for (std::int64_t i = 0; i < fade; ++i) dst[Lout - fade + i] *= static_cast<float>(0.5 * (1.0 + std::cos(kPi * static_cast<double>(i + 1) / static_cast<double>(fade))));
      }
    }
  }

  double corr(std::int64_t p, std::int64_t a, const std::vector<std::vector<float>>& ref) const {
    double xy = 0.0, xx = 0.0, yy = 0.0;
    for (int c = 0; c < in_.numChannels; ++c)
      for (int n = 0; n < Hs_; ++n) {
        const double x = sampleAt(in_.channels[c], in_.frames, a + p + n);
        const double y = ref[static_cast<std::size_t>(c)][static_cast<std::size_t>(n)];
        xy += x * y;
        xx += x * x;
        yy += y * y;
      }
    return xy / std::sqrt(xx * yy + 1e-12);
  }

  void wsola(const Region& r, std::int64_t Lin, std::int64_t Lout, std::vector<float>& out) {
    const std::int64_t a = r.inStart;
    const std::int64_t K = (Lout - N_ + Hs_ - 1) / Hs_ + 1;
    const std::int64_t maxP = Lin - N_;
    std::vector<std::vector<double>> acc(static_cast<std::size_t>(in_.numChannels), std::vector<double>(static_cast<std::size_t>(Lout), 0.0));
    std::vector<double> wsum(static_cast<std::size_t>(Lout), 0.0);
    std::vector<std::vector<float>> ref(static_cast<std::size_t>(in_.numChannels), std::vector<float>(static_cast<std::size_t>(Hs_)));
    // box-filtered, decimated copy of the region for the coarse search
    std::vector<std::vector<float>> coarse;
    if (dec_ > 1) {
      coarse.assign(static_cast<std::size_t>(in_.numChannels), std::vector<float>(static_cast<std::size_t>(Lin / dec_ + 1), 0.f));
      for (int c = 0; c < in_.numChannels; ++c)
        for (std::int64_t i = 0; i < Lin / dec_; ++i) {
          double s = 0.0;
          for (int k = 0; k < dec_; ++k) s += sampleAt(in_.channels[c], in_.frames, a + i * dec_ + k);
          coarse[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = static_cast<float>(s / dec_);
        }
    }
    std::int64_t prevP = 0;
    for (std::int64_t k = 0; k < K; ++k) {
      const std::int64_t t = std::min<std::int64_t>(k * Hs_, Lout - N_);
      std::int64_t p;
      if (k == 0) {
        p = 0;
      } else if (k == K - 1) {
        p = maxP;
      } else {
        const double q = static_cast<double>(t) * static_cast<double>(maxP) / static_cast<double>(Lout - N_);
        const std::int64_t center = std::llround(q);
        for (int c = 0; c < in_.numChannels; ++c)
          for (int n = 0; n < Hs_; ++n) ref[static_cast<std::size_t>(c)][static_cast<std::size_t>(n)] = sampleAt(in_.channels[c], in_.frames, a + prevP + Hs_ + n);
        double best = -2.0;
        std::int64_t bestP = std::clamp<std::int64_t>(center, 0, maxP);
        auto consider = [&](std::int64_t cand, std::int64_t d, double score) {
          if (score > best + 1e-12 || (std::fabs(score - best) <= 1e-12 && (std::llabs(d) < std::llabs(bestP - center) || (std::llabs(d) == std::llabs(bestP - center) && d < bestP - center)))) {
            best = score;
            bestP = cand;
          }
        };
        if (dec_ > 1) {
          // coarse: step by dec on the decimated copy, then refine at full resolution around the winner
          const std::int64_t hsd = Hs_ / dec_;
          double cbest = -2.0;
          std::int64_t cbestD = 0;
          for (std::int64_t d = -D_; d <= D_; d += dec_) {
            const std::int64_t cand = std::clamp<std::int64_t>(center + d, 0, maxP);
            const std::int64_t pd = cand / dec_;
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int c = 0; c < in_.numChannels; ++c)
              for (std::int64_t n = 0; n < hsd; ++n) {
                const std::int64_t xi = pd + n;
                const double x = xi < static_cast<std::int64_t>(coarse[static_cast<std::size_t>(c)].size()) ? coarse[static_cast<std::size_t>(c)][static_cast<std::size_t>(xi)] : 0.0;
                double y = 0.0;
                for (int j = 0; j < dec_; ++j) y += ref[static_cast<std::size_t>(c)][static_cast<std::size_t>(n * dec_ + j)];
                y /= dec_;
                xy += x * y;
                xx += x * x;
                yy += y * y;
              }
            const double s = xy / std::sqrt(xx * yy + 1e-12);
            if (s > cbest + 1e-12 || (std::fabs(s - cbest) <= 1e-12 && std::llabs(d) < std::llabs(cbestD))) {
              cbest = s;
              cbestD = d;
            }
          }
          for (std::int64_t d = cbestD - dec_; d <= cbestD + dec_; ++d) {
            if (d < -D_ || d > D_) continue;
            const std::int64_t cand = std::clamp<std::int64_t>(center + d, 0, maxP);
            consider(cand, d, corr(cand, a, ref));
          }
        } else {
          for (std::int64_t d = -D_; d <= D_; ++d) {
            const std::int64_t cand = std::clamp<std::int64_t>(center + d, 0, maxP);
            consider(cand, d, corr(cand, a, ref));
          }
        }
        p = bestP;
      }
      for (int n = 0; n < N_; ++n) {
        double w = win_[static_cast<std::size_t>(n)];
        if (k == 0 && n < Hs_) w = 1.0;                // flat head: output sample 0 is source sample a
        if (k == K - 1 && n >= Hs_) w = 1.0;           // flat tail: the last output sample is the region's last
        const std::int64_t o = t + n;
        wsum[static_cast<std::size_t>(o)] += w;
        for (int c = 0; c < in_.numChannels; ++c) acc[static_cast<std::size_t>(c)][static_cast<std::size_t>(o)] += w * sampleAt(in_.channels[c], in_.frames, a + p + n);
      }
      prevP = p;
      if ((k & 15) == 0) {
        if (progress_ && !cancelled_) {
          const auto now = std::chrono::steady_clock::now();
          if (std::chrono::duration<double, std::milli>(now - last_).count() >= 25.0) {
            last_ = now;
            const double frac = (static_cast<double>(done_) + static_cast<double>(Lout) * static_cast<double>(k) / static_cast<double>(K)) / static_cast<double>(totalOut_);
            if (!progress_(std::min(1.0, frac))) {
              cancelled_ = true;
              return;
            }
          }
        }
      }
    }
    for (int c = 0; c < in_.numChannels; ++c) {
      float* dst = out.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(totalFrames_) + r.outStart;
      for (std::int64_t i = 0; i < Lout; ++i) {
        const double w = wsum[static_cast<std::size_t>(i)];
        const double v = w > 1e-6 ? acc[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] / w : 0.0;
        dst[i] = std::isfinite(v) ? static_cast<float>(v) : 0.f;
      }
    }
  }

 public:
  std::int64_t totalFrames_ = 0;

 private:
  const Input& in_;
  const Progress& progress_;
  std::int64_t totalOut_;
  std::int64_t done_ = 0;
  bool cancelled_ = false;
  std::chrono::steady_clock::time_point last_;
  int N_ = 0, Hs_ = 0, D_ = 0, dec_ = 1;
  std::vector<double> win_;
};

}  // namespace

QualityParams paramsFor(Quality q) {
  switch (q) {
    case Quality::Draft: return {20.0, 5.0, 8};
    case Quality::High: return {60.0, 15.0, 1};
    case Quality::Standard: break;
  }
  return {40.0, 10.0, 4};
}

std::int64_t mapPosition(std::int64_t pos, std::int64_t inFrames, std::int64_t outFrames) {
  if (inFrames <= 0) return 0;
  return (2 * pos * outFrames + inFrames) / (2 * inFrames);  // operands are non-negative, so integer division floors
}

double lengthRatio(double sourceBpm, double targetBpm) { return sourceBpm / targetBpm; }

Status checkRatio(double ratio) {
  if (!(ratio >= kMinLengthRatio - 1e-9 && ratio <= kMaxLengthRatio + 1e-9))
    return err(ErrorCode::OutOfRange, "Out of range: the tempo change must stay between 0.5x and 2x.");
  return {};
}

Result<Plan> makePlan(std::int64_t inFrames, std::int64_t outFrames, const std::vector<std::int64_t>& anchorsIn) {
  if (inFrames <= 0 || outFrames <= 0) return err(ErrorCode::InvalidArgument, "the source is empty");
  std::vector<std::int64_t> anchors = anchorsIn;
  std::sort(anchors.begin(), anchors.end());
  Plan plan;
  plan.inFrames = inFrames;
  plan.outFrames = outFrames;
  plan.inBounds.push_back(0);
  plan.outBounds.push_back(0);
  for (std::int64_t a : anchors) {
    if (a <= 0 || a >= inFrames) continue;
    const std::int64_t m = mapPosition(a, inFrames, outFrames);
    if (a <= plan.inBounds.back() || m <= plan.outBounds.back() || m >= outFrames) continue;
    plan.inBounds.push_back(a);
    plan.outBounds.push_back(m);
  }
  plan.inBounds.push_back(inFrames);
  plan.outBounds.push_back(outFrames);
  return plan;
}

Result<std::vector<float>> stretch(const Input& in, const Plan& plan, Quality quality, const Progress& progress) {
  if (in.numChannels < 1 || in.numChannels > 2 || !in.channels || in.frames <= 0 || in.sampleRate < 8000) return err(ErrorCode::InvalidArgument, "unsupported audio");
  if (plan.inFrames != in.frames || plan.outFrames <= 0 || plan.inBounds.size() < 2 || plan.inBounds.size() != plan.outBounds.size() || plan.inBounds.front() != 0 ||
      plan.outBounds.front() != 0 || plan.inBounds.back() != plan.inFrames || plan.outBounds.back() != plan.outFrames)
    return err(ErrorCode::InvalidArgument, "the stretch plan does not match the audio");
  for (std::size_t i = 1; i < plan.inBounds.size(); ++i)
    if (plan.inBounds[i] <= plan.inBounds[i - 1] || plan.outBounds[i] <= plan.outBounds[i - 1]) return err(ErrorCode::InvalidArgument, "the stretch plan is not increasing");
  if (const Status s = checkRatio(static_cast<double>(plan.outFrames) / static_cast<double>(plan.inFrames)); !s.ok()) return s.error();
  std::vector<float> out;
  try {
    out.assign(static_cast<std::size_t>(in.numChannels) * static_cast<std::size_t>(plan.outFrames), 0.f);
  } catch (const std::bad_alloc&) {
    return err(ErrorCode::LimitExceeded, "Not enough memory to stretch this source.");
  }
  Worker w(in, quality, progress, plan.outFrames);
  w.totalFrames_ = plan.outFrames;
  for (std::size_t i = 0; i + 1 < plan.inBounds.size(); ++i) {
    w.region({plan.inBounds[i], plan.inBounds[i + 1], plan.outBounds[i], plan.outBounds[i + 1]}, out);
    if (w.cancelled()) return err(ErrorCode::Cancelled, "Stretching cancelled; nothing was changed.");
  }
  if (progress) progress(1.0);
  return out;
}

}  // namespace chopfractal::stretch
