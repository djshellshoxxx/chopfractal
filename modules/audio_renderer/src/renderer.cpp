#include <chopfractal/audio_renderer/renderer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace chopfractal::render {
namespace {

constexpr int kDefaultAttack = 32;
constexpr int kDefaultRelease = 64;
constexpr int kKillFade = 64;
constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 999.0;
constexpr float kRetriggerDecay = 0.8f;
constexpr std::uint32_t kPreviewQueue = 16;

struct Voice {
  bool active = false;
  bool fading = false;  // being stolen or released: ramping to silence
  int fadeLeft = 0;
  int fadeTotal = 1;
  const float* ch0 = nullptr;
  const float* ch1 = nullptr;
  double pos = 0.0;
  double step = 1.0;
  std::int64_t regionStart = 0;
  std::int64_t regionEnd = 0;
  std::int64_t age = 0;
  std::int64_t gateFrames = 0;
  int attack = 0;
  int release = 1;
  float gainL = 0.f;
  float gainR = 0.f;
  float lastLevel = 0.f;
  std::uint64_t serial = 0;
  // Event effects. Defaults are exact bypasses (rateScale stays 1.0, filter/crunch skipped).
  double rateScale = 1.0;
  double glideMul = 1.0;
  bool filterOn = false;
  bool highPass = false;
  float fa1 = 0.f, fa2 = 0.f, fa3 = 0.f, fk = 0.f;
  float ic1[2] = {0.f, 0.f};
  float ic2[2] = {0.f, 0.f};
  bool crunchOn = false;
  float crushLevels = 1.f;
  int holdFrames = 1;
  int holdCount = 0;
  float heldL = 0.f, heldR = 0.f;
};

struct Trigger {
  int frame;
  const FlatEvent* event;
};

bool finiteIn(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

}  // namespace

Result<std::shared_ptr<const Playback>> makePlayback(SourcePtr source, const FlatEventList& events, Ticks lengthTicks) {
  if (!source || source->frames <= 0 || (source->channels != 1 && source->channels != 2) || source->sampleRate < 8000 || source->sampleRate > 384000)
    return makeError(ErrorCode::InvalidArgument, "the source buffer is missing or has an unsupported format");
  if (source->samples.size() != static_cast<std::size_t>(source->channels) * static_cast<std::size_t>(source->frames))
    return makeError(ErrorCode::Corrupt, "the source buffer size does not match its channel and frame counts");
  if (lengthTicks <= 0) return makeError(ErrorCode::InvalidArgument, "the pattern length must be positive");
  if (events.size() > limits::kMaxEventsPerPattern) return makeError(ErrorCode::LimitExceeded, "too many events", static_cast<std::int64_t>(limits::kMaxEventsPerPattern));

  auto pb = std::make_shared<Playback>();
  pb->source = std::move(source);
  pb->lengthTicks = lengthTicks;
  for (const FlatEvent& e : events) {
    if (e.duration <= 0 || e.start < 0 || e.start >= lengthTicks) return makeError(ErrorCode::OutOfRange, "an event has invalid timing");
    if (e.region.empty() || e.region.start < 0 || e.region.end > pb->source->frames) return makeError(ErrorCode::OutOfRange, "an event reads outside the source");
    const EventFx& fx = e.fx;
    if (static_cast<std::uint8_t>(fx.filter) > 2 || !finiteIn(fx.cutoff, 0.f, 1.f) || !finiteIn(fx.resonance, 0.f, 1.f) ||
        !finiteIn(fx.crush, 0.f, 1.f) || !finiteIn(fx.glideSemitones, -limits::kMaxGlideSemitones, limits::kMaxGlideSemitones))
      return makeError(ErrorCode::OutOfRange, "an event has an out-of-range effect");
    const EventTransform& t = e.tx;
    if (!finiteIn(t.level, 0.f, limits::kMaxLevel) || !finiteIn(t.pan, -1.f, 1.f) || !finiteIn(t.pitchSemitones, -limits::kMaxPitchSemitones, limits::kMaxPitchSemitones) ||
        t.retrigger < 1 || t.retrigger > limits::kMaxRetrigger)
      return makeError(ErrorCode::OutOfRange, "an event has an out-of-range transform");
    // Retrigger: n evenly spaced sub-hits across the event, each decaying; expanded here so the audio
    // thread needs no cross-block retrigger state.
    const int n = t.retrigger;
    float level = t.level;
    for (int k = 0; k < n; ++k) {
      FlatEvent s = e;
      s.tx.retrigger = 1;
      s.start = e.start + e.duration * k / n;
      s.duration = e.duration * (k + 1) / n - e.duration * k / n;
      s.tx.level = level;
      level *= kRetriggerDecay;
      if (s.duration > 0) pb->events.push_back(s);
    }
  }
  if (pb->events.size() > kMaxPreparedEvents) return makeError(ErrorCode::LimitExceeded, "too many events after retrigger expansion", static_cast<std::int64_t>(kMaxPreparedEvents));
  std::stable_sort(pb->events.begin(), pb->events.end(), [](const FlatEvent& a, const FlatEvent& b) { return a.start < b.start; });
  return std::shared_ptr<const Playback>(std::move(pb));
}

Result<std::shared_ptr<const Playback>> makePassThroughPlayback(SourcePtr source) {
  auto base = makePlayback(std::move(source), {}, kTicksPerWhole);
  if (!base.ok()) return base.error();
  auto pb = std::make_shared<Playback>(*base.value());
  pb->passThrough = true;
  return std::shared_ptr<const Playback>(std::move(pb));
}

struct Renderer::Impl {
  Config cfg;
  bool prepared = false;
  Mailbox<Playback> mailbox;
  const Playback* current = nullptr;

  std::vector<Voice> voices;
  std::array<Voice, 2> previews;
  int previewIndex = 0;
  std::vector<float> mixL, mixR;
  std::vector<Trigger> triggers;

  double outGain = 1.0;
  double dry = 0.0;
  double smoothCoef = 0.01;

  bool wasPlaying = false;
  bool haveExpected = false;
  double expectedTicks = 0.0;
  double freeRunTicks = 0.0;
  std::uint64_t serialCounter = 0;
  std::uint64_t dropped = 0;

  std::array<PreviewRequest, kPreviewQueue> previewQueue;
  std::atomic<std::uint32_t> previewHead{0};
  std::atomic<std::uint32_t> previewTail{0};

  void prepare(const Config& c) {
    cfg = c;
    cfg.maxBlock = std::max(1, std::min(cfg.maxBlock, 16384));
    cfg.maxVoices = std::max(1, std::min(cfg.maxVoices, limits::kMaxVoices * 2));
    cfg.sampleRate = (std::isfinite(cfg.sampleRate) && cfg.sampleRate >= 8000.0) ? cfg.sampleRate : 48000.0;
    voices.assign(static_cast<std::size_t>(cfg.maxVoices) * 2, Voice{});  // second half holds stolen, fading voices
    previews = {};
    mixL.assign(static_cast<std::size_t>(cfg.maxBlock), 0.f);
    mixR.assign(static_cast<std::size_t>(cfg.maxBlock), 0.f);
    triggers.clear();
    triggers.reserve(kMaxPreparedEvents);
    smoothCoef = 1.0 - std::exp(-1.0 / (0.01 * cfg.sampleRate));
    outGain = 1.0;
    dry = 0.0;
    wasPlaying = false;
    haveExpected = false;
    previewHead.store(0);
    previewTail.store(0);
    prepared = true;
  }

  void hardKillAll() {
    for (Voice& v : voices) v.active = false;
    for (Voice& v : previews) v.active = false;
  }

  void releaseVoice(Voice& v) {
    if (!v.active || v.fading) return;
    v.fading = true;
    v.fadeTotal = kKillFade;
    v.fadeLeft = kKillFade;
  }

  void releaseAll() {
    for (Voice& v : voices) releaseVoice(v);
  }

  static void passthrough(const float* const* in, float* const* out, int nch, int frames) {
    for (int c = 0; c < nch; ++c) {
      const float* src = in ? in[c] : nullptr;
      float* dst = out[c];
      for (int i = 0; i < frames; ++i) {
        const float x = src ? src[i] : 0.f;
        dst[i] = std::isfinite(x) ? x : 0.f;
      }
    }
  }

  void startVoice(Voice& v, const SourceData& src, SampleRange region, const EventTransform& tx, std::int64_t gateFrames,
                  std::uint32_t fadeIn, std::uint32_t fadeOut, const EventFx& fx = EventFx{}) {
    const double ratio = static_cast<double>(src.sampleRate) / cfg.sampleRate;
    const double rate = ratio * std::exp2(static_cast<double>(tx.pitchSemitones) / 12.0);
    v = Voice{};
    v.active = true;
    v.serial = ++serialCounter;
    v.ch0 = src.channel(0);
    v.ch1 = src.channels > 1 ? src.channel(1) : v.ch0;
    v.regionStart = region.start;
    v.regionEnd = region.end;
    v.step = tx.reverse ? -rate : rate;
    v.pos = tx.reverse ? static_cast<double>(region.end - 1) : static_cast<double>(region.start);
    const std::int64_t gate = std::max<std::int64_t>(1, gateFrames);
    v.gateFrames = gate;
    std::int64_t a = fadeIn ? std::llround(static_cast<double>(fadeIn) / ratio) : kDefaultAttack;
    std::int64_t r = fadeOut ? std::llround(static_cast<double>(fadeOut) / ratio) : kDefaultRelease;
    a = std::min<std::int64_t>(a, gate / 2);
    r = std::min<std::int64_t>(r, gate / 2);
    v.attack = static_cast<int>(a);
    v.release = static_cast<int>(std::max<std::int64_t>(1, r));
    v.gainL = tx.level * (tx.pan > 0.f ? 1.f - tx.pan : 1.f);
    v.gainR = tx.level * (tx.pan < 0.f ? 1.f + tx.pan : 1.f);
    if (fx.glideSemitones != 0.f) v.glideMul = std::exp2((static_cast<double>(fx.glideSemitones) / 12.0) / static_cast<double>(gate));
    if (fx.crush > 0.f) {
      const double c = fx.crush;
      v.crunchOn = true;
      v.crushLevels = static_cast<float>(std::exp2(16.0 - 12.0 * c - 1.0));
      v.holdFrames = 1 + static_cast<int>(std::lround(15.0 * c * c));
    }
    if (fx.filter != FilterType::Off) {
      const double fc = std::min(20.0 * std::pow(1000.0, static_cast<double>(fx.cutoff)), 0.45 * cfg.sampleRate);
      const double q = 0.71 + (8.0 - 0.71) * static_cast<double>(fx.resonance);
      const double g = std::tan(3.14159265358979323846 * fc / cfg.sampleRate);
      const double k = 1.0 / q;
      const double a1 = 1.0 / (1.0 + g * (g + k));
      v.filterOn = true;
      v.highPass = fx.filter == FilterType::HighPass;
      v.fk = static_cast<float>(k);
      v.fa1 = static_cast<float>(a1);
      v.fa2 = static_cast<float>(g * a1);
      v.fa3 = static_cast<float>(g * g * a1);
    }
  }

  Voice* allocateVoice() {
    int playing = 0;
    for (const Voice& v : voices)
      if (v.active && !v.fading) ++playing;
    if (playing >= cfg.maxVoices) {
      // Steal the quietest sounding voice; voices still in their attack are protected (their level is not
      // yet meaningful) unless every voice is. Ties go to the oldest.
      Voice* victim = nullptr;
      for (int pass = 0; pass < 2 && !victim; ++pass)
        for (Voice& v : voices) {
          if (!v.active || v.fading) continue;
          if (pass == 0 && v.age < v.attack) continue;
          if (!victim || v.lastLevel < victim->lastLevel || (v.lastLevel == victim->lastLevel && v.serial < victim->serial)) victim = &v;
        }
      if (victim) releaseVoice(*victim);  // fades out instead of cutting
    }
    for (Voice& v : voices)
      if (!v.active) return &v;
    // Pool exhausted. Recycle the fading voice closest to silence; only if none is fading, the oldest sounding one.
    Voice* nearest = nullptr;
    for (Voice& v : voices)
      if (v.fading && (!nearest || v.fadeLeft < nearest->fadeLeft)) nearest = &v;
    if (!nearest)
      for (Voice& v : voices)
        if (!nearest || v.serial < nearest->serial) nearest = &v;
    return nearest;
  }

  void startFromEvent(const SourceData& src, const FlatEvent& e, double ticksPerFrame) {
    if (e.region.empty() || e.region.start < 0 || e.region.end > src.frames) return;
    Voice* v = allocateVoice();
    if (!v) return;
    const std::int64_t gate = static_cast<std::int64_t>(std::llround(static_cast<double>(e.duration) / ticksPerFrame));
    startVoice(*v, src, e.region, e.tx, gate, e.fadeInFrames, e.fadeOutFrames, e.fx);
  }

  void drainPreview(const Playback* pb, bool allow) {
    std::uint32_t tail = previewTail.load(std::memory_order_relaxed);
    const std::uint32_t head = previewHead.load(std::memory_order_acquire);
    while (tail != head) {
      const PreviewRequest req = previewQueue[tail % kPreviewQueue];
      ++tail;
      if (!allow || !pb || !pb->source) continue;
      const SourceData& src = *pb->source;
      if (req.region.empty() || req.region.start < 0 || req.region.end > src.frames) continue;
      if (!finiteIn(req.tx.level, 0.f, limits::kMaxLevel) || !finiteIn(req.tx.pan, -1.f, 1.f) ||
          !finiteIn(req.tx.pitchSemitones, -limits::kMaxPitchSemitones, limits::kMaxPitchSemitones))
        continue;
      Voice& old = previews[static_cast<std::size_t>(previewIndex)];
      releaseVoice(old);  // the previous preview fades out while the new one starts in the other slot
      previewIndex ^= 1;
      const double rate = (static_cast<double>(src.sampleRate) / cfg.sampleRate) * std::exp2(static_cast<double>(req.tx.pitchSemitones) / 12.0);
      const std::int64_t gate = static_cast<std::int64_t>(std::ceil(static_cast<double>(req.region.length()) / rate));
      startVoice(previews[static_cast<std::size_t>(previewIndex)], src, req.region, req.tx, gate, 0, 0);
    }
    previewTail.store(tail, std::memory_order_release);
  }

  void renderVoice(Voice& v, int begin, int end) {
    for (int i = begin; i < end; ++i) {
      if (!v.active) return;
      if (v.age >= v.gateFrames) {
        v.active = false;
        return;
      }
      const std::int64_t i0 = static_cast<std::int64_t>(std::floor(v.pos));
      if (i0 < v.regionStart || i0 >= v.regionEnd) {
        v.active = false;
        return;
      }
      const std::int64_t i1 = std::min(i0 + 1, v.regionEnd - 1);
      const float frac = static_cast<float>(v.pos - static_cast<double>(i0));
      float l = v.ch0[i0] + (v.ch0[i1] - v.ch0[i0]) * frac;
      float r = v.ch1[i0] + (v.ch1[i1] - v.ch1[i0]) * frac;
      if (v.crunchOn) {
        if (v.holdCount <= 0) {
          v.heldL = std::floor(l * v.crushLevels + 0.5f) / v.crushLevels;
          v.heldR = std::floor(r * v.crushLevels + 0.5f) / v.crushLevels;
          v.holdCount = v.holdFrames;
        }
        --v.holdCount;
        l = v.heldL;
        r = v.heldR;
      }

      float env = 1.f;
      if (v.attack > 0 && v.age < v.attack) env = static_cast<float>(v.age) / static_cast<float>(v.attack);
      const std::int64_t toGateEnd = v.gateFrames - v.age;
      if (toGateEnd < v.release) env = std::min(env, static_cast<float>(toGateEnd) / static_cast<float>(v.release));
      const double curStep = v.step * v.rateScale;
      const double remaining = curStep > 0.0 ? (static_cast<double>(v.regionEnd - 1) - v.pos) / curStep : (v.pos - static_cast<double>(v.regionStart)) / -curStep;
      if (remaining < static_cast<double>(v.release)) env = std::min(env, static_cast<float>(std::max(0.0, remaining) / static_cast<double>(v.release)));
      if (v.fading) {
        env *= static_cast<float>(v.fadeLeft) / static_cast<float>(v.fadeTotal);
        if (--v.fadeLeft <= 0) v.active = false;
      }
      env = std::max(0.f, env);
      v.lastLevel = env * std::max(v.gainL, v.gainR);
      float el = l * env, er = r * env;
      if (v.filterOn) {
        float* in2[2] = {&el, &er};
        for (int c = 0; c < 2; ++c) {
          const float x = *in2[c];
          const float v3 = x - v.ic2[c];
          const float v1 = v.fa1 * v.ic1[c] + v.fa2 * v3;
          const float v2 = v.ic2[c] + v.fa2 * v.ic1[c] + v.fa3 * v3;
          v.ic1[c] = 2.f * v1 - v.ic1[c];
          v.ic2[c] = 2.f * v2 - v.ic2[c];
          if (std::fabs(v.ic1[c]) < 1e-20f) v.ic1[c] = 0.f;
          if (std::fabs(v.ic2[c]) < 1e-20f) v.ic2[c] = 0.f;
          float y = v.highPass ? x - v.fk * v1 - v2 : v2;
          if (!std::isfinite(y)) {
            y = 0.f;
            v.ic1[c] = v.ic2[c] = 0.f;
          }
          *in2[c] = y;
        }
      }
      mixL[static_cast<std::size_t>(i)] += el * v.gainL;
      mixR[static_cast<std::size_t>(i)] += er * v.gainR;
      v.pos += v.step * v.rateScale;
      v.rateScale *= v.glideMul;
      ++v.age;
    }
  }

  void renderVoices(int begin, int end) {
    if (end <= begin) return;
    for (Voice& v : voices)
      if (v.active) renderVoice(v, begin, end);
    for (Voice& v : previews)
      if (v.active) renderVoice(v, begin, end);
  }

  void schedule(const Playback& pb, double pos0, double pos1, double tpf, int frames) {
    const double length = static_cast<double>(pb.lengthTicks);
    const std::vector<FlatEvent>& ev = pb.events;
    if (ev.empty() || pos1 <= 0.0) return;
    const double scanLo = pos0 - 2.0 * tpf;
    const double scanHi = pos1 + 2.0 * tpf;
    long long c0 = static_cast<long long>(std::floor(scanLo / length));
    const long long c1 = static_cast<long long>(std::floor(scanHi / length));
    if (c0 < 0) c0 = 0;
    for (long long c = c0; c <= c1; ++c) {
      const double base = static_cast<double>(c) * length;
      const double lo = scanLo - base;
      const double hi = scanHi - base;
      auto it = std::lower_bound(ev.begin(), ev.end(), lo, [](const FlatEvent& e, double v) { return static_cast<double>(e.start) < v; });
      for (; it != ev.end() && static_cast<double>(it->start) < hi; ++it) {
        // First frame whose time is >= the event time; the epsilon absorbs float noise at exact boundaries.
        const double offset = std::ceil((base + static_cast<double>(it->start) - pos0) / tpf - 1e-6);
        if (!(offset >= 0.0) || offset >= static_cast<double>(frames)) continue;
        if (triggers.size() < triggers.capacity())
          triggers.push_back({static_cast<int>(offset), &*it});
        else
          ++dropped;
      }
    }
    std::sort(triggers.begin(), triggers.end(), [](const Trigger& a, const Trigger& b) {
      return a.frame != b.frame ? a.frame < b.frame : a.event < b.event;
    });
  }

  void processChunk(const TransportBlock& tb, const RenderParams& params, const float* const* in, float* const* out, int nch, int frames) {
    const Playback* pb = mailbox.acquire();
    if (pb != current) {
      const SourceData* oldSrc = current ? current->source.get() : nullptr;
      const SourceData* newSrc = pb ? pb->source.get() : nullptr;
      if (oldSrc != newSrc) hardKillAll();  // voices point into the old source buffer, which may now be freed
      current = pb;
    }
    const bool sourceReady = pb && pb->source && pb->lengthTicks > 0;
    drainPreview(pb, params.enabled && sourceReady);
    if (!params.enabled || !sourceReady) {
      hardKillAll();
      wasPlaying = false;
      haveExpected = false;
      passthrough(in, out, nch, frames);
      return;
    }

    std::fill(mixL.begin(), mixL.begin() + frames, 0.f);
    std::fill(mixR.begin(), mixR.begin() + frames, 0.f);
    triggers.clear();

    if (pb->passThrough) {
      // Source loaded for audition but no pattern yet: input passes through, previews play on top.
      if (wasPlaying) releaseAll();
      wasPlaying = false;
      haveExpected = false;
      renderVoices(0, frames);
      for (int i = 0; i < frames; ++i) {
        const float x0 = (in && in[0]) ? in[0][i] : 0.f;
        const float x1 = (nch > 1 && in && in[1]) ? in[1][i] : x0;
        const float l = mixL[static_cast<std::size_t>(i)];
        const float r = mixR[static_cast<std::size_t>(i)];
        float o0 = nch == 2 ? x0 + l : x0 + 0.5f * (l + r);
        float o1 = x1 + r;
        if (!std::isfinite(o0)) o0 = 0.f;
        if (!std::isfinite(o1)) o1 = 0.f;
        out[0][i] = o0;
        if (nch == 2) out[1][i] = o1;
      }
      return;
    }

    double bpm = tb.bpm;
    if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;
    bpm = std::min(kMaxBpm, std::max(kMinBpm, bpm));
    const double tpf = bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * cfg.sampleRate);
    const double pos0 = (tb.positionValid && std::isfinite(tb.ppq)) ? tb.ppq * static_cast<double>(kTicksPerQuarter) : freeRunTicks;

    if (!tb.playing) {
      if (wasPlaying) releaseAll();
      wasPlaying = false;
      haveExpected = false;
    } else {
      const double pos1 = pos0 + static_cast<double>(frames) * tpf;
      if (wasPlaying && haveExpected && std::fabs(pos0 - expectedTicks) > 2.0 * tpf + 1e-6) releaseAll();  // seek or loop wrap
      if (tb.scheduleEvents) schedule(*pb, pos0, pos1, tpf, frames);
      expectedTicks = pos1;
      freeRunTicks = pos1;
      haveExpected = true;
      wasPlaying = true;
    }

    int cursor = 0;
    for (const Trigger& t : triggers) {
      if (t.frame > cursor) {
        renderVoices(cursor, t.frame);
        cursor = t.frame;
      }
      startFromEvent(*pb->source, *t.event, tpf);
    }
    renderVoices(cursor, frames);

    // Output stage: generated playback, plus optionally the current input; both gains are smoothed
    // per sample so the result does not depend on the host block size.
    const double dB = std::isfinite(params.outputGainDb) ? std::min(24.0, std::max(-96.0, static_cast<double>(params.outputGainDb))) : 0.0;
    const double targetGain = std::pow(10.0, dB / 20.0);
    const double targetDry = std::isfinite(params.dryMix) ? std::min(1.0, std::max(0.0, static_cast<double>(params.dryMix))) : 0.0;
    const float* in0 = (in && in[0]) ? in[0] : nullptr;
    const float* in1 = (nch > 1 && in && in[1]) ? in[1] : in0;
    for (int i = 0; i < frames; ++i) {
      outGain += (targetGain - outGain) * smoothCoef;
      dry += (targetDry - dry) * smoothCoef;
      if (std::fabs(targetGain - outGain) < 1e-7) outGain = targetGain;
      if (std::fabs(targetDry - dry) < 1e-7) dry = targetDry;
      const float x0 = in0 ? in0[i] : 0.f;
      const float x1 = in1 ? in1[i] : 0.f;
      const float l = mixL[static_cast<std::size_t>(i)];
      const float r = mixR[static_cast<std::size_t>(i)];
      float o0, o1 = 0.f;
      if (nch == 2) {
        o0 = static_cast<float>((l + dry * x0) * outGain);
        o1 = static_cast<float>((r + dry * x1) * outGain);
      } else {
        o0 = static_cast<float>((0.5 * (l + r) + dry * x0) * outGain);
      }
      if (!std::isfinite(o0) || std::fabs(o0) < 1e-30f) o0 = 0.f;
      if (!std::isfinite(o1) || std::fabs(o1) < 1e-30f) o1 = 0.f;
      out[0][i] = o0;
      if (nch == 2) out[1][i] = o1;
    }
  }

  void process(const TransportBlock& tb, const RenderParams& params, const float* const* in, float* const* out, int nch, int frames) {
    if (frames <= 0 || nch < 1 || nch > 2 || !out || !out[0] || (nch == 2 && !out[1])) return;
    if (!prepared) {
      passthrough(in, out, nch, frames);
      return;
    }
    if (frames <= cfg.maxBlock) {
      processChunk(tb, params, in, out, nch, frames);
      return;
    }
    // A host block larger than prepared: process in chunks rather than overrunning scratch memory.
    double bpm = (std::isfinite(tb.bpm) && tb.bpm > 0.0) ? std::min(kMaxBpm, std::max(kMinBpm, tb.bpm)) : 120.0;
    for (int done = 0; done < frames; done += cfg.maxBlock) {
      const int n = std::min(cfg.maxBlock, frames - done);
      const float* inp[2] = {nullptr, nullptr};
      float* outp[2] = {nullptr, nullptr};
      for (int c = 0; c < nch; ++c) {
        inp[c] = (in && in[c]) ? in[c] + done : nullptr;
        outp[c] = out[c] + done;
      }
      TransportBlock t2 = tb;
      if (tb.positionValid && std::isfinite(tb.ppq)) t2.ppq = tb.ppq + static_cast<double>(done) * bpm / (60.0 * cfg.sampleRate);
      processChunk(t2, params, inp, outp, nch, n);
    }
  }
};

Renderer::Renderer() : impl_(new Impl) {}
Renderer::~Renderer() = default;

void Renderer::prepare(const Config& config) { impl_->prepare(config); }
Mailbox<Playback>& Renderer::mailbox() { return impl_->mailbox; }

void Renderer::process(const TransportBlock& transport, const RenderParams& params, const float* const* in, float* const* out,
                       int numChannels, int frames) {
  impl_->process(transport, params, in, out, numChannels, frames);
}

bool Renderer::requestPreview(const PreviewRequest& request) {
  const std::uint32_t head = impl_->previewHead.load(std::memory_order_relaxed);
  const std::uint32_t tail = impl_->previewTail.load(std::memory_order_acquire);
  if (head - tail >= kPreviewQueue) return false;
  impl_->previewQueue[head % kPreviewQueue] = request;
  impl_->previewHead.store(head + 1, std::memory_order_release);
  return true;
}

void Renderer::reset() {
  impl_->hardKillAll();
  impl_->wasPlaying = false;
  impl_->haveExpected = false;
}

int Renderer::activeVoices() const {
  int n = 0;
  for (const Voice& v : impl_->voices)
    if (v.active) ++n;
  return n;
}

std::uint64_t Renderer::droppedTriggers() const { return impl_->dropped; }

Result<std::vector<std::vector<float>>> renderOffline(std::shared_ptr<const Playback> playback, const OfflineSettings& s) {
  if (!playback || !playback->source) return makeError(ErrorCode::InvalidArgument, "no playback to render");
  if (!(s.sampleRate >= 8000.0 && s.sampleRate <= 384000.0)) return makeError(ErrorCode::OutOfRange, "invalid sample rate");
  if (!(s.bpm >= kMinBpm && s.bpm <= kMaxBpm)) return makeError(ErrorCode::OutOfRange, "invalid tempo");
  if (s.blockSize < 1 || s.blockSize > 16384 || s.cycles < 1 || s.cycles > 64 || !(s.tailSeconds >= 0.0 && s.tailSeconds <= 30.0))
    return makeError(ErrorCode::OutOfRange, "invalid render settings");

  Renderer r;
  Config cfg;
  cfg.sampleRate = s.sampleRate;
  cfg.maxBlock = s.blockSize;
  r.prepare(cfg);
  r.mailbox().publish(playback);

  const double tpf = s.bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * s.sampleRate);
  const std::int64_t mainFrames = static_cast<std::int64_t>(std::ceil(static_cast<double>(s.cycles) * static_cast<double>(playback->lengthTicks) / tpf));
  const std::int64_t tailFrames = static_cast<std::int64_t>(std::ceil(s.tailSeconds * s.sampleRate));
  const std::int64_t total = mainFrames + tailFrames;
  if (total > static_cast<std::int64_t>(s.sampleRate) * 3600) return makeError(ErrorCode::LimitExceeded, "render would exceed one hour");

  std::vector<std::vector<float>> out(2, std::vector<float>(static_cast<std::size_t>(total), 0.f));
  std::vector<float> silence(static_cast<std::size_t>(s.blockSize), 0.f);
  // Two phases so that no block ever straddles the end of the pattern: the output must not depend on the
  // block size. Phase 1 schedules events; phase 2 lets sounding voices ring out without starting new ones.
  auto renderRange = [&](std::int64_t from, std::int64_t to, bool schedule) {
    for (std::int64_t pos = from; pos < to; pos += s.blockSize) {
      const int n = static_cast<int>(std::min<std::int64_t>(s.blockSize, to - pos));
      TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.bpm = s.bpm;
      tb.ppq = static_cast<double>(pos) * s.bpm / (60.0 * s.sampleRate);
      tb.scheduleEvents = schedule;
      const float* in[2] = {silence.data(), silence.data()};
      float* o[2] = {out[0].data() + pos, out[1].data() + pos};
      r.process(tb, s.params, in, o, 2, n);
    }
  };
  renderRange(0, mainFrames, true);
  renderRange(mainFrames, total, false);
  return out;
}

}  // namespace chopfractal::render
