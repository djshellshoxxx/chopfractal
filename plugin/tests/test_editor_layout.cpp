// The editor shell: pure layout function, panel placement at the default, minimum and 2x sizes, drawer, persistence.
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "CdlTheme.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ShellLayout.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
bool disjoint(const juce::Rectangle<int>& a, const juce::Rectangle<int>& b) { return !a.intersects(b); }

void checkLayout(int w, int h, int drawer) {
  const auto r = shell::computeShellLayout(w, h, 1.0f, drawer);
  const juce::Rectangle<int> win(0, 0, w, h);
  for (const auto& rc : {r.topBar, r.source, r.pattern, r.macro, r.status}) CHECK(win.contains(rc) && !rc.isEmpty());
  CHECK(disjoint(r.topBar, r.source) && disjoint(r.source, r.pattern) && disjoint(r.pattern, r.macro) && disjoint(r.macro, r.status));
  CHECK(r.sourceParts.wave.getHeight() >= 40);
  CHECK(r.patternParts.grid.getHeight() >= 100);
  CHECK(r.source.contains(r.sourceParts.wave) && r.source.contains(r.sourceParts.chopStrip) && r.pattern.contains(r.patternParts.grid));
  CHECK(r.macro.getHeight() >= 128);
  if (drawer > 0) {
    CHECK(!r.drawer.isEmpty() && win.contains(r.drawer));
    CHECK(disjoint(r.drawer, r.source) && disjoint(r.drawer, r.pattern) && disjoint(r.drawer, r.macro));
    CHECK(r.macro.getWidth() == w - 2 * r.margin);  // the macro strip keeps the full width
  } else {
    CHECK(r.drawer.isEmpty());
  }
}
}  // namespace

CHOP_TEST(shell_layout_matches_the_spec_numbers_at_default_size) {
  const auto r = shell::computeShellLayout(1100, 720);
  CHECK(r.scale == 1.0f);
  CHECK(r.topBar == juce::Rectangle<int>(0, 0, 1100, 44));
  CHECK(r.status == juce::Rectangle<int>(0, 692, 1100, 28));
  CHECK(r.macro == juce::Rectangle<int>(12, 516, 1076, 168));
  CHECK(r.source.getX() == 12 && r.source.getWidth() == 1076 && r.source.getY() == 52);
  CHECK(r.sourceParts.toolbar.getHeight() == 28 && r.patternParts.header.getHeight() == 28);
  CHECK(r.patternParts.laneHeaders.getWidth() == 148);
  const int slack = r.macro.getY() - r.pattern.getBottom();
  CHECK(slack == r.gap);
}

CHOP_TEST(shell_layout_holds_from_minimum_to_double_size_with_and_without_a_drawer) {
  for (const auto size : {juce::Point<int>(880, 560), juce::Point<int>(1100, 720), juce::Point<int>(1500, 900), juce::Point<int>(2200, 1440), juce::Point<int>(900, 1440),
                          juce::Point<int>(2200, 560)}) {
    checkLayout(size.x, size.y, 0);
    checkLayout(size.x, size.y, 560);
  }
  const auto small = shell::computeShellLayout(880, 560);
  CHECK(small.source.getHeight() >= 150 && small.pattern.getHeight() >= 170);
  CHECK(small.patternParts.laneHeaders.getWidth() == 120);
  const auto big = shell::computeShellLayout(2200, 1440);
  CHECK(big.scale == 2.0f && big.topBar.getHeight() == 88 && big.status.getHeight() == 56);
}

CHOP_TEST(editor_panels_sit_where_the_shell_layout_puts_them_at_every_size) {
  if (!haveDisplay()) return;
  const auto previous = cdl::settingsFile();
  const auto ui = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_layout_test_ui.json");
  ui.deleteFile();
  cdl::setSettingsFileOverride(ui);
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  CHECK(editor != nullptr);
  if (editor) {
    editor->setVisible(true);
    pump(60);
    CHECK(editor->getWidth() == 1100 && editor->getHeight() == 720);
    auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get());
    CHECK(ed != nullptr);
    for (const auto size : {juce::Point<int>(1100, 720), juce::Point<int>(880, 560), juce::Point<int>(2200, 1440)}) {
      editor->setSize(size.x, size.y);
      pump(60);
      const auto r = shell::computeShellLayout(size.x, size.y);
      struct Expect {
        const char* id;
        juce::Rectangle<int> bounds;
      };
      const Expect expected[] = {{"TopBar", r.topBar},          {"WavePanel", r.sourceParts.wave}, {"PatternPanel", r.patternParts.grid},
                                 {"MacroStrip", r.macro},       {"StatusBar", r.status},           {"ShellChrome", {0, 0, size.x, size.y}}};
      for (const auto& e : expected) {
        auto* c = editor->findChildWithID(e.id);
        CHECK(c != nullptr);
        if (!c) continue;
        if (c->getBounds() != e.bounds) std::fprintf(stderr, "  %s at %dx%d is %s, expected %s\n", e.id, size.x, size.y, c->getBounds().toString().toRawUTF8(), e.bounds.toString().toRawUTF8());
        CHECK(c->getBounds() == e.bounds);
      }
      // Nine macro knobs, every one inside the strip and none overlapping the next.
      const auto& knobs = ed->macroStrip().knobs();
      CHECK(knobs.size() == 9);
      for (std::size_t i = 0; i < knobs.size(); ++i) {
        CHECK(knobs[i]->getParentComponent()->getLocalBounds().contains(knobs[i]->getBounds()));
        if (i > 0) CHECK(!knobs[i]->getBounds().intersects(knobs[i - 1]->getBounds()));
        CHECK(knobs[i]->slider().getWidth() >= 40);
      }
    }
    // A drawer takes width from the source and pattern panels only.
    editor->setSize(1100, 720);
    pump(30);
    const auto closed = ed->waveformBounds();
    auto* macro = editor->findChildWithID("MacroStrip");
    const auto macroBounds = macro->getBounds();
    CHECK(ed->openDrawer("tools"));
    CHECK(ed->openDrawerId() == "tools");
    pump(30);
    CHECK(ed->waveformBounds().getWidth() < closed.getWidth());
    CHECK(macro->getBounds() == macroBounds);
    auto* drawer = editor->findChildWithID("DrawerHost");
    CHECK(drawer != nullptr && drawer->isVisible() && !drawer->getBounds().intersects(ed->waveformBounds()));
    CHECK(!ed->openDrawer("no-such-drawer"));
    ed->closeDrawer();
    pump(30);
    CHECK(ed->waveformBounds() == closed);
  }
  editor.reset();
  ui.deleteFile();
  cdl::setSettingsFileOverride(previous);
}

CHOP_TEST(window_size_theme_and_mode_are_remembered_per_user) {
  if (!haveDisplay()) return;
  const auto previous = cdl::settingsFile();
  const auto ui = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_persist_test_ui.json");
  ui.deleteFile();
  cdl::setSettingsFileOverride(ui);
  ChopFractalProcessor p;
  {
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get());
    CHECK(ed != nullptr);
    if (ed) {
      CHECK(ed->theme() == cdl::ThemeId::Night && !ed->advanced());
      ed->setSize(1300, 800);
      ed->setTheme(cdl::ThemeId::Concrete);
      ed->setAdvanced(true);
      pump(900);  // the resize is saved after the size has been stable for a few ticks
    }
  }
  CHECK(cdl::loadUserSettings().width == 1300 && cdl::loadUserSettings().height == 800);
  {
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* ed = dynamic_cast<ChopFractalEditor*>(editor.get());
    CHECK(ed != nullptr);
    if (ed) {
      CHECK(ed->getWidth() == 1300 && ed->getHeight() == 800);
      CHECK(ed->theme() == cdl::ThemeId::Concrete && ed->advanced());
      auto* zoom = findButton(*ed, "Zoom In");
      CHECK(zoom != nullptr && zoom->isVisible());  // Advanced shows the hit editing controls
      ed->setAdvanced(false);
      CHECK(!zoom->isVisible());
      ed->setTheme(cdl::ThemeId::Night);
    }
  }
  ui.deleteFile();
  cdl::setSettingsFileOverride(previous);
  cdl::setCurrentTheme(cdl::ThemeId::Night);
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
