// Minimal consumer: define chops, one event, flatten it. Uses only chop_contracts public headers.
#include <chopfractal/chop_contracts/event.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 48000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 24000}, 0, 0, 0});

  Event e;
  e.id = EventId{1};
  e.chop = ChopId{1};
  e.start = 0;
  e.duration = kTicksPerQuarter;

  FlatEventList flat;
  Status s = flattenInto(flat, {e}, 0, ticksPerBar(TimeSignature{}), {}, chops, {}, 1);
  if (!s.ok() || flat.size() != 1) return 1;
  std::printf("flattened %zu event(s)\n", flat.size());
  return 0;
}
