#pragma once
// Helpers shared by the G3 workflow tests: an isolated per-user folder, a ready editor and panel snapshots.
#include <memory>

#include "CdlTheme.h"
#include "DragOut.h"
#include "PluginEditor.h"
#include "PresetManager.h"
#include "shell_test_support.hpp"

namespace shelltest {
// Points every per-user file at a fresh temp folder for the life of the object.
struct IsolatedUser {
  juce::File root, previousSettings, previousQuickStart;  // restored on exit: main() points both at temp files
  IsolatedUser() {
    previousSettings = cdl::settingsFile();
    previousQuickStart = QuickStartTracker::defaultFile();
    root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_g3_" + juce::String(juce::Random::getSystemRandom().nextInt()));
    root.createDirectory();
    PresetManager::setFolderOverride(root.getChildFile("presets"));
    dragout::setFolderOverride(root.getChildFile("drag"));
    cdl::setSettingsFileOverride(root.getChildFile("ui.json"));
    QuickStartTracker::setFileOverride(root.getChildFile("quickstart.json"));
    cdl::setCurrentTheme(cdl::ThemeId::Night);
    cdl::setReducedMotion(false);
  }
  ~IsolatedUser() {
    PresetManager::setFolderOverride(juce::File());
    dragout::setFolderOverride(juce::File());
    cdl::setSettingsFileOverride(previousSettings);
    QuickStartTracker::setFileOverride(previousQuickStart);
    cdl::setCurrentTheme(cdl::ThemeId::Night);
    cdl::setReducedMotion(false);
    root.deleteRecursively();
  }
};

// A processor with a loaded, chopped and generated source plus its editor, shown at a given size.
struct EditorRig {
  ChopFractalProcessor proc;
  std::unique_ptr<juce::AudioProcessorEditor> base;
  ChopFractalEditor* ed = nullptr;
  juce::Component& settingsOwner() { return ed->settingsDrawer(); }
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
// The same without the settle time: for popups that close themselves when the window loses focus.
inline void snapshotNow(juce::Component& c, const juce::String& name) {
  const char* dir = std::getenv("CHOPFRACTAL_G3_SNAPS");
  if (!dir) return;
  juce::File folder(dir);
  folder.createDirectory();
  juce::Image img = c.createComponentSnapshot(c.getLocalBounds());
  juce::File f = folder.getChildFile(name + "_" + juce::String(c.getWidth()) + "x" + juce::String(c.getHeight()) + ".png");
  f.deleteFile();
  juce::FileOutputStream fs(f);
  juce::PNGImageFormat().writeImageToStream(img, fs);
}
}  // namespace shelltest
