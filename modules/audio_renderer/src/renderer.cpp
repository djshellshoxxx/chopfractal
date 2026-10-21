#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/audio_renderer/slice_quality.hpp>

#include "armed_switch.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace chopfractal::render {
namespace {

constexpr int kDefaultAttack = 32;
constexpr int kDefaultRelease = 64;
constexpr int kKillFade = 64;
constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 999.0;
constexpr std::uint32_t kPreviewQueue = 16;
constexpr double kPi = 3.14159265358979323846;
// Quality stage (research section 7), milliseconds.
constexpr double kAttackMsHit = 0.8, kAttackMsAir = 1.5, kAttackMsTexture = 3.0, kAttackMsReverse = 4.0;
constexpr double kReleaseMsHit = 3.0, kReleaseMsCymbal = 5.0, kReleaseMsRapid = 1.5, kReleaseMsReverse = 0.3;
constexpr double kStealFadeMs = 3.0, kKickChokeMs = 3.0, kSnareChokeMs = 6.0, kHatChokeMs = 1.5, kDuckMs = 5.0;
constexpr float kDuckGain = 0.5f;  // -6 dB
constexpr Ticks kRapidTicks = 120;  // a hit shorter than a 1/32 note is a rapid repeat

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
  // Quality stage and gesture effects (all inert by default).
  bool cosFade = false;        // raised-cosine fades instead of linear ramps
  // Engine v2 hints from the event (0 / false = the legacy behaviour, bit for bit).
  std::uint8_t chokeGroup = 0;
  bool smoothFades = false;    // smoothstep ramps instead of linear
  SliceClass cls = SliceClass::Other;
  bool repeat = false;
  bool secondary = false;
  float duck = 1.f, duckTarget = 1.f, duckStep = 0.f;  // sidechain-style duck of a kick under a snare
  bool lpOn = false;           // Echo tone
  float lpA = 0.f;
  float lpState[2] = {0.f, 0.f};
  bool tremOn = false;         // Tremolo-Pan
  bool tremAmp = false, tremPan = false;
  double tremInc = 0.0;
  float tremDepth = 0.f;
  float level = 1.f, panBase = 0.f;
  int panDir = 1;
  // Performance pads (F07): tag 2 marks a pad voice. Pad voices survive transport stop, seek and loop wrap.
  std::uint8_t tag = 0;
  std::uint8_t padNote = 0;
  std::uint8_t padGroup = 0;
  // Output bus (F11): 0 main, 1..kMaxAuxBuses aux. A bus the host has not enabled folds into main at render time.
  std::uint8_t bus = 0;
};

constexpr std::uint8_t kTagPad = 2;

struct Trigger {
  int frame;
  const FlatEvent* event;
};

float smoothstep01(float x) { return x * x * (3.f - 2.f * x); }  // x in [0, 1]

// Which sounding groups a new hit of group `incoming` cuts: a kick or snare cuts its own previous hit, a closed hat
// cuts earlier closed hats and open hats, an open hat cuts earlier open hats (groups: 1 kick, 2 snare, 3 closed hat, 4 open hat).
bool chokes(std::uint8_t incoming, std::uint8_t sounding) {
  if (incoming == 0 || sounding == 0) return false;
  if (incoming == 3) return sounding == 3 || sounding == 4;
  return incoming == sounding;
}

bool finiteIn(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

// Finite and |a| >= 1e-30 (so no NaN, Inf, zero or denormal), else 0. Done on the bit pattern: MSVC compiles std::isfinite(float)
// to a library call, which made the 16-lane aux write loop several times slower there than on GCC/Clang.
inline float keepAuxSample(float a) {
  constexpr std::uint32_t kMinBits = 0x0DA24260u;  // bit pattern of 1e-30f
  constexpr std::uint32_t kInfBits = 0x7F800000u;
  std::uint32_t u;
  std::memcpy(&u, &a, sizeof u);
  u &= 0x7FFFFFFFu;
  return (u - kMinBits) < (kInfBits - kMinBits) ? a : 0.f;
}

}  // namespace

struct Renderer::Impl {
  Config cfg;
  bool prepared = false;
  Mailbox<Playback> mailbox;
  const Playback* current = nullptr;
  const Playback* mainSeen = nullptr;  // last pointer delivered by `mailbox` (never dereferenced)
  armed::State arm;                     // F09 armed loop-boundary switch

  std::vector<Voice> voices;
  std::array<Voice, 2> previews;
  int previewIndex = 0;
  std::vector<float> mixL, mixR;
  std::vector<float> auxMix;                 // cfg.auxBuses x 2 x maxBlock, bus-major (prepare allocates it)
  std::vector<float> auxDelay;               // cfg.auxBuses x 2 x limiter latency: keeps the aux buses aligned with a limited main
  int auxDelayLen = 0;
  int auxDelayPos = 0;
  bool auxDelayActive = false;
  std::vector<double> gainRamp;              // outGain per frame of the chunk, for the aux lanes (cfg.maxBlock)
  std::array<bool, kMaxAuxBuses> auxDirty{}; // a voice mixed into this aux bus in the chunk being processed
  int activeAux = 0;                         // 1 + the highest aux bus written by the chunk being processed (0 = none)
  unsigned auxMask = 0;                      // bit b: aux bus b + 1 is written by the chunk being processed
  const AuxOutputs* auxOut = nullptr;
  std::vector<Trigger> triggers;
  Mailbox<PadBank> padBank;
  const PadBank* bank = nullptr;             // acquired once per chunk
  const PadEvent* padEvents = nullptr;       // the live input of the call being processed
  int numPadEvents = 0;
  int padOffset = 0;                         // frame of the current chunk within the host block
  PadParams padParams;
  std::atomic<int> padVoices{0};
  std::atomic<std::uint64_t> padDroppedCount{0};
  std::atomic<std::uint64_t> padUnmappedCount{0};

  double outGain = 1.0;
  double dry = 0.0;
  bool gainsPrimed = false;  // first block after prepare() starts at the targets instead of ramping from defaults
  double smoothCoef = 0.01;

  bool wasPlaying = false;
  bool haveExpected = false;
  double expectedTicks = 0.0;
  double freeRunTicks = 0.0;
  const SourceData* currentSource = nullptr;  // source of `current`, kept so the old Playback is never read after a switch
  std::uint64_t serialCounter = 0;
  std::uint64_t dropped = 0;

  Limiter limiter;
  std::atomic<int> latency{0};
  double matchGain = 1.0, matchTarget = 1.0, matchStep = 0.0;  // linear; ramped per sample
  int matchRampLeft = 0;
  bool polish = false;                      // RenderParams::slicePolish of the block being processed
  std::uint8_t fxMask = fxbit::kAll;        // RenderParams::fxMask of the block being processed
  std::array<double, 1025> sineTable{};     // one period of sine, for the tremolo-pan LFO (built in prepare)

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
    cfg.auxBuses = std::max(0, std::min(cfg.auxBuses, kMaxAuxBuses));
    auxMix.assign(static_cast<std::size_t>(cfg.auxBuses) * 2 * static_cast<std::size_t>(cfg.maxBlock), 0.f);
    auxDelayLen = cfg.auxBuses > 0 ? std::max(1, Limiter::latencyFor(cfg.sampleRate)) : 0;
    auxDelay.assign(static_cast<std::size_t>(cfg.auxBuses) * 2 * static_cast<std::size_t>(auxDelayLen), 0.f);
    auxDelayPos = 0;
    auxDelayActive = false;
    gainRamp.assign(cfg.auxBuses > 0 ? static_cast<std::size_t>(cfg.maxBlock) : 0, 1.0);
    auxDirty.fill(false);
    activeAux = 0;
    auxMask = 0;
    triggers.clear();
    triggers.reserve(kMaxPreparedEvents);
    smoothCoef = 1.0 - std::exp(-1.0 / (0.01 * cfg.sampleRate));
    for (std::size_t i = 0; i < sineTable.size(); ++i) sineTable[i] = std::sin(2.0 * kPi * static_cast<double>(i) / 1024.0);
    limiter.prepare(cfg.sampleRate, cfg.maxBlock);
    latency.store(0, std::memory_order_relaxed);
    matchGain = matchTarget = 1.0;
    matchStep = 0.0;
    matchRampLeft = 0;
    outGain = 1.0;
    dry = 0.0;
    gainsPrimed = false;
    wasPlaying = false;
    haveExpected = false;
    previewHead.store(0);
    previewTail.store(0);
    arm.reset();
    prepared = true;
  }

  void hardKillAll() {
    for (Voice& v : voices) v.active = false;
    for (Voice& v : previews) v.active = false;
  }

  // Fades a sounding voice to silence over `frames` (never cuts). A voice already fading keeps its shorter fade.
  void fadeVoice(Voice& v, int frames) {
    if (!v.active) return;
    frames = std::max(1, frames);
    if (v.fading && v.fadeLeft <= frames) return;
    v.fading = true;
    v.fadeTotal = frames;
    v.fadeLeft = frames;
  }

  int msToFrames(double ms) const { return std::max(1, static_cast<int>(std::lround(ms * cfg.sampleRate / 1000.0))); }

  void releaseVoice(Voice& v) {
    if (!v.active || v.fading) return;
    v.fading = true;
    const int f = polish ? msToFrames(kStealFadeMs) : kKillFade;  // research 7.2: 3 ms for non-transient content
    v.fadeTotal = f;
    v.fadeLeft = f;
  }

  void releaseAll() {
    for (Voice& v : voices)
      if (v.tag != kTagPad) releaseVoice(v);  // pad voices belong to the player, not to the transport
  }

  void releasePadVoices(std::uint8_t note, bool anyNote, std::uint8_t group, bool anyGroup) {
    for (Voice& v : voices)
      if (v.active && v.tag == kTagPad && (anyNote || v.padNote == note) && (anyGroup || v.padGroup == group)) releaseVoice(v);
  }

  // One pad event at its exact frame. The source must be ready (callers check).
  void applyPad(const PadEvent& e, const SourceData& src) {
    switch (e.type) {
      case PadEvent::Type::AllNotesOff: releasePadVoices(0, true, 0, true); return;
      case PadEvent::Type::NoteOff:
        if (padParams.mode == PadMode::Gate && e.note < kPadNotes) releasePadVoices(e.note, false, 0, true);  // one-shots ignore note-off
        return;
      case PadEvent::Type::NoteOn: break;
    }
    if (e.note >= kPadNotes || !bank) return;
    const PadSlot& slot = bank->slots[e.note];
    if (slot.region.empty() || slot.region.start < 0 || slot.region.end > src.frames) {
      padUnmappedCount.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (padParams.mode == PadMode::Mono) releasePadVoices(0, true, 0, true);
    else if (padParams.mode == PadMode::Gate) releasePadVoices(e.note, false, 0, true);
    if (slot.chokeGroup != 0) releasePadVoices(0, true, slot.chokeGroup, false);
    Voice* v = allocateVoice();
    if (!v) return;
    EventTransform tx;
    tx.level = velocityGain(padParams.curve, e.velocity);
    const double rate = static_cast<double>(src.sampleRate) / cfg.sampleRate;  // pitch 0
    const std::int64_t gate = static_cast<std::int64_t>(std::ceil(static_cast<double>(slot.region.length()) / rate));  // the whole chop plays
    startVoice(*v, src, slot.region, tx, gate, slot.fadeInFrames, slot.fadeOutFrames);
    v->tag = kTagPad;
    v->padNote = e.note;
    v->padGroup = slot.chokeGroup;
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

  // `slice` is null for auditions and previews (the v1 path). `tpf` is ticks per output frame.
  void startVoice(Voice& v, const SourceData& src, SampleRange region, const EventTransform& tx, std::int64_t gateFrames,
                  std::uint32_t fadeIn, std::uint32_t fadeOut, const EventFx& fx = EventFx{}, const PreparedSlice* slice = nullptr,
                  double tpf = 0.0, Ticks eventTicks = 0) {
    const double ratio = static_cast<double>(src.sampleRate) / cfg.sampleRate;
    const bool q = polish && slice;
    float pitch = tx.pitchSemitones;
    float level = tx.level;
    float pan = tx.pan;
    if (slice) {
      pitch += slice->pitchOffset;
      pan = std::min(1.f, std::max(-1.f, pan + slice->panOffset));
      if (q) {
        pitch += slice->pitchJitter;
        level *= slice->trim * slice->gainJitter;
      }
      pitch = std::min(limits::kMaxPitchSemitones, std::max(-limits::kMaxPitchSemitones, pitch));
    }
    const double rate = ratio * std::exp2(static_cast<double>(pitch) / 12.0);
    if (slice && slice->freezeUnit) {
      // Roll: the frozen grab is as long as one repeat, so a palindrome plays exactly the same audio backwards.
      const std::int64_t unit = static_cast<std::int64_t>(std::ceil(static_cast<double>(std::max<std::int64_t>(1, gateFrames)) * rate)) + 1;
      region.end = std::min(region.end, region.start + unit);
    }
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
    std::int64_t a, r;
    if (q) {
      // Research 7.1: short raised-cosine fades by class; the long fade belongs at the start of a reversed slice
      // (its transient is now the end) and a very short one at its end. A chop's own fade settings still win.
      double attackMs = kAttackMsTexture, releaseMs = kReleaseMsHit;
      if (slice->cls == SliceClass::Kick || slice->cls == SliceClass::Snare) attackMs = kAttackMsHit;
      else if (slice->cls == SliceClass::Hat || slice->cls == SliceClass::Cymbal) attackMs = kAttackMsAir;
      if (slice->cls == SliceClass::Cymbal) releaseMs = kReleaseMsCymbal;
      if (eventTicks > 0 && eventTicks < kRapidTicks) releaseMs = kReleaseMsRapid;
      if (tx.reverse) {
        attackMs = kAttackMsReverse;
        // The very short release belongs where the reversed slice reaches its (former) transient; a gate that cuts the
        // swell short gets the normal release instead, or the cut would click.
        if (static_cast<double>(gate) * rate >= static_cast<double>(region.end - region.start) - 1.0) releaseMs = kReleaseMsReverse;
      }
      a = fadeIn ? std::llround(static_cast<double>(fadeIn) / ratio) : msToFrames(attackMs);
      r = fadeOut ? std::llround(static_cast<double>(fadeOut) / ratio) : msToFrames(releaseMs);
      v.cosFade = true;
    } else {
      a = fadeIn ? std::llround(static_cast<double>(fadeIn) / ratio) : kDefaultAttack;
      r = fadeOut ? std::llround(static_cast<double>(fadeOut) / ratio) : kDefaultRelease;
    }
    a = std::min<std::int64_t>(a, gate / 2);
    r = std::min<std::int64_t>(r, gate / 2);
    if (slice && slice->tailFrac >= 0.f) {
      // Stutter/Roll gate shape: the audible part releases over `tail` of its length (low is choppy, high is washy).
      const std::int64_t minRel = q ? msToFrames(kReleaseMsRapid) : 1;
      r = std::min<std::int64_t>(gate, std::max<std::int64_t>(minRel, std::llround(static_cast<double>(slice->tailFrac) * static_cast<double>(gate))));
    }
    v.attack = static_cast<int>(a);
    v.release = static_cast<int>(std::max<std::int64_t>(1, r));
    v.level = level;
    v.panBase = pan;
    v.gainL = level * (pan > 0.f ? 1.f - pan : 1.f);
    v.gainR = level * (pan < 0.f ? 1.f + pan : 1.f);
    if (slice) {
      v.cls = slice->cls;
      v.repeat = slice->repeat;
      v.secondary = slice->secondary;
      if (slice->echoLpHz > 0.f && (fxMask & fxbit::kEcho)) {
        v.lpOn = true;
        v.lpA = static_cast<float>(1.0 - std::exp(-2.0 * kPi * static_cast<double>(slice->echoLpHz) / cfg.sampleRate));
      }
      if (slice->tremMode <= 2 && slice->tremDepth > 0.f && slice->tremPeriodTicks > 0 && tpf > 0.0 && (fxMask & fxbit::kTremPan)) {
        v.tremOn = true;
        v.tremAmp = slice->tremMode != 1;
        v.tremPan = slice->tremMode != 0;
        v.tremDepth = slice->tremDepth;
        v.tremInc = tpf / static_cast<double>(slice->tremPeriodTicks);
        v.panDir = slice->panDir;
      }
    }
    if (fx.glideSemitones != 0.f) v.glideMul = std::exp2((static_cast<double>(fx.glideSemitones) / 12.0) / static_cast<double>(gate));
    if (fx.crush > 0.f) {
      const double c = fx.crush;
      v.crunchOn = true;
      v.crushLevels = static_cast<float>(std::exp2(16.0 - 12.0 * c - 1.0));
      v.holdFrames = 1 + static_cast<int>(std::lround(15.0 * c * c));
    }
    if (fx.filter != FilterType::Off) {
      const double fc = std::min(20.0 * std::pow(1000.0, static_cast<double>(fx.cutoff)), 0.45 * cfg.sampleRate);
      const double qf = 0.71 + (8.0 - 0.71) * static_cast<double>(fx.resonance);
      const double g = std::tan(3.14159265358979323846 * fc / cfg.sampleRate);
      const double k = 1.0 / qf;
      const double a1 = 1.0 / (1.0 + g * (g + k));
      v.filterOn = true;
      v.highPass = fx.filter == FilterType::HighPass;
      v.fk = static_cast<float>(k);
      v.fa1 = static_cast<float>(a1);
      v.fa2 = static_cast<float>(g * a1);
      v.fa3 = static_cast<float>(g * g * a1);
    }
  }

  // `secondary` (an Echo repeat) never steals a primary voice: when only primaries are left it is dropped (nullptr).
  Voice* allocateVoice(bool secondary = false) {
    int playing = 0;
    for (const Voice& v : voices)
      if (v.active && !v.fading) ++playing;
    if (playing >= cfg.maxVoices) {
      // Steal the quietest sounding voice; voices still in their attack are protected (their level is not
      // yet meaningful) unless every voice is. Ties go to the oldest. Echo repeats go first.
      Voice* victim = nullptr;
      for (int pass = 0; pass < 2 && !victim; ++pass)
        for (Voice& v : voices) {
          if (!v.active || v.fading) continue;
          if (pass == 0 && v.age < v.attack) continue;
          if (secondary && !v.secondary) continue;
          if (!victim || (v.secondary && !victim->secondary) ||
              (v.secondary == victim->secondary && (v.lastLevel < victim->lastLevel || (v.lastLevel == victim->lastLevel && v.serial < victim->serial))))
            victim = &v;
        }
      if (!victim && secondary) return nullptr;
      if (victim) releaseVoice(*victim);  // fades out instead of cutting
    }
    for (Voice& v : voices)
      if (!v.active) return &v;
    if (secondary) return nullptr;
    // Pool exhausted. Recycle the fading voice closest to silence; only if none is fading, the oldest sounding one.
    Voice* nearest = nullptr;
    for (Voice& v : voices)
      if (v.fading && (!nearest || v.fadeLeft < nearest->fadeLeft)) nearest = &v;
    if (!nearest)
      for (Voice& v : voices)
        if (!nearest || v.serial < nearest->serial) nearest = &v;
    return nearest;
  }

  // Voice choke between drum tails (research 7.2), applied when a new hit starts: a kick fades the previous kick, a
  // snare fades the previous snare (unless it is a roll or stutter repeat, which overlap on purpose) and ducks a kick
  // that is still ringing by 6 dB instead of cutting it, a closed hat chokes an open hat or cymbal.
  void applyChoke(const PreparedSlice& s) {
    if (s.secondary) return;
    for (Voice& v : voices) {
      if (!v.active || v.fading) continue;
      if (s.cls == SliceClass::Kick && v.cls == SliceClass::Kick) {
        fadeVoice(v, msToFrames(kKickChokeMs));
      } else if (s.cls == SliceClass::Snare && !s.repeat) {
        if (v.cls == SliceClass::Snare) {
          fadeVoice(v, msToFrames(kSnareChokeMs));
        } else if (v.cls == SliceClass::Kick) {
          v.duckTarget = kDuckGain;
          v.duckStep = (v.duck - kDuckGain) / static_cast<float>(msToFrames(kDuckMs));
        }
      } else if (s.cls == SliceClass::Hat && !s.repeat && v.cls == SliceClass::Cymbal) {
        fadeVoice(v, msToFrames(kHatChokeMs));
      }
    }
  }

  void startFromEvent(const SourceData& src, const FlatEvent& e, const PreparedSlice& slice, double ticksPerFrame, std::uint8_t bus) {
    if (e.region.empty() || e.region.start < 0 || e.region.end > src.frames) return;
    if (slice.fxBit && !(fxMask & slice.fxBit)) return;           // that effect is bypassed
    if (slice.replacedBy && (fxMask & slice.replacedBy)) return;  // its repeats play instead of this base event
    if (e.chokeGroup != 0) {  // the engine named the roles: those groups decide, not the inferred slice classes
      for (Voice& old : voices)
        if (old.active && !old.fading && chokes(e.chokeGroup, old.chokeGroup)) releaseVoice(old);  // short fade, no click
    } else if (polish) {
      applyChoke(slice);
    }
    Voice* v = allocateVoice(slice.secondary);
    if (!v) return;
    SampleRange region = e.region;
    if (polish && slice.regionStart >= 0 && slice.regionStart < region.start) region.start = slice.regionStart;
    const std::int64_t gate = static_cast<std::int64_t>(std::llround(static_cast<double>(e.duration) / ticksPerFrame));
    startVoice(*v, src, region, e.tx, gate, e.fadeInFrames, e.fadeOutFrames, e.fx, &slice, ticksPerFrame, e.duration);
    v->chokeGroup = e.chokeGroup;
    v->smoothFades = e.smoothFades;
    v->bus = bus;
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

  // One period of sine from the table, argument in cycles (any real).
  float sineCycles(double cycles) const {
    const double x = cycles - std::floor(cycles);
    const double pos = x * 1024.0;
    const int i = static_cast<int>(pos);
    const double f = pos - static_cast<double>(i);
    return static_cast<float>(sineTable[static_cast<std::size_t>(i)] + (sineTable[static_cast<std::size_t>(i) + 1] - sineTable[static_cast<std::size_t>(i)]) * f);
  }

  void renderVoice(Voice& v, int begin, int end) {
    // Raised-cosine ramp for polished voices, smoothstep for engine v2 hints, the plain linear ramp (the v1 sound) otherwise.
    auto shape = [&v](float x) {
      if (v.cosFade) return 0.5f - 0.5f * std::cos(static_cast<float>(kPi) * x);
      return v.smoothFades ? smoothstep01(x) : x;
    };
    // A voice on a bus the host did not enable plays on main.
    const int bus = (v.bus != 0 && ((auxMask >> (v.bus - 1)) & 1u)) ? v.bus : 0;
    float* const dstL = bus == 0 ? mixL.data() : auxMix.data() + (static_cast<std::size_t>(bus - 1) * 2) * static_cast<std::size_t>(cfg.maxBlock);
    float* const dstR = bus == 0 ? mixR.data() : dstL + cfg.maxBlock;
    if (bus != 0) auxDirty[static_cast<std::size_t>(bus - 1)] = true;
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
      if (v.lpOn) {  // Echo tone: each repeat is darker than the one before
        v.lpState[0] += v.lpA * (l - v.lpState[0]);
        v.lpState[1] += v.lpA * (r - v.lpState[1]);
        if (std::fabs(v.lpState[0]) < 1e-20f) v.lpState[0] = 0.f;
        if (std::fabs(v.lpState[1]) < 1e-20f) v.lpState[1] = 0.f;
        l = v.lpState[0];
        r = v.lpState[1];
      }
      float gainL = v.gainL, gainR = v.gainR;
      if (v.tremOn) {
        // Phase-locked to the hit: cycles = age / period. Tremolo starts at full level; pan starts at centre moving
        // right (left for odd event indices) with an equal-power law that keeps the centre at unity.
        const double cyc = static_cast<double>(v.age) * v.tremInc;
        if (v.tremAmp) {
          const float gT = 1.f - v.tremDepth * (0.5f - 0.5f * sineCycles(cyc + 0.25));
          l *= gT;
          r *= gT;
        }
        if (v.tremPan) {
          const double x = cyc - std::floor(cyc);
          const float tri = static_cast<float>(x < 0.25 ? 4.0 * x : (x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0));
          const float p = std::min(1.f, std::max(-1.f, v.panBase + v.tremDepth * tri * static_cast<float>(v.panDir)));
          const double a = (static_cast<double>(p) + 1.0) * 0.125;  // (p + 1) * pi / 4 in cycles
          gainL = v.level * 1.41421356f * sineCycles(a + 0.25);
          gainR = v.level * 1.41421356f * sineCycles(a);
        }
      }

      float env = 1.f;
      if (v.attack > 0 && v.age < v.attack) env = shape(static_cast<float>(v.age) / static_cast<float>(v.attack));
      const std::int64_t toGateEnd = v.gateFrames - v.age;
      if (toGateEnd < v.release) env = std::min(env, shape(static_cast<float>(toGateEnd) / static_cast<float>(v.release)));
      const double curStep = v.step * v.rateScale;
      const double remaining = curStep > 0.0 ? (static_cast<double>(v.regionEnd - 1) - v.pos) / curStep : (v.pos - static_cast<double>(v.regionStart)) / -curStep;
      // Fade out at the region end, but never over more than half of a short region (a 1-frame region has no fade).
      const double regionLen = static_cast<double>(v.regionEnd - v.regionStart);
      if (regionLen > 1.0) {
        const double endFade = std::min(static_cast<double>(v.release), std::max(1.0, 0.5 * regionLen));
        if (remaining < endFade) env = std::min(env, shape(static_cast<float>(std::max(0.0, remaining) / endFade)));
      }
      if (v.fading) {
        env *= shape(static_cast<float>(v.fadeLeft) / static_cast<float>(v.fadeTotal));
        if (--v.fadeLeft <= 0) v.active = false;
      }
      if (v.duck > v.duckTarget) {
        v.duck -= v.duckStep;
        if (v.duck <= v.duckTarget) {
          v.duck = v.duckTarget;
          v.duckStep = 0.f;
        }
      }
      if (v.duck != 1.f) env *= v.duck;
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
      dstL[i] += el * gainL;
      dstR[i] += er * gainR;
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

  // Renders [0, frames), starting voices from the pattern triggers and the pad events in frame order. At equal frames the
  // pattern goes first. Pad events carry frames of the whole host block; padOffset rebases them for an oversize-block chunk.
  void renderWithEvents(const Playback& pb, double tpf, int frames) {
    constexpr int kNone = 0x7fffffff;
    std::size_t ti = 0;
    int pi = 0;
    int cursor = 0;
    while (pi < numPadEvents && padEvents[pi].frame < padOffset) ++pi;  // events of earlier chunks
    for (;;) {
      const int tf = ti < triggers.size() ? triggers[ti].frame : kNone;
      const int pf = (pi < numPadEvents && padEvents[pi].frame - padOffset < frames) ? padEvents[pi].frame - padOffset : kNone;
      if (tf == kNone && pf == kNone) break;
      const int at = std::min(tf, pf);
      if (at > cursor) {
        renderVoices(cursor, at);
        cursor = at;
      }
      if (tf <= pf) {
        const Trigger& t = triggers[ti++];
        const std::size_t idx = static_cast<std::size_t>(t.event - pb.events.data());
        startFromEvent(*pb.source, *t.event, pb.slices[idx], tpf, idx < pb.eventBus.size() ? pb.eventBus[idx] : std::uint8_t{0});
      } else {
        applyPad(padEvents[pi++], *pb.source);
      }
    }
    renderVoices(cursor, frames);
  }

  void resetAuxDelay() {
    if (!auxDelayActive) return;
    std::fill(auxDelay.begin(), auxDelay.end(), 0.f);
    auxDelayPos = 0;
    auxDelayActive = false;
  }

  // The raw aux buses: mix times the output gain (no dry, no level match, no limiter). Only buses a voice mixed into are
  // computed; with the limiter's alignment delay every lane runs so a ringing-out tail is never left behind.
  void writeAux(const AuxOutputs& aux, int frames, bool delayed) {
    const std::size_t stride = static_cast<std::size_t>(cfg.maxBlock);
    for (int b = 0; b < activeAux; ++b) {
      if (!((auxMask >> b) & 1u)) continue;
      if (!delayed && !auxDirty[static_cast<std::size_t>(b)]) continue;  // stays at the zeros written at the top of the chunk
      for (int c = 0; c < 2; ++c) {
        const std::size_t lane = static_cast<std::size_t>(b) * 2 + static_cast<std::size_t>(c);
        const float* m = auxMix.data() + lane * stride;
        float* dst = aux.bus[b][c];
        const double* g = gainRamp.data();
        for (int i = 0; i < frames; ++i) {
          dst[i] = keepAuxSample(static_cast<float>(m[i] * g[i]));
        }
        if (delayed) {
          float* ring = auxDelay.data() + lane * static_cast<std::size_t>(auxDelayLen);
          int pos = auxDelayPos;
          for (int i = 0; i < frames; ++i) {
            const float old = ring[pos];
            ring[pos] = dst[i];
            dst[i] = old;
            if (++pos >= auxDelayLen) pos = 0;
          }
        }
      }
    }
    if (delayed) auxDelayPos = (auxDelayPos + frames) % auxDelayLen;
  }

  // Adopt a Playback the main mailbox delivered. Only a pointer it has not delivered before counts: a scene switch made on
  // the audio thread (armed switch) must not be reverted when the session later publishes that same Playback. Compare
  // cached pointers only: once acquire() acknowledged a newer Playback the publisher may free the old one.
  void adoptMain() {
    const Playback* pb = mailbox.acquire();
    if (pb != mainSeen) {
      mainSeen = pb;
      const SourceData* newSrc = pb ? pb->source.get() : nullptr;
      if (currentSource != newSrc) hardKillAll();  // voices point into the old source buffer, which may now be freed
      current = pb;
      currentSource = newSrc;
    }
  }

  // F09: a block that contains the loop boundary of an armed scene is processed as two chunks, the second starting on the
  // boundary frame with the new Playback. Chunks reuse the oversize-block machinery (rebased pad events, shifted aux and
  // input/output pointers), so everything else sees an ordinary host block.
  void processArmed(const TransportBlock& tb, const RenderParams& params, const float* const* in, float* const* out, int nch, int frames,
                    const AuxOutputs& aux) {
    adoptMain();
    arm.poll(current);
    if (arm.pending() && params.enabled && tb.playing && tb.scheduleEvents) {
      double bpm = tb.bpm;
      if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;
      bpm = std::min(kMaxBpm, std::max(kMinBpm, bpm));
      const double tpf = bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * cfg.sampleRate);
      const double pos0 = (tb.positionValid && std::isfinite(tb.ppq)) ? tb.ppq * static_cast<double>(kTicksPerQuarter) : freeRunTicks;
      const armed::Split sp = arm.locate(current, currentSource, tb.playing, wasPlaying, pos0, tpf, frames);
      if (sp.offset >= 0) {
        const int savedPadOffset = padOffset;
        if (sp.offset > 0) processChunk(tb, params, in, out, nch, sp.offset, aux);
        current = arm.apply();  // same source buffer: sounding voices keep ringing across the switch
        TransportBlock t2 = tb;
        if (tb.positionValid && std::isfinite(tb.ppq)) t2.ppq = tb.ppq + sp.ppqAdvance;
        const float* inp[2] = {nullptr, nullptr};
        float* outp[2] = {nullptr, nullptr};
        for (int c = 0; c < nch; ++c) {
          inp[c] = (in && in[c]) ? in[c] + sp.offset : nullptr;
          outp[c] = out[c] + sp.offset;
        }
        AuxOutputs chunkAux;
        float* chunkPtrs[kMaxAuxBuses][2] = {};
        chunkAux.count = std::max(0, std::min(aux.count, kMaxAuxBuses));
        for (int b = 0; b < chunkAux.count; ++b) {
          for (int c = 0; c < 2; ++c) chunkPtrs[b][c] = (aux.bus[b] && aux.bus[b][c]) ? aux.bus[b][c] + sp.offset : nullptr;
          chunkAux.bus[b] = chunkPtrs[b];
        }
        padOffset = savedPadOffset + sp.offset;
        processChunk(t2, params, inp, outp, nch, frames - sp.offset, chunkAux);
        padOffset = savedPadOffset;
        return;
      }
    }
    processChunk(tb, params, in, out, nch, frames, aux);
  }

  void processChunk(const TransportBlock& tb, const RenderParams& params, const float* const* in, float* const* out, int nch, int frames,
                    const AuxOutputs& aux) {
    // Aux outputs: every enabled bus is written for every frame, whatever path this block takes (hosts do not clear them).
    const int auxCount = std::max(0, std::min(aux.count, kMaxAuxBuses));
    for (int b = 0; b < auxCount; ++b)
      for (int c = 0; c < 2; ++c)
        if (aux.bus[b] && aux.bus[b][c]) std::fill(aux.bus[b][c], aux.bus[b][c] + frames, 0.f);
    // An aux bus is served when the renderer was prepared for it and the host gave both channel pointers; a gap (Out 2 off,
    // Out 3 on) is fine. Anything routed to a bus that is not served plays on main.
    activeAux = 0;
    auxMask = 0;
    if (nch == 2) {
      const int limit = std::min(auxCount, cfg.auxBuses);
      for (int b = 0; b < limit; ++b)
        if (aux.bus[b] && aux.bus[b][0] && aux.bus[b][1]) {
          auxMask |= 1u << b;
          activeAux = b + 1;
        }
    }
    adoptMain();
    const Playback* pb = current;
    bank = padBank.acquire();
    const bool sourceReady = pb && pb->source && pb->lengthTicks > 0;
    polish = params.slicePolish;
    fxMask = params.fxMask;
    drainPreview(pb, params.enabled && sourceReady);
    if (!params.enabled || !sourceReady) {
      hardKillAll();
      wasPlaying = false;
      haveExpected = false;
      if (limiter.active()) limiter.reset();
      resetAuxDelay();
      latency.store(0, std::memory_order_relaxed);
      passthrough(in, out, nch, frames);
      return;
    }
    const LimiterParams lp{params.limiterEnabled, params.limiterCeilingDb};
    latency.store(params.limiterEnabled ? limiter.latencyFrames() : 0, std::memory_order_relaxed);

    std::fill(mixL.begin(), mixL.begin() + frames, 0.f);
    std::fill(mixR.begin(), mixR.begin() + frames, 0.f);
    for (int b = 0; b < kMaxAuxBuses; ++b) {
      if (!auxDirty[static_cast<std::size_t>(b)]) continue;  // nothing was mixed into it last chunk: still zero
      auxDirty[static_cast<std::size_t>(b)] = false;
      for (int c = 0; c < 2; ++c) {
        float* m = auxMix.data() + (static_cast<std::size_t>(b) * 2 + static_cast<std::size_t>(c)) * static_cast<std::size_t>(cfg.maxBlock);
        std::fill(m, m + cfg.maxBlock, 0.f);
      }
    }
    triggers.clear();

    if (pb->passThrough) {
      resetAuxDelay();  // the aux buses are silent here
      // Source loaded for audition but no pattern yet: input passes through, previews play on top.
      if (wasPlaying) releaseAll();
      wasPlaying = false;
      haveExpected = false;
      renderWithEvents(*pb, 0.0, frames);  // no pattern triggers; pad events still play
      const bool limitIt = params.limiterEnabled || limiter.active();
      for (int i = 0; i < frames; ++i) {
        const float x0 = (in && in[0]) ? in[0][i] : 0.f;
        const float x1 = (nch > 1 && in && in[1]) ? in[1][i] : x0;
        const float l = mixL[static_cast<std::size_t>(i)];
        const float r = mixR[static_cast<std::size_t>(i)];
        if (limitIt) {  // stereo for the limiter; the mono downmix happens after it
          mixL[static_cast<std::size_t>(i)] = std::isfinite(x0 + l) ? x0 + l : 0.f;
          mixR[static_cast<std::size_t>(i)] = std::isfinite(x1 + r) ? x1 + r : 0.f;
          continue;
        }
        float o0 = nch == 2 ? x0 + l : x0 + 0.5f * (l + r);
        float o1 = x1 + r;
        if (!std::isfinite(o0)) o0 = 0.f;
        if (!std::isfinite(o1)) o1 = 0.f;
        out[0][i] = o0;
        if (nch == 2) out[1][i] = o1;
      }
      if (limitIt) {
        limiter.process(lp, mixL.data(), mixR.data(), frames);
        for (int i = 0; i < frames; ++i) {
          const float a = mixL[static_cast<std::size_t>(i)], b = mixR[static_cast<std::size_t>(i)];
          out[0][i] = nch == 2 ? a : 0.5f * (a + b);
          if (nch == 2) out[1][i] = b;
        }
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

    renderWithEvents(*pb, tpf, frames);

    // Output stage: generated playback (times the level-match gain), plus optionally the current input; all gains are
    // smoothed per sample so the result does not depend on the host block size. The limiter, when on, comes last.
    const double dB = std::isfinite(params.outputGainDb) ? std::min(24.0, std::max(-96.0, static_cast<double>(params.outputGainDb))) : 0.0;
    const double targetGain = std::pow(10.0, dB / 20.0);
    const double targetDry = std::isfinite(params.dryMix) ? std::min(1.0, std::max(0.0, static_cast<double>(params.dryMix))) : 0.0;
    const float mdb = std::isfinite(params.matchGainDb) ? std::min(24.f, std::max(-24.f, params.matchGainDb)) : 0.f;
    const double targetMatch = mdb == 0.f ? 1.0 : std::pow(10.0, static_cast<double>(mdb) / 20.0);
    if (!gainsPrimed) {
      outGain = targetGain;
      dry = targetDry;
      matchGain = matchTarget = targetMatch;
      matchRampLeft = 0;
      gainsPrimed = true;
    } else if (targetMatch != matchTarget) {
      matchTarget = targetMatch;
      matchRampLeft = std::max(1, static_cast<int>(std::lround(0.05 * cfg.sampleRate)));  // 50 ms linear ramp per change
      matchStep = (targetMatch - matchGain) / static_cast<double>(matchRampLeft);
    }
    const bool limitIt = params.limiterEnabled || limiter.active();
    // The limiter delays main by its look-ahead; the raw aux buses get the same delay so stems stay aligned with main.
    const bool delayAux = limitIt && activeAux > 0 && auxDelayLen > 0;
    if (delayAux) auxDelayActive = true;
    else resetAuxDelay();
    const float* in0 = (in && in[0]) ? in[0] : nullptr;
    const float* in1 = (nch > 1 && in && in[1]) ? in[1] : in0;
    for (int i = 0; i < frames; ++i) {
      outGain += (targetGain - outGain) * smoothCoef;
      dry += (targetDry - dry) * smoothCoef;
      if (std::fabs(targetGain - outGain) < 1e-7) outGain = targetGain;
      if (std::fabs(targetDry - dry) < 1e-7) dry = targetDry;
      if (matchRampLeft > 0) {
        matchGain += matchStep;
        if (--matchRampLeft == 0) matchGain = matchTarget;
      }
      const float x0 = in0 ? in0[i] : 0.f;
      const float x1 = in1 ? in1[i] : 0.f;
      if (activeAux > 0) gainRamp[static_cast<std::size_t>(i)] = outGain;
      float l = mixL[static_cast<std::size_t>(i)];
      float r = mixR[static_cast<std::size_t>(i)];
      if (matchGain != 1.0) {  // the generated branch only; skipped entirely at 0 dB so the plain path has no extra multiply
        l = static_cast<float>(l * matchGain);
        r = static_cast<float>(r * matchGain);
      }
      if (limitIt) {
        float s0 = static_cast<float>((l + dry * x0) * outGain);
        float s1 = static_cast<float>((r + dry * (nch == 2 ? x1 : x0)) * outGain);
        if (!std::isfinite(s0)) s0 = 0.f;
        if (!std::isfinite(s1)) s1 = 0.f;
        mixL[static_cast<std::size_t>(i)] = s0;
        mixR[static_cast<std::size_t>(i)] = s1;
        continue;
      }
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
    if (activeAux > 0) writeAux(aux, frames, delayAux);
    if (limitIt) {
      limiter.process(lp, mixL.data(), mixR.data(), frames);
      for (int i = 0; i < frames; ++i) {
        const float a = mixL[static_cast<std::size_t>(i)], b = mixR[static_cast<std::size_t>(i)];
        float o0 = nch == 2 ? a : 0.5f * (a + b);
        float o1 = b;
        if (!std::isfinite(o0) || std::fabs(o0) < 1e-30f) o0 = 0.f;
        if (!std::isfinite(o1) || std::fabs(o1) < 1e-30f) o1 = 0.f;
        out[0][i] = o0;
        if (nch == 2) out[1][i] = o1;
      }
    }
  }

  OutputMeter meter;

  // The public entry points call this: the block, then one pass over the finished output for the meter.
  void processMetered(const TransportBlock& tb, const RenderParams& params, const float* const* in, float* const* out, int nch, int frames,
                      const LiveInput& live, const AuxOutputs& aux) {
    process(tb, params, in, out, nch, frames, live, aux);
    if (frames <= 0 || nch < 1 || nch > 2 || !out || !out[0] || (nch == 2 && !out[1])) return;
    int n = 0;
    for (const Voice& v : voices)
      if (v.active) ++n;
    meter.push(out, nch, frames, n);
  }

  void process(const TransportBlock& tb, const RenderParams& params, const float* const* in, float* const* out, int nch, int frames, const LiveInput& live,
               const AuxOutputs& aux) {
    if (frames <= 0 || nch < 1 || nch > 2 || !out || !out[0] || (nch == 2 && !out[1])) return;
    if (!prepared) {
      passthrough(in, out, nch, frames);
      for (int b = 0; b < std::min(aux.count, kMaxAuxBuses); ++b)
        for (int c = 0; c < 2; ++c)
          if (aux.bus[b] && aux.bus[b][c]) std::fill(aux.bus[b][c], aux.bus[b][c] + frames, 0.f);
      return;
    }
    padParams = live.padParams;
    padEvents = live.pads;
    numPadEvents = (live.pads && live.numPads > 0) ? std::min(live.numPads, kMaxPadEventsPerBlock) : 0;
    if (live.pads && live.numPads > kMaxPadEventsPerBlock) padDroppedCount.fetch_add(static_cast<std::uint64_t>(live.numPads - kMaxPadEventsPerBlock), std::memory_order_relaxed);
    padOffset = 0;
    processBlocks(tb, params, in, out, nch, frames, aux);
    int pads = 0;
    for (const Voice& v : voices)
      if (v.active && v.tag == kTagPad) ++pads;
    padVoices.store(pads, std::memory_order_relaxed);
    padEvents = nullptr;
    numPadEvents = 0;
  }

  void processBlocks(const TransportBlock& tb, const RenderParams& params, const float* const* in, float* const* out, int nch, int frames,
                     const AuxOutputs& aux) {
    if (frames <= cfg.maxBlock) {
      processArmed(tb, params, in, out, nch, frames, aux);
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
      padOffset = done;
      AuxOutputs chunkAux;
      float* chunkPtrs[kMaxAuxBuses][2] = {};
      chunkAux.count = std::max(0, std::min(aux.count, kMaxAuxBuses));
      for (int b = 0; b < chunkAux.count; ++b) {
        for (int c = 0; c < 2; ++c) chunkPtrs[b][c] = (aux.bus[b] && aux.bus[b][c]) ? aux.bus[b][c] + done : nullptr;
        chunkAux.bus[b] = chunkPtrs[b];
      }
      TransportBlock t2 = tb;
      if (tb.positionValid && std::isfinite(tb.ppq)) t2.ppq = tb.ppq + static_cast<double>(done) * bpm / (60.0 * cfg.sampleRate);
      processArmed(t2, params, inp, outp, nch, n, chunkAux);
    }
  }
};

Renderer::Renderer() : impl_(new Impl) {}
Renderer::~Renderer() = default;

void Renderer::prepare(const Config& config) { impl_->prepare(config); }
Mailbox<Playback>& Renderer::mailbox() { return impl_->mailbox; }
Mailbox<ArmedSwitch>& Renderer::armMailbox() { return impl_->arm.mailbox; }
std::uint64_t Renderer::appliedSwitchToken() const { return impl_->arm.applied(); }

void Renderer::process(const TransportBlock& transport, const RenderParams& params, const float* const* in, float* const* out,
                       int numChannels, int frames) {
  impl_->processMetered(transport, params, in, out, numChannels, frames, LiveInput{}, AuxOutputs{});
}

void Renderer::process(const TransportBlock& transport, const RenderParams& params, const float* const* in, float* const* out,
                       int numChannels, int frames, const LiveInput& live) {
  impl_->processMetered(transport, params, in, out, numChannels, frames, live, AuxOutputs{});
}

void Renderer::process(const TransportBlock& transport, const RenderParams& params, const float* const* in, float* const* out,
                       int numChannels, int frames, const AuxOutputs& aux) {
  impl_->processMetered(transport, params, in, out, numChannels, frames, LiveInput{}, aux);
}

void Renderer::process(const TransportBlock& transport, const RenderParams& params, const float* const* in, float* const* out,
                       int numChannels, int frames, const LiveInput& live, const AuxOutputs& aux) {
  impl_->processMetered(transport, params, in, out, numChannels, frames, live, aux);
}

Mailbox<PadBank>& Renderer::padMailbox() { return impl_->padBank; }
std::uint64_t Renderer::padDropped() const { return impl_->padDroppedCount.load(std::memory_order_relaxed); }
std::uint64_t Renderer::padUnmapped() const { return impl_->padUnmappedCount.load(std::memory_order_relaxed); }
int Renderer::activePadVoices() const { return impl_->padVoices.load(std::memory_order_relaxed); }

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
  impl_->limiter.reset();
  impl_->wasPlaying = false;
  impl_->haveExpected = false;
}

int Renderer::activeVoices() const {
  int n = 0;
  for (const Voice& v : impl_->voices)
    if (v.active) ++n;
  return n;
}

int Renderer::latencyFrames() const { return impl_->latency.load(std::memory_order_relaxed); }
void Renderer::primeLimiter() { impl_->limiter.prime(); }

OutputMeter& Renderer::meter() { return impl_->meter; }

LimiterTelemetry& Renderer::limiterTelemetry() { return impl_->limiter.telemetry(); }

std::uint64_t Renderer::droppedTriggers() const { return impl_->dropped; }

namespace {
Result<OfflineStems> renderStems(std::shared_ptr<const Playback> playback, const OfflineSettings& s, int auxBuses, const ProgressFn& progress) {
  if (auxBuses < 0 || auxBuses > kMaxAuxBuses) return makeError(ErrorCode::OutOfRange, "invalid number of aux buses");
  if (!playback || !playback->source) return makeError(ErrorCode::InvalidArgument, "no playback to render");
  if (!(s.sampleRate >= 8000.0 && s.sampleRate <= 384000.0)) return makeError(ErrorCode::OutOfRange, "invalid sample rate");
  if (!(s.bpm >= kMinBpm && s.bpm <= kMaxBpm)) return makeError(ErrorCode::OutOfRange, "invalid tempo");
  if (s.blockSize < 1 || s.blockSize > 16384 || s.cycles < 1 || s.cycles > 64 || !(s.tailSeconds >= 0.0 && s.tailSeconds <= 30.0))
    return makeError(ErrorCode::OutOfRange, "invalid render settings");

  Renderer r;
  Config cfg;
  cfg.sampleRate = s.sampleRate;
  cfg.maxBlock = s.blockSize;
  cfg.auxBuses = auxBuses;
  r.prepare(cfg);
  r.mailbox().publish(playback);

  const double tpf = s.bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * s.sampleRate);
  const std::int64_t mainFrames = static_cast<std::int64_t>(std::ceil(static_cast<double>(s.cycles) * static_cast<double>(playback->lengthTicks) / tpf - 1e-6));  // 1e-6: an exact multiple must not gain a frame
  const std::int64_t tailFrames = static_cast<std::int64_t>(std::ceil(s.tailSeconds * s.sampleRate));
  const std::int64_t total = mainFrames + tailFrames;
  if (total > static_cast<std::int64_t>(s.sampleRate) * 3600) return makeError(ErrorCode::LimitExceeded, "render would exceed one hour");
  // The limiter delays the signal by its latency. An export is latency-compensated: render that many extra frames at the
  // end (nothing new is scheduled there) and drop the same number from the start, so the length is unchanged.
  const std::int64_t lat = (s.params.limiterEnabled && s.params.enabled) ? Limiter::latencyFor(s.sampleRate) : 0;

  std::vector<std::vector<float>> out(2, std::vector<float>(static_cast<std::size_t>(total + lat), 0.f));
  std::vector<std::vector<std::vector<float>>> auxOut(static_cast<std::size_t>(auxBuses), std::vector<std::vector<float>>(2, std::vector<float>(static_cast<std::size_t>(total + lat), 0.f)));
  std::vector<float> silence(static_cast<std::size_t>(s.blockSize), 0.f);
  // Two phases so that no block ever straddles the end of the pattern: the output must not depend on the
  // block size. Phase 1 schedules events; phase 2 lets sounding voices ring out without starting new ones.
  const std::int64_t totalBlocks = (mainFrames + s.blockSize - 1) / s.blockSize + (tailFrames + s.blockSize - 1) / s.blockSize;
  std::int64_t blocksDone = 0;
  bool aborted = false;
  auto renderRange = [&](std::int64_t from, std::int64_t to, bool schedule) {
    for (std::int64_t pos = from; pos < to && !aborted; pos += s.blockSize) {
      const int n = static_cast<int>(std::min<std::int64_t>(s.blockSize, to - pos));
      TransportBlock tb;
      tb.playing = true;
      tb.positionValid = true;
      tb.bpm = s.bpm;
      tb.ppq = static_cast<double>(pos) * s.bpm / (60.0 * s.sampleRate);
      tb.scheduleEvents = schedule;
      const float* in[2] = {silence.data(), silence.data()};
      float* o[2] = {out[0].data() + pos, out[1].data() + pos};
      AuxOutputs aux;
      float* ap[kMaxAuxBuses][2] = {};
      aux.count = auxBuses;
      for (int b = 0; b < auxBuses; ++b) {
        ap[b][0] = auxOut[static_cast<std::size_t>(b)][0].data() + pos;
        ap[b][1] = auxOut[static_cast<std::size_t>(b)][1].data() + pos;
        aux.bus[b] = ap[b];
      }
      r.process(tb, s.params, in, o, 2, n, aux);
      if (progress && ++blocksDone % 64 == 0 && !progress(std::min(0.999, static_cast<double>(blocksDone) / static_cast<double>(std::max<std::int64_t>(1, totalBlocks)))))
        aborted = true;
    }
  };
  if (lat > 0) r.primeLimiter();  // the export starts as if the limiter had been running on the silence before it
  renderRange(0, mainFrames, true);
  renderRange(mainFrames, total + lat, false);
  if (!aborted && progress && !progress(1.0)) aborted = true;
  if (aborted) return makeError(ErrorCode::Cancelled, "export cancelled");
  if (lat > 0) {
    auto trim = [&](std::vector<float>& ch) {
      ch.erase(ch.begin(), ch.begin() + static_cast<std::ptrdiff_t>(lat));
      ch.resize(static_cast<std::size_t>(total));
    };
    for (auto& ch : out) trim(ch);
    for (auto& bus : auxOut)
      for (auto& ch : bus) trim(ch);
  }
  OfflineStems result;
  result.main = std::move(out);
  result.aux = std::move(auxOut);
  return result;
}
}  // namespace

Result<OfflineStems> renderOfflineStems(std::shared_ptr<const Playback> playback, const OfflineSettings& s, int auxBuses, const ProgressFn& progress) {
  return renderStems(std::move(playback), s, auxBuses, progress);
}

Result<std::vector<std::vector<float>>> renderOffline(std::shared_ptr<const Playback> playback, const OfflineSettings& s, const ProgressFn& progress) {
  auto stems = renderStems(std::move(playback), s, 0, progress);  // no aux buses: every event folds into main
  if (!stems.ok()) return stems.error();
  return std::move(stems.value().main);
}

}  // namespace chopfractal::render
