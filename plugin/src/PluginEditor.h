#pragma once
// The editor shell. Layout (see docs/specs/gui-redesign.md, as-built section): TopBar over a Source panel (ShellChrome
// furniture plus the WavePanel waveform), a Pattern panel (lane headers plus the PatternPanel grid), the MacroStrip, the
// StatusBar, and a DrawerHost on the right for everything else. The shell owns the model and the command layer; panels
// read the model and call EditorCommands, and hold no pattern, chop or history logic of their own.
//
// Registering a drawer: build a component, call registerDrawer(id, title, content, width). It gets a toggle button in the
// top bar (unless showButton is false) and is shown in the drawer region when openDrawer(id) / toggleDrawer(id) is called.
#include <juce_audio_utils/juce_audio_utils.h>

#include "CdlLookAndFeel.h"
#include "ControlsPanel.h"
#include "DrawerHost.h"
#include "EditorCommands.h"
#include "EditorModel.h"
#include "EditorTheme.h"
#include "FeaturePanel.h"
#include "HistoryPanel.h"
#include "MacroStrip.h"
#include "OrbitPanel.h"
#include "PatternPanel.h"
#include "ExportDrawer.h"
#include "PresetBrowser.h"
#include "PluginProcessor.h"
#include "SettingsDrawer.h"
#include "ShellChrome.h"
#include "ShellLayout.h"
#include "StatusBar.h"
#include "TopBar.h"
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

  // ---- drawer registration (second wave panels plug in here) ----
  void registerDrawer(const juce::String& id, const juce::String& title, juce::Component& content, int preferredWidth = 320, bool showButton = true);
  bool openDrawer(const juce::String& id) { return drawer_.open(id); }
  void closeDrawer() { drawer_.close(); }
  bool toggleDrawer(const juce::String& id) { return drawer_.toggle(id); }
  juce::String openDrawerId() const { return drawer_.openId(); }
  // Shell services for drawer content.
  EditorModel& model() { return model_; }
  EditorCommands& commands() { return commands_; }

  // Theme (Night / Concrete) and Simple / Advanced: applied to the whole editor and remembered per user.
  void setTheme(cdl::ThemeId);
  cdl::ThemeId theme() const { return cdl::currentTheme(); }
  void setAdvanced(bool);
  bool advanced() const { return advanced_; }
  void setUiScalePercent(int percent);  // 90, 100, 110 or 125: scales the whole shell, capped so the window still fits it
  int uiScalePercent() const { return uiScalePercent_; }
  void setReducedMotion(bool on);
  SettingsDrawer& settingsDrawer() { return settings_; }
  const shell::ShellRects& shellRects() const { return rects_; }

  // Public so tests can drive them without modal dialogs.
  void doReroll() { features_.reroll(); }
  bool applyPreset(const chopfractal::presets::Preset& p, bool generateAfter = false) {
    const bool ok = features_.applyPreset(p, generateAfter);
    macro_.resetDriftTicks();
    return ok;
  }
  chopfractal::presets::Preset currentPreset(const juce::String& name) const { return features_.currentPreset(name); }
  bool savePresetNamed(const juce::String& name, bool overwrite) { return presets_.saveCurrent(name, overwrite); }
  bool deleteSelectedPreset() { return presets_.deleteSelected(); }
  void reloadPresets() { presets_.reload(); }
  PresetBrowser& presetBrowser() { return presets_; }
  ExportDrawer& exportDrawer() { return export_; }
  juce::File makeDragFile(bool wav) { return features_.makeDragFile(wav); }
  // Read-only views for tests and accessibility tooling (editor coordinates).
  juce::Rectangle<int> patternBounds() const { return pattern_.getBounds(); }
  juce::Rectangle<int> waveformBounds() const { return wave_.getBounds(); }
  const MacroStrip& macroStrip() const { return macro_; }
  const std::vector<chopfractal::ui::EventRect>& hitRects() const { return model_.view().pattern.rects; }
  int laneCount() const { return static_cast<int>(model_.view().chops.size()); }
  chopfractal::EventId selectedHit() const { return model_.selected(); }
  const std::vector<chopfractal::EventId>& selectedHits() const { return model_.selectedHits(); }
  const std::vector<chopfractal::ChopId>& selectedChops() const { return model_.selectedChops(); }

 private:
  void timerCallback() override;
  void syncHelpTree();
  void saveUserState();

  // Declaration order is destruction order reversed: the panels (and the attachments they own) go before the model.
  cdl::CdlLookAndFeel lnf_;  // first member: outlives every component that uses it
  EditorModel model_;
  EditorCommands commands_;
  juce::TooltipWindow tooltip_{this};
  juce::Component* tooltipRoot_ = nullptr;  // root changes when the standalone wrapper attaches the editor
  ShellChrome chrome_;
  WavePanel wave_;
  PatternPanel pattern_;
  MacroStrip macro_;
  TopBar topBar_;
  StatusBar status_;
  // Legacy panels: still the only home of the older controls until the second wave replaces them; shown in the Tools drawer.
  juce::Component legacyBody_;
  ControlsPanel controls_;
  FeaturePanel features_;
  HistoryPanel history_;
  OrbitPanel orbit_;
  PresetBrowser presets_;
  ExportDrawer export_;
  juce::Viewport legacyView_;
  SettingsDrawer settings_;
  DrawerHost drawer_;

  shell::ShellRects rects_;
  bool advanced_ = false;
  int uiScalePercent_ = 100;
  bool lastPlaying_ = false;
  int stableSizeTicks_ = 0;
  juce::Point<int> savedSize_;
  int lastWaveWidth_ = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalEditor)
};
