#pragma once
// Smart Setup, Fractal, Evolve, export, snapshots, roles, presets and drag-out. These controls act on the project
// rather than on host parameters. Their widget state is translated into the *Ui structs the commands take.
#include <vector>

#include "DragOut.h"
#include "EditorPanels.h"
#include "PresetManager.h"

class FeaturePanel : public EditorPanelBase {
 public:
  FeaturePanel(EditorModel&, EditorCommands&);
  void resized() override;

  // Public so tests can drive them without modal dialogs.
  void reroll() { cmd_.reroll(); }
  // The shell's Evolve button drives the same settings as the rows below (Every, Amount, Depth, Motif).
  void setEvolveEnabled(bool on);
  bool evolveEnabled() const { return evolve_.getToggleState(); }
  juce::String presetName() const;  // the preset chosen in the list, empty after a manual change
  void applyPreset(const chopfractal::presets::Preset&);
  chopfractal::presets::Preset currentPreset(const juce::String& name) const;
  void savePresetNamed(const juce::String& name, bool overwrite);
  void deleteSelectedPreset();
  void reloadPresets();
  juce::File makeDragFile(bool wav) { return cmd_.makeDragFile(wav, exportUi()); }

 private:
  void modelChanged(Change) override;
  void pushEvolve();
  void updateExportButtons();
  void askSavePreset();
  FractalUi fractalUi() const;
  EvolveUi evolveUi() const;
  ExportUi exportUi() const;

  juce::TextButton smart_{"Smart Setup"}, acceptRoles_{"Accept Roles"}, useTempo_{"Use Tempo"}, fractal_{"Fractal"}, keep_{"Keep"}, exportWav_{"Export WAV"},
      exportKit_{"Export Kit"}, locate_{"Locate Source"};
  juce::TextButton storeA_{"Store A"}, recallA_{"Recall A"}, storeB_{"Store B"}, recallB_{"Recall B"}, setRole_{"Set Role"};
  juce::TextButton savePreset_{"Save Preset"}, deletePreset_{"Delete"};
  juce::ComboBox role_, preset_, detectMode_;
  std::vector<PresetManager::Entry> presets_;
  DragHandle dragMidi_{"Drag MIDI", [this] { return makeDragFile(false); }};
  DragHandle dragWav_{"Drag WAV", [this] { return makeDragFile(true); }};
  juce::ToggleButton evolve_{"Run Evolve"};  // legacy mirror of the shell's Evolve button
  juce::Label motifLabel_{{}, "Motif"}, depthLabel_{{}, "Depth"}, everyLabel_{{}, "Every"}, fmtLabel_{{}, "Format"}, loopsLabel_{{}, "Loops"};
  juce::TextEditor motif_;
  chopfractal::evolve::Settings pushedEvolve_;  // the settings this panel last sent; a different session value (a restored project) is shown
  juce::ComboBox depth_, every_, exportFormat_, exportRate_, exportLoops_;
  juce::Slider evolveAmount_;
};
