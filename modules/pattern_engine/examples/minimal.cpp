// Minimal consumer: generate a 2-bar pattern from two chops and flatten it for a scheduler.
#include <chopfractal/pattern_engine/pattern.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 96000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 48000}, 0, 0, 0});
  chops.chops.push_back({ChopId{2}, {48000, 96000}, 0, 0, 0});

  pattern::Settings settings;
  settings.bars = 2;
  settings.density = 0.6;
  settings.seed = 1234;  // same chops + settings + seed => identical pattern, always

  auto generated = pattern::generate(chops, settings);
  if (!generated.ok()) return 1;
  auto flat = pattern::flatten(generated.value(), chops);
  if (!flat.ok() || flat.value().empty()) return 1;
  std::printf("%zu events over %lld ticks\n", flat.value().size(), static_cast<long long>(pattern::lengthTicks(generated.value())));
  return 0;
}
