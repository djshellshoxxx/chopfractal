#pragma once
// Smart Setup, Fractal, Evolve, export, snapshots, roles, presets and drag-out. These controls act on the project
// rather than on host parameters. Their widget state is translated into the *Ui structs the commands take.
#include <vector>

#include "DragOut.h"
#include "EditorPanels.h"

class FeaturePanel : public EditorPanelBase {
 public:
  FeaturePanel(EditorModel&, EditorCommands&);
  void resized() override;

  // Public so tests can drive them without modal dialogs.
  void reroll() { cmd_.reroll(); }
  // The shell's Evolve button drives the same settings as the rows below (Every, Amount, Depth, Motif).
  void setEvolveEnabled(bool on);
  bool evolveEnabled() const { return evolve_.getToggleState(); }
  // Applies a preset through EditorCommands (parameters, seed, grid, motif, rules), stops a running Evolve first and shows
  // the preset's Evolve cadence. The pattern is untouched unless `generateAfter`.
  bool applyPreset(const chopfractal::presets::Preset&, bool generateAfter = false);
  bool undoPreset();
  void setEvolveValues(int everyLoops, double amount);
  int evolveEveryIndex() const { return every_.getSelectedId() - 1; }
  double evolveAmountValue() const { return evolveAmount_.getValue(); }
  chopfractal::presets::Preset currentPreset(const juce::String& name) const;
  juce::File makeDragFile(bool wav) { return cmd_.makeDragFile(wav, exportUi()); }

 private:
  void modelChanged(Change) override;
  void pushEvolve();
  void updateExportButtons();
  FractalUi fractalUi() const;
  EvolveUi evolveUi() const;
  ExportUi exportUi() const;

  juce::TextButton smart_{"Smart Setup"}, acceptRoles_{"Accept Roles"}, useTempo_{"Use Tempo"}, fractal_{"Fractal"}, keep_{"Keep"}, exportWav_{"Export WAV"},
      exportKit_{"Export Kit"}, locate_{"Locate Source"};
  juce::TextButton storeA_{"Store A"}, recallA_{"Recall A"}, storeB_{"Store B"}, recallB_{"Recall B"}, setRole_{"Set Role"};
  int undoEvery_ = 2;
  double undoAmount_ = 0.25;
  juce::ComboBox role_, detectMode_;
  DragHandle dragMidi_{"Drag MIDI", [this] { return makeDragFile(false); }};
  DragHandle dragWav_{"Drag WAV", [this] { return makeDragFile(true); }};
  juce::ToggleButton evolve_{"Run Evolve"};  // legacy mirror of the shell's Evolve button
  juce::Label motifLabel_{{}, "Motif"}, depthLabel_{{}, "Depth"}, everyLabel_{{}, "Every"}, fmtLabel_{{}, "Format"}, loopsLabel_{{}, "Loops"};
  juce::TextEditor motif_;
  chopfractal::evolve::Settings pushedEvolve_;  // the settings this panel last sent; a different session value (a restored project) is shown
  juce::ComboBox depth_, every_, exportFormat_, exportRate_, exportLoops_;
  juce::Slider evolveAmount_;
};
