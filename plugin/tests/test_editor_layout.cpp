// The editor's panels sit at the same pixels the single-file editor drew them at (editor-local coordinates).
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
struct PanelBounds {
  const char* id;
  juce::Rectangle<int> bounds;
};
// The rows land with the panels, one extraction at a time.
const PanelBounds kPanels[] = {
    {"ControlsPanel", {10, 10, 920, 232}},
    {"StatusBar", {10, 830, 920, 70}},
    {"OrbitPanel", {824, 720, 106, 106}},
    {"FeaturePanel", {10, 248, 920, 104}},
    {"WavePanel", {10, 358, 920, 120}},
    {"PatternPanel", {10, 484, 920, 230}},
    {"HistoryPanel", {10, 720, 808, 106}},
};
}  // namespace

CHOP_TEST(editor_panels_have_the_pre_refactor_bounds) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->setVisible(true);
  pump(60);
  CHECK(editor->getWidth() == 940 && editor->getHeight() == 912);
  for (const auto& panel : kPanels) {
    auto* c = editor->findChildWithID(panel.id);
    CHECK(c != nullptr);
    if (!c) continue;
    if (c->getBounds() != panel.bounds)
      std::fprintf(stderr, "  %s is at %s, expected %s\n", panel.id, c->getBounds().toString().toRawUTF8(), panel.bounds.toString().toRawUTF8());
    CHECK(c->getBounds() == panel.bounds);
  }
}

// With CHOPFRACTAL_SNAPSHOT_REF pointing at a PNG rendered by the pre-refactor editor, every pixel must match.
CHOP_TEST(editor_snapshot_is_unchanged_by_the_split) {
  if (!haveDisplay()) return;
  const char* ref = std::getenv("CHOPFRACTAL_SNAPSHOT_REF");
  if (!ref) return;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->setVisible(true);
  pump(300);
  const juce::Image now = editor->createComponentSnapshot(editor->getLocalBounds());
  const juce::Image was = juce::ImageFileFormat::loadFrom(juce::File(ref));
  CHECK(was.isValid());
  if (!was.isValid()) return;
  CHECK(now.getWidth() == was.getWidth() && now.getHeight() == was.getHeight());
  if (now.getWidth() != was.getWidth() || now.getHeight() != was.getHeight()) return;
  int differing = 0;
  for (int y = 0; y < now.getHeight(); ++y)
    for (int x = 0; x < now.getWidth(); ++x)
      if (now.getPixelAt(x, y) != was.getPixelAt(x, y)) ++differing;
  if (differing) std::fprintf(stderr, "  %d pixels differ from %s\n", differing, ref);
  CHECK(differing == 0);
}
