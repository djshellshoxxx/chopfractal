#pragma once
// The editor shell: owns the model, the command layer and the seven panels, lays them out, runs the 10 Hz refresh
// and handles file drops, key presses and the hover help. The panels draw the headless view models from
// plugin_ui_adapter and route user actions through EditorCommands to the composition root; the shell holds no
// pattern, chop or history logic of its own.
#include <juce_audio_utils/juce_audio_utils.h>

#include "CdlLookAndFeel.h"
#include "ControlsPanel.h"
#include "EditorCommands.h"
#include "EditorModel.h"
#include "EditorTheme.h"
#include "FeaturePanel.h"
#include "HistoryPanel.h"
#include "OrbitPanel.h"
#include "PatternPanel.h"
#include "PluginProcessor.h"
#include "StatusBar.h"
#include "WavePanel.h"

class ChopFractalEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public juce::TooltipClient, private juce::Timer {
 public:
  explicit ChopFractalEditor(ChopFractalProcessor& processor);
  ~ChopFractalEditor() override;

  juce::String getTooltip() override;
  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void filesDropped(const juce::StringArray& files, int x, int y) override;
  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress&) override;

  // Public so tests can drive them without modal dialogs.
  void doReroll() { features_.reroll(); }
  void applyPreset(const chopfractal::presets::Preset& p) { features_.applyPreset(p); }
  chopfractal::presets::Preset currentPreset(const juce::String& name) const { return features_.currentPreset(name); }
  void savePresetNamed(const juce::String& name, bool overwrite) { features_.savePresetNamed(name, overwrite); }
  void deleteSelectedPreset() { features_.deleteSelectedPreset(); }
  void reloadPresets() { features_.reloadPresets(); }
  juce::File makeDragFile(bool wav) { return features_.makeDragFile(wav); }
  // Theme (Night / Concrete): applies to the whole editor tree and is remembered per user.
  void setTheme(cdl::ThemeId);
  cdl::ThemeId theme() const { return cdl::currentTheme(); }
  // Read-only views for tests and accessibility tooling (editor coordinates).
  juce::Rectangle<int> patternBounds() const { return pattern_.getBounds(); }
  juce::Rectangle<int> waveformBounds() const { return wave_.getBounds(); }
  const std::vector<chopfractal::ui::EventRect>& hitRects() const { return model_.view().pattern.rects; }
  int laneCount() const { return static_cast<int>(model_.view().chops.size()); }
  chopfractal::EventId selectedHit() const { return model_.selected(); }
  const std::vector<chopfractal::EventId>& selectedHits() const { return model_.selectedHits(); }
  const std::vector<chopfractal::ChopId>& selectedChops() const { return model_.selectedChops(); }

 private:
  void timerCallback() override;
  void syncHelpTree();

  // Declaration order is destruction order reversed: the panels (and the attachments they own) go before the model.
  cdl::CdlLookAndFeel lnf_;  // first member: outlives every component that uses it
  EditorModel model_;
  EditorCommands commands_;
  juce::TooltipWindow tooltip_{this};
  juce::Component* tooltipRoot_ = nullptr;  // root changes when the standalone wrapper attaches the editor
  ControlsPanel controls_;
  FeaturePanel features_;
  WavePanel wave_;
  PatternPanel pattern_;
  HistoryPanel history_;
  OrbitPanel orbit_;
  StatusBar status_;
  bool lastPlaying_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalEditor)
};
