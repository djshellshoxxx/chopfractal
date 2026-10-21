#include <chopfractal/audio_renderer/limiter.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace chopfractal::render {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kTaps = 32;           // taps per interpolator phase
constexpr int kHalf = kTaps / 2;
constexpr double kBeta = 2.5;
constexpr int kDetectorDelay = kHalf;  // the interpolated interval is centred 16 frames behind the newest input
constexpr int kFadeFrames = 256;
constexpr double kGuardDb = 0.1;  // 8x oversampling can still miss a fraction of a dB between grid points; aim this far under the ceiling

double besselI0(double x) {
  double sum = 1.0, term = 1.0;
  for (int k = 1; k < 40; ++k) {
    term *= (x / (2.0 * k)) * (x / (2.0 * k));
    sum += term;
  }
  return sum;
}

float sanitize(float v) { return std::isfinite(v) ? v : 0.f; }

}  // namespace

Limiter::Limiter() { prepare(48000.0, 2048); }

int Limiter::latencyFor(double sampleRate) {
  const double sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
  return std::max(8, static_cast<int>(std::lround(0.0015 * sr))) + kDetectorDelay;
}

void Limiter::prepare(double sampleRate, int) {
  sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
  lookahead_ = std::max(8, static_cast<int>(std::lround(0.0015 * sampleRate_)));
  latency_ = lookahead_ + kDetectorDelay;
  oversample_ = sampleRate_ >= 200000.0 ? 1 : (sampleRate_ >= 100000.0 ? 4 : 8);
  // Kaiser-windowed sinc interpolator: phase p reads x[c-15 .. c+16] and returns the value p/8 of the way from x[c] to x[c+1].
  for (int p = 0; p < 8; ++p) {
    const double d = p / 8.0;
    double sum = 0.0;
    double h[kTaps];
    for (int t = 0; t < kTaps; ++t) {
      const double u = (t - (kHalf - 1)) - d;
      const double sinc = std::fabs(u) < 1e-12 ? 1.0 : std::sin(kPi * u) / (kPi * u);
      const double r = u / kHalf;
      const double win = std::fabs(r) >= 1.0 ? 0.0 : besselI0(kBeta * std::sqrt(1.0 - r * r)) / besselI0(kBeta);
      h[t] = sinc * win;
      sum += h[t];
    }
    for (int t = 0; t < kTaps; ++t) taps_[p][t] = static_cast<float>(h[t] / sum);  // unity gain at DC
  }
  releaseCoef_ = 1.0 - std::exp(-1.0 / (0.120 * sampleRate_));
  for (int c = 0; c < 2; ++c) {
    hist_[c].assign(kTaps, 0.f);
    dly_[c].assign(static_cast<std::size_t>(latency_ + 1), 0.f);
  }
  tpRing_.assign(static_cast<std::size_t>(latency_ + 1), 0.f);
  dqVal_.assign(static_cast<std::size_t>(lookahead_ + 2), 1.0);
  dqIdx_.assign(static_cast<std::size_t>(lookahead_ + 2), 0);
  boxRing_.assign(static_cast<std::size_t>(lookahead_ + 1), 1.0);
  reset();
}

void Limiter::reset() noexcept {
  for (int c = 0; c < 2; ++c) {
    std::fill(hist_[c].begin(), hist_[c].end(), 0.f);
    std::fill(dly_[c].begin(), dly_[c].end(), 0.f);
  }
  std::fill(tpRing_.begin(), tpRing_.end(), 0.f);
  std::fill(boxRing_.begin(), boxRing_.end(), 1.0);
  boxSum_ = static_cast<double>(lookahead_ + 1);
  boxPos_ = 0;
  dqHead_ = 0;
  dqCount_ = 0;
  gRel_ = 1.0;
  prevGt_ = 1.0;
  dlyPos_ = 0;
  n_ = 0;
  warm_ = 0;
  mix_ = 0.f;
  active_ = false;
}

void Limiter::prime() noexcept {
  reset();
  active_ = true;
  mix_ = 1.f;
}

void Limiter::process(const LimiterParams& params, float* l, float* r, int frames) noexcept {
  if (frames <= 0 || !l || !r) return;
  const bool wantOn = params.enabled;
  if (!active_) {
    if (!wantOn) return;  // bit-exact bypass
    reset();
    active_ = true;
    warm_ = latency_;  // the limited path is silent until the delay line has filled; stay on the bypass path until then
  }
  const double ceilDb = std::min(0.0, std::max(-6.0, static_cast<double>(std::isfinite(params.ceilingDb) ? params.ceilingDb : -1.f))) - kGuardDb;
  const double kneeDb = 6.0;
  const double thresholdDb = ceilDb - kneeDb;
  const double thresholdLin = std::pow(10.0, thresholdDb / 20.0);
  const float clampLin = static_cast<float>(std::pow(10.0, (ceilDb + kGuardDb) / 20.0));
  const float step = 1.f / static_cast<float>(kFadeFrames);
  const int D = lookahead_;
  const int ringLen = latency_ + 1;
  const int boxLen = D + 1;
  const int dqLen = D + 2;
  double grMin = 1.0;
  float peakMax = 0.f;
  std::uint32_t backstops = 0;

  for (int i = 0; i < frames; ++i) {
    const float in[2] = {sanitize(l[i]), sanitize(r[i])};
    // 1. history and delay line
    for (int c = 0; c < 2; ++c) {
      float* h = hist_[c].data();
      std::memmove(h, h + 1, (kTaps - 1) * sizeof(float));
      h[kTaps - 1] = in[c];
      dly_[c][static_cast<std::size_t>(dlyPos_)] = in[c];
    }
    // 2. true-peak estimate of the interval [x[n-16], x[n-15]) (history taps 0..31 are x[n-31..n]); sample peaks included
    float tp = 0.f;
    for (int c = 0; c < 2; ++c) {
      const float* h = hist_[c].data();
      tp = std::max(tp, std::max(std::fabs(h[kHalf - 1]), std::fabs(h[kHalf])));
      if (oversample_ == 1) continue;
      for (int p = 1; p < 8; p += 8 / oversample_) {
        float y = 0.f;
        for (int t = 0; t < kTaps; ++t) y += taps_[p][t] * h[t];
        tp = std::max(tp, std::fabs(y));
      }
    }
    tpRing_[static_cast<std::size_t>(dlyPos_)] = tp;
    // 3. gain needed by that interval (soft knee towards the ceiling)
    double gt = 1.0;
    if (tp > thresholdLin) {
      const double pDb = 20.0 * std::log10(static_cast<double>(tp));
      const double outDb = thresholdDb + kneeDb * std::tanh((pDb - thresholdDb) / kneeDb);
      gt = std::pow(10.0, (outDb - pDb) / 20.0);
    }
    const double req = std::min(gt, prevGt_);  // audio sample n-16 sits in two intervals
    prevGt_ = gt;
    // 4. sliding minimum over the last D + 1 requirements
    while (dqCount_ > 0) {
      const int back = (dqHead_ + dqCount_ - 1) % dqLen;
      if (dqVal_[static_cast<std::size_t>(back)] >= req) --dqCount_;
      else break;
    }
    dqVal_[static_cast<std::size_t>((dqHead_ + dqCount_) % dqLen)] = req;
    dqIdx_[static_cast<std::size_t>((dqHead_ + dqCount_) % dqLen)] = n_;
    ++dqCount_;
    while (dqIdx_[static_cast<std::size_t>(dqHead_)] <= n_ - boxLen) {
      dqHead_ = (dqHead_ + 1) % dqLen;
      --dqCount_;
    }
    const double m = dqVal_[static_cast<std::size_t>(dqHead_)];
    // 5. release (instant attack into the window), then the boxcar that makes the attack a ramp
    gRel_ = m < gRel_ ? m : gRel_ + (m - gRel_) * releaseCoef_;
    boxSum_ += gRel_ - boxRing_[static_cast<std::size_t>(boxPos_)];
    boxRing_[static_cast<std::size_t>(boxPos_)] = gRel_;
    boxPos_ = (boxPos_ + 1) % boxLen;
    const double g = std::min(1.0, boxSum_ / static_cast<double>(boxLen));
    // 6. the delayed audio times the gain, then the sample-peak backstop
    const int readPos = (dlyPos_ + 1) % ringLen;  // oldest entry: written L frames ago
    float o[2];
    for (int c = 0; c < 2; ++c) {
      float y = static_cast<float>(static_cast<double>(dly_[c][static_cast<std::size_t>(readPos)]) * g);
      if (y > clampLin) { y = clampLin; ++backstops; }
      if (y < -clampLin) { y = -clampLin; ++backstops; }
      o[c] = y;
    }
    grMin = std::min(grMin, g);
    peakMax = std::max(peakMax, static_cast<float>(static_cast<double>(tpRing_[static_cast<std::size_t>(readPos)]) * g));
    dlyPos_ = (dlyPos_ + 1) % ringLen;
    ++n_;
    // 7. blend with the bypass path while enabling or disabling (the limited path lags the input by L frames)
    if (warm_ > 0) {
      --warm_;
    } else {
      mix_ = wantOn ? std::min(1.f, mix_ + step) : std::max(0.f, mix_ - step);
    }
    if (mix_ >= 1.f) {
      l[i] = o[0];
      r[i] = o[1];
    } else {
      l[i] = in[0] + mix_ * (o[0] - in[0]);
      r[i] = in[1] + mix_ * (o[1] - in[1]);
    }
    if (!wantOn && mix_ <= 0.f && warm_ == 0) {  // faded out: the rest of the block is untouched input
      active_ = false;
      break;
    }
  }
  const float grDb = static_cast<float>(20.0 * std::log10(std::max(1e-6, grMin)));
  float cur = telemetry_.grDb.load(std::memory_order_relaxed);
  if (grDb < cur) telemetry_.grDb.store(grDb, std::memory_order_relaxed);
  if (peakMax > 0.f) {
    const float pdb = 20.f * std::log10(peakMax);
    if (pdb > telemetry_.peakDb.load(std::memory_order_relaxed)) telemetry_.peakDb.store(pdb, std::memory_order_relaxed);
  }
  if (backstops) telemetry_.backstops.fetch_add(backstops, std::memory_order_relaxed);
}

}  // namespace chopfractal::render
