// makePlayback: validates an event list and prepares everything the audio thread needs, so process() has no cross-block
// state to build: retrigger sub-hits, the gesture effects (Echo repeats, Stutter and Roll repeats, tremolo parameters)
// expanded into extra prepared events, and the per-slice quality data (transient-aware start, class, level trim).
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/audio_renderer/slice_quality.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace chopfractal::render {
namespace {

constexpr float kRetriggerDecay = 0.8f;
constexpr std::size_t kMaxRepeatsPerEvent = 64;

bool finiteIn(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

// Uniform in [-1, 1), a pure function of its key (so jitter never depends on build order or platform libm).
float signedDraw(std::uint64_t key) {
  return static_cast<float>(static_cast<double>(mix64(key) >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0);
}
double unitDraw(std::uint64_t key) { return static_cast<double>(mix64(key) >> 11) * (1.0 / 9007199254740992.0); }

struct Item {
  FlatEvent e;
  PreparedSlice s;
};

struct Rep {
  Ticks t;     // offset inside the event
  Ticks slot;  // length of this repeat's slot
};

// Repeat onsets across `duration`: stepped (one division per stage, ending on the grid) or smooth geometric
// acceleration from the first to the last division.
void walkRates(Ticks duration, int a, int b, bool smooth, std::vector<Rep>& out) {
  const int steps = std::abs(b - a) + 1;
  const int dir = b >= a ? 1 : -1;
  Ticks t = 0;
  while (t < duration && out.size() < kMaxRepeatsPerEvent) {
    Ticks slot;
    if (smooth && steps > 1) {
      const double s0 = static_cast<double>(kStutterDivisionTicks[a]), s1 = static_cast<double>(kStutterDivisionTicks[b]);
      slot = static_cast<Ticks>(std::llround(s0 * std::pow(s1 / s0, static_cast<double>(t) / static_cast<double>(duration))));
    } else {
      const int stage = std::min<int>(steps - 1, static_cast<int>(t * steps / duration));
      slot = kStutterDivisionTicks[a + dir * stage];
    }
    slot = std::max<Ticks>(slot, 20);
    out.push_back({t, slot});
    t += slot;
  }
}

void applyTremPan(PreparedSlice& s, const std::optional<TremPanFx>& t, const PlaybackOptions& o, std::size_t index) {
  if (!t || t->mode == 254) return;
  const std::uint8_t mode = t->mode == 255 ? o.tremMode : t->mode;
  const std::uint8_t rate = std::min<std::uint8_t>(t->rateIndex == 255 ? o.tremRate : t->rateIndex, 3);
  const int depth = t->depth == 255 ? o.tremDepth : (t->depth >= 128 ? t->depth - 128 : t->depth);
  if (mode > 2) return;
  s.tremMode = mode;
  s.tremDepth = static_cast<float>(std::min(100, std::max(0, depth))) / 100.f;
  s.tremPeriodTicks = kTremPeriodTicks[rate];
  s.panDir = (index & 1u) ? -1 : 1;
}

}  // namespace

Result<std::shared_ptr<const Playback>> makePlayback(SourcePtr source, const FlatEventList& events, Ticks lengthTicks,
                                                     const PlaybackOptions& options) {
  if (!source || source->frames <= 0 || (source->channels != 1 && source->channels != 2) || source->sampleRate < 8000 || source->sampleRate > 384000)
    return makeError(ErrorCode::InvalidArgument, "the source buffer is missing or has an unsupported format");
  if (source->samples.size() != static_cast<std::size_t>(source->channels) * static_cast<std::size_t>(source->frames))
    return makeError(ErrorCode::Corrupt, "the source buffer size does not match its channel and frame counts");
  // Bounds keep corrupt state from making the scheduler loop thousands of cycles per block or overflow tick math.
  constexpr Ticks kMinLength = 60;
  constexpr Ticks kMaxLength = static_cast<Ticks>(limits::kMaxBars) * 32 * kTicksPerWhole;
  if (lengthTicks < kMinLength || lengthTicks > kMaxLength) return makeError(ErrorCode::OutOfRange, "the pattern length is outside the supported range");
  if (events.size() > limits::kMaxEventsPerPattern) return makeError(ErrorCode::LimitExceeded, "too many events", static_cast<std::int64_t>(limits::kMaxEventsPerPattern));

  auto pb = std::make_shared<Playback>();
  pb->source = std::move(source);
  pb->lengthTicks = lengthTicks;

  // One analysis per distinct source region (transient-aware start, class, level), shared by every event that reads it.
  std::map<std::pair<std::int64_t, std::int64_t>, std::size_t> regionIndex;
  std::vector<SliceAnalysis> analyses;
  std::vector<std::size_t> eventAnalysis(events.size(), 0);

  for (std::size_t idx = 0; idx < events.size(); ++idx) {
    const FlatEvent& e = events[idx];
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
    if ((e.echo && !(*e.echo == sanitize(*e.echo))) || (e.tremPan && !(*e.tremPan == sanitize(*e.tremPan))) ||
        (e.stutter && !(*e.stutter == sanitize(*e.stutter))) || (e.roll && !(*e.roll == sanitize(*e.roll))))
      return makeError(ErrorCode::OutOfRange, "an event has an out-of-range gesture effect");
    const auto key = std::make_pair(e.region.start, e.region.end);
    auto it = regionIndex.find(key);
    if (it == regionIndex.end()) {
      it = regionIndex.emplace(key, analyses.size()).first;
      analyses.push_back(analyzeSlice(*pb->source, e.region));
    }
    eventAnalysis[idx] = it->second;
  }
  const std::vector<float> trims = matchTrims(analyses);

  std::vector<Item> items;
  items.reserve(events.size() * 2);

  // Pass 1: base events. Retrigger: n evenly spaced sub-hits across the event, each decaying; expanded here so the
  // audio thread needs no cross-block retrigger state.
  for (std::size_t idx = 0; idx < events.size(); ++idx) {
    const FlatEvent& e = events[idx];
    const SliceAnalysis& an = analyses[eventAnalysis[idx]];
    const int n = e.tx.retrigger;
    float level = e.tx.level;
    for (int k = 0; k < n; ++k) {
      Item it;
      it.e = e;
      it.e.tx.retrigger = 1;
      it.e.start = (e.start + e.duration * k / n) % lengthTicks;  // a sub-hit past the loop end wraps to the loop start
      it.e.duration = e.duration * (k + 1) / n - e.duration * k / n;
      it.e.tx.level = level;
      level *= kRetriggerDecay;
      if (it.e.duration <= 0) continue;
      it.s.regionStart = an.start;
      it.s.cls = an.cls;
      it.s.trim = trims[eventAnalysis[idx]];
      it.s.repeat = k > 0;
      it.s.replacedBy = e.stutter ? fxbit::kStutter : (e.roll ? fxbit::kRoll : 0);
      if (n >= 3 && k > 0) {  // research 3.3: fixed gain and pitch per repeat sounds like a machine gun
        it.s.gainJitter = 1.f + 0.024f * signedDraw(hashCombine(e.id.value, static_cast<std::uint64_t>(2 * k + 1)));
        it.s.pitchJitter = 0.3f * signedDraw(hashCombine(e.id.value, static_cast<std::uint64_t>(2 * k + 2)));
      }
      applyTremPan(it.s, e.tremPan, options, idx);
      items.push_back(it);
    }
  }
  if (items.size() > kMaxPreparedEvents) return makeError(ErrorCode::LimitExceeded, "too many events after retrigger expansion", static_cast<std::int64_t>(kMaxPreparedEvents));

  // Pass 2: gesture-effect repeats, appended while capacity remains (a repeat that does not fit is dropped, never an error).
  auto room = [&]() { return items.size() < kMaxPreparedEvents; };
  std::vector<Rep> reps;
  for (std::size_t idx = 0; idx < events.size(); ++idx) {
    const FlatEvent& e = events[idx];
    const SliceAnalysis& an = analyses[eventAnalysis[idx]];
    const float trim = trims[eventAnalysis[idx]];
    if (e.stutter) {
      const StutterFx& f = *e.stutter;
      reps.clear();
      walkRates(e.duration, f.rate, f.rateEnd, false, reps);
      const double decayDb = static_cast<double>(f.decay) / 10.0;
      for (std::size_t k = 0; k < reps.size() && room(); ++k) {
        if (k > 0 && f.probability < 100 && !(unitDraw(hashCombine(e.id.value ^ 0x5354554Full, k)) * 100.0 < static_cast<double>(f.probability))) continue;
        Item it;
        it.e = e;
        it.e.tx.retrigger = 1;
        it.e.stutter.reset();
        it.e.echo.reset();
        it.e.tremPan.reset();
        const Ticks slotLen = std::min(reps[k].slot, e.duration - reps[k].t);
        it.e.start = e.start + reps[k].t;
        it.e.duration = std::max<Ticks>(1, slotLen * f.gate / 100);
        double g = static_cast<double>(e.tx.level) * std::pow(10.0, decayDb * static_cast<double>(k) / 20.0);
        if (f.decay > 0) g = std::min(g, std::max(1.0, static_cast<double>(e.tx.level)));  // a crescendo stops at full level
        it.e.tx.level = static_cast<float>(std::min(g, static_cast<double>(limits::kMaxLevel)));
        it.s.regionStart = an.start;
        it.s.cls = an.cls;
        it.s.trim = trim;
        it.s.fxBit = fxbit::kStutter;
        it.s.repeat = true;
        it.s.tailFrac = static_cast<float>(f.tail) / 100.f;
        it.s.pitchOffset = reps.size() > 1 ? static_cast<float>(f.pitch) / 10.f * static_cast<float>(k) / static_cast<float>(reps.size() - 1) : 0.f;
        it.s.panOffset = (k % 2 == 0 ? -1.f : 1.f) * 0.8f * static_cast<float>(f.pan) / 100.f;
        if (k > 0) {
          it.s.gainJitter = 1.f + 0.024f * signedDraw(hashCombine(e.id.value ^ 0x5354ull, 2 * k + 1));
          it.s.pitchJitter = 0.3f * signedDraw(hashCombine(e.id.value ^ 0x5354ull, 2 * k + 2));
        }
        items.push_back(it);
      }
    } else if (e.roll) {
      const RollFx& f = *e.roll;
      reps.clear();
      walkRates(e.duration, f.rate, f.rateEnd, (f.flags & kRollSmooth) != 0, reps);
      const std::int64_t frozen = e.region.start + static_cast<std::int64_t>(static_cast<double>(e.region.length() - 1) * static_cast<double>(f.freeze) / 100.0);
      for (std::size_t k = 0; k < reps.size() && room(); ++k) {
        Item it;
        it.e = e;
        it.e.tx.retrigger = 1;
        it.e.roll.reset();
        it.e.echo.reset();
        it.e.tremPan.reset();
        const Ticks slotLen = std::min(reps[k].slot, e.duration - reps[k].t);
        it.e.start = e.start + reps[k].t;
        it.e.duration = std::max<Ticks>(1, slotLen);
        it.e.region.start = frozen;
        const double vel = reps.size() > 1 ? f.velStart + (static_cast<double>(f.velEnd) - f.velStart) * static_cast<double>(k) / static_cast<double>(reps.size() - 1)
                                           : static_cast<double>(f.velEnd);
        it.e.tx.level = static_cast<float>(std::min(static_cast<double>(e.tx.level) * vel / 100.0, static_cast<double>(limits::kMaxLevel)));
        if ((f.flags & kRollPalindrome) && (k % 2 == 1)) it.e.tx.reverse = !e.tx.reverse;
        it.s.regionStart = frozen;
        it.s.cls = an.cls;
        it.s.trim = trim;
        it.s.fxBit = fxbit::kRoll;
        it.s.repeat = true;
        it.s.freezeUnit = true;
        it.s.tailFrac = 0.12f;
        it.s.pitchOffset = reps.size() > 1 ? static_cast<float>(f.pitch) / 10.f * static_cast<float>(k) / static_cast<float>(reps.size() - 1) : 0.f;
        if (k > 0) {
          it.s.gainJitter = 1.f + 0.024f * signedDraw(hashCombine(e.id.value ^ 0x524Full, 2 * k + 1));
          it.s.pitchJitter = 0.3f * signedDraw(hashCombine(e.id.value ^ 0x524Full, 2 * k + 2));
        }
        items.push_back(it);
      }
    } else if (e.echo && e.echo->repeats > 0 && e.tx.retrigger == 1) {
      const EchoFx& f = *e.echo;
      const std::uint8_t rate = std::min<std::uint8_t>(f.rateIndex == 255 ? options.echoRate : f.rateIndex, 3);
      const int levelPct = f.level == 255 ? options.echoDecay : (f.level >= 128 ? f.level - 128 : f.level);
      const double decay = static_cast<double>(std::min(100, std::max(0, levelPct))) / 100.0;
      const double tone = static_cast<double>(std::min<int>(options.echoTone, 100)) / 100.0;
      // The primary's prepared base event is the template (retrigger 1, so exactly one base item exists for this id).
      for (int k = 1; k <= f.repeats && room(); ++k) {
        const Ticks start = e.start + static_cast<Ticks>(k) * kEchoSpacingTicks[rate];
        if (start >= lengthTicks) break;  // a repeat never wraps into the next loop
        Item it;
        it.e = e;
        it.e.echo.reset();
        it.e.start = start;
        it.e.tx.level = static_cast<float>(std::min(static_cast<double>(e.tx.level) * std::pow(decay, k), static_cast<double>(limits::kMaxLevel)));
        it.s.regionStart = an.start;
        it.s.cls = an.cls;
        it.s.trim = trim;
        it.s.fxBit = fxbit::kEcho;
        it.s.secondary = true;
        it.s.repeat = true;
        if (tone > 0.0) it.s.echoLpHz = static_cast<float>(std::max(400.0, 12000.0 * std::pow(1.0 - tone * 0.85, k)));
        applyTremPan(it.s, e.tremPan, options, idx);
        items.push_back(it);
      }
    }
  }

  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.e.start < b.e.start; });
  pb->events.reserve(items.size());
  pb->slices.reserve(items.size());
  for (Item& it : items) {
    pb->events.push_back(std::move(it.e));
    pb->slices.push_back(it.s);
  }
  return std::shared_ptr<const Playback>(std::move(pb));
}

Result<std::shared_ptr<const Playback>> makePassThroughPlayback(SourcePtr source) {
  auto base = makePlayback(std::move(source), {}, kTicksPerWhole);
  if (!base.ok()) return base.error();
  auto pb = std::make_shared<Playback>(*base.value());
  pb->passThrough = true;
  return std::shared_ptr<const Playback>(std::move(pb));
}

}  // namespace chopfractal::render
