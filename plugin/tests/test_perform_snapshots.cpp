// With CHOPFRACTAL_PERFORM_SNAPS=<folder> every wave-two drawer is rendered at 1100x720 and 880x560 for a visual check.
#include <cstdlib>

#include "perform_test_support.hpp"

using namespace shelltest;

CHOP_TEST(perform_drawers_render_snapshots_when_asked) {
  const char* dir = std::getenv("CHOPFRACTAL_PERFORM_SNAPS");
  if (!dir || !haveDisplay()) return;
  const juce::File out(dir);
  out.createDirectory();
  const char* ids[] = {"capture", "tempo", "pads", "scenes", "evolve", "outputs", "arrange", "orbit"};
  for (const auto& size : {std::pair<int, int>{1100, 720}, std::pair<int, int>{880, 560}}) {
    PerformFixture f(true, 42, size.first, size.second);
    f.editor->setVisible(true);
    f.editor->openDrawer("scenes");
    for (int i = 0; i < 3; ++i) {
      click(f.editor->perform().scenes(), "Capture Scene");
      f.editor->commands().mutate();
    }
    for (const char* id : ids) {
      f.editor->openDrawer(id);
      pump(300);
      const auto img = f.editor->createComponentSnapshot(f.editor->getLocalBounds());
      juce::FileOutputStream fs(out.getChildFile(juce::String(id) + "_" + juce::String(size.first) + ".png"));
      fs.setPosition(0);
      fs.truncate();
      juce::PNGImageFormat().writeImageToStream(img, fs);
    }
  }
}
