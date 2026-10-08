#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/recursive_hit_zoom/zoom.hpp>

#include <algorithm>
#include <string>

namespace chopfractal::zoom {
namespace {

constexpr int kAllowed[] = {2, 3, 4, 5, 6, 8};

bool allowedSubdivision(int n) { return std::find(std::begin(kAllowed), std::end(kAllowed), n) != std::end(kAllowed); }

// Derived child ids live in the upper half of the id space so they cannot collide with the pattern
// engine's sequential counter. They depend only on (parent id, child index): stable across reseeds.
EventId childId(EventId parent, int index) {
  return EventId{hashCombine(parent.value, static_cast<std::uint64_t>(index) + 1) | (1ull << 63)};
}

double densityOf(const Settings& s) {
  if (s.densityValue >= 0.0) return s.densityValue;
  switch (s.density) {
    case Density::Sparse: return 0.4;
    case Density::Balanced: return 0.7;
    case Density::Dense: return 1.0;
  }
  return 0.7;
}

Status validate(const Event& parent, const Context& ctx, const Settings& s) {
  if (!allowedSubdivision(s.subdivisions)) return makeError(ErrorCode::InvalidArgument, "subdivision must be 2, 3, 4, 5, 6 or 8");
  if (s.densityValue >= 0.0 && !(s.densityValue <= 1.0)) return makeError(ErrorCode::OutOfRange, "density must lie in [0, 1]");
  if (s.densityValue != s.densityValue) return makeError(ErrorCode::OutOfRange, "density must be a number");
  if (s.shuffleTicks < 0) return makeError(ErrorCode::OutOfRange, "shuffle must not be negative");
  if (s.maxDepth < 1 || s.maxDepth > limits::kMaxNestedDepth) return makeError(ErrorCode::OutOfRange, "maximum depth must be 1 to 3", limits::kMaxNestedDepth);
  if (s.inherit == Inherit::Override) {
    Event probe = parent;
    probe.tx = s.overrideTx;
    Status v = validateEvent(probe);
    if (!v.ok()) return makeError(ErrorCode::OutOfRange, "override transform is out of range");
  }
  if (!parent.id.valid() || !parent.chop.valid() || !parent.enabled || parent.duration <= 0)
    return makeError(ErrorCode::InvalidArgument, "the parent event is not valid for zooming");
  if (ctx.parentDepth < 0 || ctx.parentDepth + 1 > s.maxDepth)
    return makeError(ErrorCode::LimitExceeded, "nesting depth limit reached", s.maxDepth);
  if (ctx.sourceBounds.empty() || ctx.sourceBounds.start < 0) return makeError(ErrorCode::InvalidArgument, "the parent's source bounds are invalid");
  if (parent.duration < static_cast<Ticks>(s.subdivisions) * kMinChildTicks)
    return makeError(ErrorCode::InvalidArgument, "the event is too short to subdivide that far");
  if (s.operation == Operation::SelectSubregion && ctx.sourceBounds.length() < s.subdivisions)
    return makeError(ErrorCode::InvalidArgument, "the source region is too short to split");
  return {};
}

EventTransform childTransform(const Event& parent, const Settings& s) {
  switch (s.inherit) {
    case Inherit::Inherit: return parent.tx;
    case Inherit::Reset: return EventTransform{};
    case Inherit::Override: {
      EventTransform t = parent.tx;
      if (s.overrideMask & kLevel) t.level = s.overrideTx.level;
      if (s.overrideMask & kPan) t.pan = s.overrideTx.pan;
      if (s.overrideMask & kPitch) t.pitchSemitones = s.overrideTx.pitchSemitones;
      if (s.overrideMask & kReverse) t.reverse = s.overrideTx.reverse;
      if (s.overrideMask & kRetrigger) t.retrigger = s.overrideTx.retrigger;
      return t;
    }
  }
  return parent.tx;
}

}  // namespace

int nearestPermittedSubdivision(std::size_t budget) {
  int best = 0;
  for (int n : kAllowed)
    if (static_cast<std::size_t>(n) <= budget) best = n;
  return best;
}

Result<std::shared_ptr<const NestedPattern>> createChildPattern(const Event& parent, const Context& ctx, const Settings& s,
                                                                std::uint64_t seed) {
  Status v = validate(parent, ctx, s);
  if (!v.ok()) return v.error();

  const int n = s.subdivisions;
  const Ticks window = parent.duration;
  Rng rng(deriveKey(seed, kModuleVersion, parent.id.value));
  const double p = densityOf(s);

  // Fixed number of draws per cell keeps every cell's outcome independent of the others.
  std::vector<bool> active(static_cast<std::size_t>(n));
  std::vector<std::uint64_t> shuffleDraw(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    active[static_cast<std::size_t>(i)] = rng.uniform01() < p;
    shuffleDraw[static_cast<std::size_t>(i)] = rng.next();
  }
  const std::uint64_t restDraw = rng.uniform(static_cast<std::uint64_t>(n - 1)) + 1;  // 1..n-1, never the first cell
  const std::uint64_t altOffset = rng.next();
  active[0] = true;  // the first cell always plays, so a zoomed hit is never silent
  if (s.operation == Operation::CreateRests) active[static_cast<std::size_t>(restDraw)] = false;

  std::size_t count = 0;
  for (bool a : active) count += a ? 1 : 0;
  if (count > ctx.eventBudget)
    return makeError(ErrorCode::LimitExceeded, "zooming would exceed the project event cap; try " +
                                                   std::to_string(nearestPermittedSubdivision(ctx.eventBudget)) + " subdivisions",
                     nearestPermittedSubdivision(ctx.eventBudget));

  auto child = std::make_shared<NestedPattern>();
  child->seed = seed;
  child->windowDuration = window;
  child->depth = static_cast<std::uint8_t>(ctx.parentDepth + 1);
  const EventTransform tx = childTransform(parent, s);
  const std::int64_t boundsLen = ctx.sourceBounds.length();

  for (int i = 0; i < n; ++i) {
    const std::size_t si = static_cast<std::size_t>(i);
    if (!active[si]) continue;
    const Ticks cellStart = window * i / n;
    const Ticks cellEnd = window * (i + 1) / n;
    Event c;
    c.id = childId(parent.id, i);
    c.chop = parent.chop;
    c.region = ctx.sourceBounds;
    c.duration = cellEnd - cellStart;
    c.start = cellStart;
    c.tx = tx;
    if (s.shuffleTicks > 0) {
      const Ticks bound = std::min<Ticks>(s.shuffleTicks, c.duration / 2);
      const Ticks delta = static_cast<Ticks>(shuffleDraw[si] % static_cast<std::uint64_t>(2 * bound + 1)) - bound;
      c.start = std::max<Ticks>(0, std::min<Ticks>(cellStart + delta, window - c.duration));
    }
    if (s.operation == Operation::SelectSubregion) {
      c.region = {ctx.sourceBounds.start + boundsLen * i / n, ctx.sourceBounds.start + boundsLen * (i + 1) / n};
    } else if (s.operation == Operation::AlternateChops && !ctx.compatible.empty()) {
      const ChopInfo& alt = ctx.compatible[(altOffset + si) % ctx.compatible.size()];
      c.chop = alt.id;
      if (alt.id != parent.chop) {
        c.sourceOverride = true;  // an explicit, user-chosen replacement of the source chop
        c.region = alt.range;
      }
    }
    child->events.push_back(std::move(c));
  }
  return std::shared_ptr<const NestedPattern>(std::move(child));
}

Result<std::shared_ptr<const NestedPattern>> mutateChildren(const Event& parentWithChild, const Context& ctx, const Settings& s,
                                                            std::uint64_t seed) {
  if (!parentWithChild.child) return makeError(ErrorCode::InvalidArgument, "the event has no child pattern to mutate");
  if (parentWithChild.child->locked) return makeError(ErrorCode::Blocked, "the child pattern is locked; unlock it first");
  // Depth is that of the parent, which the caller supplies in ctx.parentDepth.
  Event parent = parentWithChild;
  parent.child.reset();
  auto fresh = createChildPattern(parent, ctx, s, seed);
  if (!fresh.ok()) return fresh;
  auto merged = std::make_shared<NestedPattern>(*fresh.value());
  for (const Event& old : parentWithChild.child->events) {
    if (!old.locked) continue;
    auto it = std::find_if(merged->events.begin(), merged->events.end(), [&](const Event& e) { return e.id == old.id; });
    if (it != merged->events.end())
      *it = old;
    else
      merged->events.push_back(old);
  }
  if (merged->events.size() > ctx.eventBudget)
    return makeError(ErrorCode::LimitExceeded, "keeping locked child events would exceed the project event cap",
                     nearestPermittedSubdivision(ctx.eventBudget));
  std::stable_sort(merged->events.begin(), merged->events.end(), [](const Event& a, const Event& b) { return a.start < b.start; });
  return std::shared_ptr<const NestedPattern>(std::move(merged));
}

Event collapse(const Event& parent) {
  Event e = parent;
  e.child.reset();
  e.childActive = true;
  return e;
}

Result<FlatEventList> flatten(const Event& parent, Ticks windowStart, const ChopSnapshot& chops, const FlattenLimits& limits,
                              std::uint64_t probabilitySeed) {
  FlatEventList out;
  Status s = flattenInto(out, {parent}, windowStart, parent.start + parent.duration, {}, chops, limits, probabilitySeed);
  if (!s.ok()) return s.error();
  sortFlat(out);
  return out;
}

std::vector<std::uint8_t> serialize(const NestedPattern& child) {
  Event wrapper;
  wrapper.id = EventId{1};
  wrapper.chop = ChopId{1};
  wrapper.duration = child.windowDuration;
  wrapper.child = std::make_shared<NestedPattern>(child);
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  writeEvent(w, wrapper);
  return out;
}

Result<std::shared_ptr<const NestedPattern>> deserialize(const std::uint8_t* data, std::size_t size) {
  bytes::Reader r(data, size);
  Event wrapper;
  if (!readEvent(r, wrapper) || r.remaining() != 0 || !wrapper.child) return makeError(ErrorCode::Corrupt, "child pattern state is malformed");
  if (wrapper.child->windowDuration != wrapper.duration || wrapper.duration <= 0) return makeError(ErrorCode::Corrupt, "child window is invalid");
  for (const Event& e : wrapper.child->events) {
    Status v = validateEvent(e);
    if (!v.ok()) return makeError(ErrorCode::Corrupt, "child event is invalid: " + v.error().message);
    if (e.start + e.duration > wrapper.child->windowDuration) return makeError(ErrorCode::Corrupt, "child event leaves its window");
  }
  return wrapper.child;
}

}  // namespace chopfractal::zoom
