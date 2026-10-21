#include <chopfractal/pattern_engine/pattern.hpp>

#include <memory>
#include <vector>

namespace chopfractal::pattern {
namespace {

constexpr int kMaxMapDepth = 16;

Status mapEvents(std::vector<Event>& events, const std::function<SampleRange(SampleRange)>& fn, int depth) {
  if (depth > kMaxMapDepth) return makeError(ErrorCode::LimitExceeded, "the zoom tree is too deep");
  for (Event& e : events) {
    if (!e.region.empty()) {
      const SampleRange r = fn(e.region);
      if (r.start < 0 || r.end < r.start) return makeError(ErrorCode::InvalidArgument, "the region mapping produced an invalid range");
      e.region = r;
    }
    if (e.child) {
      auto copy = std::make_shared<NestedPattern>(*e.child);
      Status s = mapEvents(copy->events, fn, depth + 1);
      if (!s.ok()) return s;
      e.child = std::move(copy);
    }
  }
  return {};
}

}  // namespace

Result<Pattern> mapRegions(const Pattern& p, const std::function<SampleRange(SampleRange)>& fn) {
  if (!fn) return makeError(ErrorCode::InvalidArgument, "no mapping function");
  Pattern out = p;
  for (Bar& bar : out.bars)
    for (Beat& beat : bar.beats) {
      Status s = mapEvents(beat.events, fn, 0);
      if (!s.ok()) return s.error();
    }
  return out;
}

}  // namespace chopfractal::pattern
