// Minimal consumer: wrap a decoded buffer, place markers, export the chop snapshot.
#include <chopfractal/source_chop/analysis.hpp>
#include <chopfractal/source_chop/chop_map.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::source;
  SourceInfo info;
  info.name = "loop";
  info.sampleRate = 48000;
  info.frames = 48000;
  auto map = ChopMap::create(info);
  if (!map.ok()) return 1;
  map.value().applyProposal(evenGrid(info.frames, {4.0, 4, GridKind::Straight}), MergeMode::Replace);
  const auto snap = map.value().snapshot();
  if (snap->chops.size() != 4) return 1;
  std::printf("%zu chops\n", snap->chops.size());
  return 0;
}
