// Minimal consumer: zoom into one event and flatten the result for playback.
#include <chopfractal/recursive_hit_zoom/zoom.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 48000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 48000}, 0, 0, 0});

  Event hit;
  hit.id = EventId{1};
  hit.chop = ChopId{1};
  hit.start = 0;
  hit.duration = kTicksPerQuarter;

  zoom::Context ctx;
  ctx.sourceBounds = chops.chops[0].range;  // the parent's resolved source range
  zoom::Settings settings;
  settings.subdivisions = 4;
  settings.density = zoom::Density::Dense;

  auto child = zoom::createChildPattern(hit, ctx, settings, /*seed=*/42);
  if (!child.ok()) return 1;
  hit.child = child.value();
  auto flat = zoom::flatten(hit, /*windowStart=*/0, chops);
  if (!flat.ok() || flat.value().size() != 4) return 1;
  std::printf("zoomed into %zu events\n", flat.value().size());
  return 0;
}
