#pragma once
// Private helpers shared by the engine's translation units. Not part of the public API.
#include <chopfractal/pattern_engine/pattern.hpp>

#include <algorithm>
#include <unordered_set>

namespace chopfractal::pattern::detail {

inline Ticks beatTicksOf(const Settings& s) { return ticksPerBeat(s.timeSignature); }

// Metric strength of a position inside a bar: downbeat > beat > half-beat > off-grid.
inline double metricWeight(Ticks startInBar, Ticks beatT) {
  if (startInBar == 0) return 1.0;
  if (startInBar % beatT == 0) return 0.85;
  if (startInBar % (beatT / 2) == 0) return 0.7;
  return 0.55;
}

// Sets durations of the selected events to the gap before the next event (bounded by the bar end and
// four grid cells). `onlyThese` == nullptr means every event in the bar. Retained events are untouched.
inline void assignDurations(Pattern& p, int bar, const std::unordered_set<std::uint64_t>* onlyThese) {
  const Ticks barT = barTicks(p.settings);
  const Ticks g = gridTicks(p.settings.grid);
  std::vector<Event*> all;
  for (Beat& bt : p.bars[static_cast<std::size_t>(bar)].beats)
    for (Event& e : bt.events) all.push_back(&e);
  std::stable_sort(all.begin(), all.end(), [](const Event* a, const Event* b) {
    return a->start != b->start ? a->start < b->start : a->id < b->id;
  });
  for (std::size_t i = 0; i < all.size(); ++i) {
    Event& e = *all[i];
    if (onlyThese && onlyThese->count(e.id.value) == 0) continue;
    Ticks next = barT;
    for (std::size_t j = i + 1; j < all.size(); ++j)
      if (all[j]->start > e.start) {
        next = all[j]->start;
        break;
      }
    Ticks dur = std::min<Ticks>({next - e.start, barT - e.start, g * 4});
    e.duration = std::max<Ticks>(1, dur);
  }
}

}  // namespace chopfractal::pattern::detail
