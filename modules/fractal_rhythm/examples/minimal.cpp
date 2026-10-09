// Minimal consumer: generate a two-scale fractal bar over four chops and count the sounding hits.
#include <chopfractal/fractal_rhythm/fractal.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot snap;
  snap.sourceFrames = 4000;
  snap.sampleRate = 48000;
  for (int i = 0; i < 4; ++i) snap.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 1000, (i + 1) * 1000}, 0, 0, 0});
  fractal::Context ctx;
  ctx.chops = &snap;
  fractal::Settings s;
  s.motif = "x.xx";
  s.depth = 2;
  auto bar = fractal::generateBar(ctx, s);
  if (!bar.ok()) return 1;
  FlatEventList flat;
  Status st = flattenInto(flat, bar.value(), 0, ctx.barTicks, {}, snap, FlattenLimits{}, 1);
  if (!st.ok()) return 1;
  std::printf("%zu hits\n", flat.size());
  return flat.size() == 9 ? 0 : 1;
}
