#include <chopfractal/chop_contracts/event.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include <chopfractal/chop_contracts/rng.hpp>

namespace chopfractal {
namespace {

double probabilityDraw(std::uint64_t seed, EventId id) {
  return static_cast<double>(hashCombine(seed, id.value) >> 11) * (1.0 / 9007199254740992.0);
}

std::string idText(EventId id) { return std::to_string(id.value); }

// id 8 + chop 8 + region 16 + start 8 + duration 8 + level/pan/pitch 12 + reverse/retrigger 2 +
// probability 4 + flags 1 + hasChild 1: the smallest serialized event, used to bound allocations.
constexpr std::size_t kMinEventBytes = 68;

}  // namespace

EchoFx sanitize(EchoFx fx) {
  fx.repeats = std::min<std::uint8_t>(fx.repeats, kEchoMaxRepeats);
  if (fx.rateIndex > 3 && fx.rateIndex != 255) fx.rateIndex = 255;
  if (fx.level != 255) {
    if (fx.level > 228) fx.level = 228;
    else if (fx.level > 100 && fx.level < 128) fx.level = 100;
  }
  return fx;
}

TremPanFx sanitize(TremPanFx fx) {
  if (fx.mode > 2 && fx.mode != 254 && fx.mode != 255) fx.mode = 255;
  if (fx.rateIndex > 3 && fx.rateIndex != 255) fx.rateIndex = 255;
  if (fx.depth != 255) {
    if (fx.depth > 228) fx.depth = 228;
    else if (fx.depth > 100 && fx.depth < 128) fx.depth = 100;
  }
  return fx;
}

StutterFx sanitize(StutterFx fx) {
  fx.rate = std::min<std::uint8_t>(fx.rate, kStutterDivisions - 1);
  fx.rateEnd = std::min<std::uint8_t>(fx.rateEnd, kStutterDivisions - 1);
  fx.gate = std::min<std::uint8_t>(std::max<std::uint8_t>(fx.gate, 10), 100);
  fx.tail = std::min<std::uint8_t>(fx.tail, 100);
  fx.probability = std::min<std::uint8_t>(fx.probability, 100);
  fx.pan = std::min<std::uint8_t>(fx.pan, 100);
  if (fx.pitch < -120) fx.pitch = -120;
  if (fx.pitch > 120) fx.pitch = 120;
  return fx;
}

RollFx sanitize(RollFx fx) {
  fx.rate = std::min<std::uint8_t>(fx.rate, kStutterDivisions - 1);
  fx.rateEnd = std::min<std::uint8_t>(fx.rateEnd, kStutterDivisions - 1);
  fx.velStart = std::min<std::uint8_t>(fx.velStart, 100);
  fx.velEnd = std::min<std::uint8_t>(fx.velEnd, 100);
  fx.freeze = std::min<std::uint8_t>(fx.freeze, 100);
  fx.flags &= static_cast<std::uint8_t>(kRollPalindrome | kRollSmooth);
  if (fx.pitch < -120) fx.pitch = -120;
  if (fx.pitch > 120) fx.pitch = 120;
  return fx;
}

Status flattenInto(FlatEventList& out, const std::vector<Event>& events, Ticks windowStart, Ticks windowDuration,
                   SampleRange ancestor, const ChopSnapshot& chops, const FlattenLimits& lim,
                   std::uint64_t probabilitySeed, int depth) {
  for (const Event& e : events) {
    if (!e.enabled) continue;
    if (e.duration <= 0 || e.start < 0 || e.duration > windowDuration || e.start > windowDuration - e.duration)
      return makeError(ErrorCode::OutOfRange, "event " + idText(e.id) + " lies outside its window");
    const ChopInfo* chop = chops.find(e.chop);
    if (!chop) return makeError(ErrorCode::NotFound, "event " + idText(e.id) + " references an unknown chop");
    const SampleRange requested = e.region.empty() ? chop->range : e.region;
    const SampleRange bound = (e.sourceOverride || ancestor.empty()) ? chop->range : ancestor;
    if (requested.empty() || !bound.contains(requested))
      return makeError(ErrorCode::OutOfRange, "event " + idText(e.id) + " reads outside its ancestor source range");
    if (e.probability < 1.f && !(probabilityDraw(probabilitySeed, e.id) < static_cast<double>(e.probability))) continue;

    if (e.child && e.childActive) {
      if (depth + 1 > lim.maxDepth) return makeError(ErrorCode::LimitExceeded, "nesting depth limit exceeded", lim.maxDepth);
      if (e.child->windowDuration != e.duration)
        return makeError(ErrorCode::Corrupt, "child window of event " + idText(e.id) + " differs from parent duration");
      Status s = flattenInto(out, e.child->events, windowStart + e.start, e.duration, requested, chops, lim,
                             probabilitySeed, depth + 1);
      if (!s.ok()) return s;
      continue;
    }
    if (out.size() >= lim.maxEvents)
      return makeError(ErrorCode::LimitExceeded, "flattened event limit exceeded", static_cast<std::int64_t>(lim.maxEvents));
    FlatEvent f;
    f.id = e.id;
    f.chop = e.chop;
    f.region = requested;
    f.start = windowStart + e.start;
    f.duration = e.duration;
    f.tx = e.tx;
    f.fx = e.fx;
    f.echo = e.echo;
    f.tremPan = e.tremPan;
    f.stutter = e.stutter;
    f.roll = e.roll;
    f.fadeInFrames = chop->fadeInFrames;
    f.fadeOutFrames = chop->fadeOutFrames;
    f.depth = static_cast<std::uint8_t>(depth);
    out.push_back(f);
  }
  return {};
}

void sortFlat(FlatEventList& list) {
  std::stable_sort(list.begin(), list.end(), [](const FlatEvent& a, const FlatEvent& b) {
    if (a.start != b.start) return a.start < b.start;
    return a.id < b.id;
  });
}

Status validateEvent(const Event& e, int depth) {
  const std::string who = "event " + idText(e.id);
  if (!e.id.valid() || !e.chop.valid()) return makeError(ErrorCode::InvalidArgument, who + " has an invalid id");
  constexpr Ticks kMaxTick = Ticks{1} << 40;  // far beyond any real pattern; keeps start + duration free of overflow
  if (e.start < 0 || e.duration <= 0 || e.start > kMaxTick || e.duration > kMaxTick) return makeError(ErrorCode::OutOfRange, who + " has invalid timing");
  if (!e.region.empty() && e.region.start < 0) return makeError(ErrorCode::OutOfRange, who + " has an invalid region");
  const EventTransform& t = e.tx;
  if (!(t.level >= 0.f && t.level <= limits::kMaxLevel)) return makeError(ErrorCode::OutOfRange, who + " level out of range");
  if (!(t.pan >= -1.f && t.pan <= 1.f)) return makeError(ErrorCode::OutOfRange, who + " pan out of range");
  if (!(std::fabs(t.pitchSemitones) <= limits::kMaxPitchSemitones)) return makeError(ErrorCode::OutOfRange, who + " pitch out of range");
  if (t.retrigger < 1 || t.retrigger > limits::kMaxRetrigger) return makeError(ErrorCode::OutOfRange, who + " retrigger out of range");
  const EventFx& fx = e.fx;
  if (static_cast<std::uint8_t>(fx.filter) > 2) return makeError(ErrorCode::OutOfRange, who + " filter type out of range");
  if (!(fx.cutoff >= 0.f && fx.cutoff <= 1.f) || !(fx.resonance >= 0.f && fx.resonance <= 1.f) ||
      !(fx.crush >= 0.f && fx.crush <= 1.f) || !(std::fabs(fx.glideSemitones) <= limits::kMaxGlideSemitones))
    return makeError(ErrorCode::OutOfRange, who + " effect out of range");
  if (!(e.probability >= 0.f && e.probability <= 1.f)) return makeError(ErrorCode::OutOfRange, who + " probability out of range");
  if ((e.echo && !(*e.echo == sanitize(*e.echo))) || (e.tremPan && !(*e.tremPan == sanitize(*e.tremPan))) ||
      (e.stutter && !(*e.stutter == sanitize(*e.stutter))) || (e.roll && !(*e.roll == sanitize(*e.roll))))
    return makeError(ErrorCode::OutOfRange, who + " has an out-of-range gesture effect");
  if (e.child) {
    if (depth + 1 > limits::kMaxNestedDepth) return makeError(ErrorCode::LimitExceeded, who + " nests too deeply", limits::kMaxNestedDepth);
    if (e.child->windowDuration != e.duration) return makeError(ErrorCode::Corrupt, who + " child window mismatch");
    if (e.child->events.size() > limits::kMaxEventsPerPattern) return makeError(ErrorCode::LimitExceeded, who + " has too many child events");
    for (const Event& c : e.child->events) {
      Status s = validateEvent(c, depth + 1);
      if (!s.ok()) return s;
    }
  }
  return {};
}

void writeEvent(bytes::Writer& w, const Event& e) {
  w.u64(e.id.value);
  w.u64(e.chop.value);
  w.i64(e.region.start);
  w.i64(e.region.end);
  w.i64(e.start);
  w.i64(e.duration);
  w.f32(e.tx.level);
  w.f32(e.tx.pan);
  w.f32(e.tx.pitchSemitones);
  w.u8(e.tx.reverse ? 1 : 0);
  w.u8(e.tx.retrigger);
  w.f32(e.probability);
  const std::uint8_t flags = static_cast<std::uint8_t>((e.enabled ? 1 : 0) | (e.locked ? 2 : 0) | (e.userOwned ? 4 : 0) |
                                                       (e.sourceOverride ? 8 : 0) | (e.childActive ? 16 : 0) |
                                                       (e.fx.active() ? 32 : 0) | (e.echo ? kEventHasEcho : 0) | (e.tremPan ? kEventHasTremPan : 0));
  w.u8(flags);
  w.u8(static_cast<std::uint8_t>((e.child ? kExtHasChild : 0) | (e.stutter ? kExtHasStutter : 0) | (e.roll ? kExtHasRoll : 0)));
  if (e.fx.active()) {
    w.u8(static_cast<std::uint8_t>(e.fx.filter));
    w.f32(e.fx.cutoff);
    w.f32(e.fx.resonance);
    w.f32(e.fx.glideSemitones);
    w.f32(e.fx.crush);
  }
  if (e.echo) {
    w.u8(e.echo->repeats);
    w.u8(e.echo->rateIndex);
    w.u8(e.echo->level);
  }
  if (e.tremPan) {
    w.u8(e.tremPan->mode);
    w.u8(e.tremPan->rateIndex);
    w.u8(e.tremPan->depth);
  }
  if (e.stutter) {
    const StutterFx& f = *e.stutter;
    w.u8(f.rate);
    w.u8(f.rateEnd);
    w.u8(f.gate);
    w.u8(f.tail);
    w.u8(static_cast<std::uint8_t>(f.decay));
    w.u8(static_cast<std::uint8_t>(f.pitch));
    w.u8(f.probability);
    w.u8(f.pan);
  }
  if (e.roll) {
    const RollFx& f = *e.roll;
    w.u8(f.rate);
    w.u8(f.rateEnd);
    w.u8(f.velStart);
    w.u8(f.velEnd);
    w.u8(f.freeze);
    w.u8(f.flags);
    w.u8(static_cast<std::uint8_t>(f.pitch));
  }
  if (e.child) {
    w.u64(e.child->seed);
    w.i64(e.child->windowDuration);
    w.u8(e.child->depth);
    w.boolean(e.child->locked);
    w.u32(static_cast<std::uint32_t>(e.child->events.size()));
    for (const Event& c : e.child->events) writeEvent(w, c);
  }
}

bool readEvent(bytes::Reader& r, Event& out, int depth) {
  out.id = EventId{r.u64()};
  out.chop = ChopId{r.u64()};
  out.region.start = r.i64();
  out.region.end = r.i64();
  out.start = r.i64();
  out.duration = r.i64();
  out.tx.level = r.f32();
  out.tx.pan = r.f32();
  out.tx.pitchSemitones = r.f32();
  out.tx.reverse = r.u8() != 0;
  out.tx.retrigger = r.u8();
  out.probability = r.f32();
  const std::uint8_t flags = r.u8();
  out.enabled = (flags & 1) != 0;
  out.locked = (flags & 2) != 0;
  out.userOwned = (flags & 4) != 0;
  out.sourceOverride = (flags & 8) != 0;
  out.childActive = (flags & 16) != 0;
  const std::uint8_t ext = r.u8();
  if (ext & ~kExtKnownBits) {  // an extension bit this build does not know: refuse rather than misread the rest
    r.fail();
    return false;
  }
  const bool hasChild = (ext & kExtHasChild) != 0;
  out.fx = EventFx{};
  out.echo.reset();
  out.tremPan.reset();
  out.stutter.reset();
  out.roll.reset();
  if (flags & 32) {
    const std::uint8_t ft = r.u8();
    if (ft > 2) {
      r.fail();
      return false;
    }
    out.fx.filter = static_cast<FilterType>(ft);
    out.fx.cutoff = r.f32();
    out.fx.resonance = r.f32();
    out.fx.glideSemitones = r.f32();
    out.fx.crush = r.f32();
  }
  if (flags & kEventHasEcho) {
    EchoFx f;
    f.repeats = r.u8();
    f.rateIndex = r.u8();
    f.level = r.u8();
    out.echo = sanitize(f);
  }
  if (flags & kEventHasTremPan) {
    TremPanFx f;
    f.mode = r.u8();
    f.rateIndex = r.u8();
    f.depth = r.u8();
    out.tremPan = sanitize(f);
  }
  if (ext & kExtHasStutter) {
    StutterFx f;
    f.rate = r.u8();
    f.rateEnd = r.u8();
    f.gate = r.u8();
    f.tail = r.u8();
    f.decay = static_cast<std::int8_t>(r.u8());
    f.pitch = static_cast<std::int8_t>(r.u8());
    f.probability = r.u8();
    f.pan = r.u8();
    out.stutter = sanitize(f);
  }
  if (ext & kExtHasRoll) {
    RollFx f;
    f.rate = r.u8();
    f.rateEnd = r.u8();
    f.velStart = r.u8();
    f.velEnd = r.u8();
    f.freeze = r.u8();
    f.flags = r.u8();
    f.pitch = static_cast<std::int8_t>(r.u8());
    out.roll = sanitize(f);
  }
  if (!r.ok()) return false;
  if (hasChild) {
    if (depth + 1 > limits::kMaxNestedDepth) {
      r.fail();
      return false;
    }
    auto child = std::make_shared<NestedPattern>();
    child->seed = r.u64();
    child->windowDuration = r.i64();
    child->depth = r.u8();
    child->locked = r.boolean();
    const std::uint32_t n = r.count(static_cast<std::uint32_t>(limits::kMaxEventsPerPattern), kMinEventBytes);
    if (!r.ok()) return false;
    child->events.resize(n);
    for (std::uint32_t i = 0; i < n; ++i)
      if (!readEvent(r, child->events[i], depth + 1)) return false;
    out.child = std::move(child);
  } else {
    out.child.reset();
  }
  return r.ok();
}

}  // namespace chopfractal
