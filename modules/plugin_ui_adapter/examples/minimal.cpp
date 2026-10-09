// Minimal consumer: build a pattern view model and an accessible description for each hit.
#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 48000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 24000}, 0, 0, 0});
  chops.chops.push_back({ChopId{2}, {24000, 48000}, 0, 0, 0});

  pattern::Settings settings;
  settings.bars = 1;
  settings.density = 1.0;
  auto p = pattern::generate(chops, settings);
  if (!p.ok()) return 1;

  const ui::PatternView view = ui::buildPatternView(p.value(), chops);
  if (view.rects.empty()) return 1;
  std::printf("%zu hits; first: %s\n", view.rects.size(), view.rects.front().description.c_str());
  return 0;
}
