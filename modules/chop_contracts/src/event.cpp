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

Status flattenInto(FlatEventList& out, const std::vector<Event>& events, Ticks windowStart, Ticks windowDuration,
                   SampleRange ancestor, const ChopSnapshot& chops, const FlattenLimits& lim,
                   std::uint64_t probabilitySeed, int depth) {
  for (const Event& e : events) {
    if (!e.enabled) continue;
    if (e.duration <= 0 || e.start < 0 || e.start + e.duration > windowDuration)
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
  if (e.start < 0 || e.duration <= 0) return makeError(ErrorCode::OutOfRange, who + " has invalid timing");
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
                                                       (e.fx.active() ? 32 : 0));
  w.u8(flags);
  w.u8(e.child ? 1 : 0);
  if (e.fx.active()) {
    w.u8(static_cast<std::uint8_t>(e.fx.filter));
    w.f32(e.fx.cutoff);
    w.f32(e.fx.resonance);
    w.f32(e.fx.glideSemitones);
    w.f32(e.fx.crush);
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
  const bool hasChild = r.u8() != 0;
  out.fx = EventFx{};
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
