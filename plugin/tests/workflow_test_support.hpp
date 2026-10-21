#pragma once
// Helpers shared by the G3 workflow tests: an isolated per-user folder, a ready editor and panel snapshots.
#include <memory>

#include "CdlTheme.h"
#include "DragOut.h"
#include "PluginEditor.h"
#include "PresetManager.h"
#include "shell_test_support.hpp"

namespace shelltest {
inline juce::Slider* findSlider(juce::Component& root, const juce::String& title) {
  juce::Slider* found = nullptr;
  int matches = 0;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* b = dynamic_cast<juce::Slider*>(&c))
      if (b->getTitle() == title) {
        found = b;
        ++matches;
      }
  });
  CHECK(matches <= 1);
  return found;
}
// Points every per-user file at a fresh temp folder for the life of the object.
struct IsolatedUser {
  juce::File root;
  IsolatedUser() {
    root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_g3_" + juce::String(juce::Random::getSystemRandom().nextInt()));
    root.createDirectory();
    PresetManager::setFolderOverride(root.getChildFile("presets"));
    dragout::setFolderOverride(root.getChildFile("drag"));
    cdl::setSettingsFileOverride(root.getChildFile("ui.json"));
    cdl::setCurrentTheme(cdl::ThemeId::Night);
  }
  ~IsolatedUser() {
    PresetManager::setFolderOverride(juce::File());
    dragout::setFolderOverride(juce::File());
    cdl::setSettingsFileOverride(juce::File());
    cdl::setCurrentTheme(cdl::ThemeId::Night);
    root.deleteRecursively();
  }
};

// A processor with a loaded, chopped and generated source plus its editor, shown at a given size.
struct EditorRig {
  ChopFractalProcessor proc;
  std::unique_ptr<juce::AudioProcessorEditor> base;
  ChopFractalEditor* ed = nullptr;
  explicit EditorRig(bool generate = true, int w = 1100, int h = 720) {
    prepare(proc);
    if (generate) makeSession(proc);
    else
      proc.withSession([&](cf::composition::ProjectSession& s) {
        CHECK(s.loadSource(drumLoop(), "loop", "").ok());
        cf::composition::DetectOptions d;
        d.analysis.sensitivity = 0.8;
        CHECK(s.applyChops(d).ok());
      });
    base.reset(proc.createEditor());
    ed = dynamic_cast<ChopFractalEditor*>(base.get());
    CHECK(ed != nullptr);
    if (ed) {
      ed->setSize(w, h);
      ed->setVisible(true);
      pump(120);
    }
  }
};

// Writes a PNG of the whole editor when CHOPFRACTAL_G3_SNAPS names a folder, so panels can be inspected at 1100x720 and 880x560.
inline void snapshot(juce::Component& root, const juce::String& name) {
  const char* dir = std::getenv("CHOPFRACTAL_G3_SNAPS");
  if (!dir) return;
  juce::File folder(dir);
  folder.createDirectory();
  pump(250);
  juce::Image img = root.createComponentSnapshot(root.getLocalBounds());
  juce::File f = folder.getChildFile(name + "_" + juce::String(root.getWidth()) + "x" + juce::String(root.getHeight()) + ".png");
  f.deleteFile();
  juce::FileOutputStream fs(f);
  juce::PNGImageFormat().writeImageToStream(img, fs);
}
}  // namespace shelltest
