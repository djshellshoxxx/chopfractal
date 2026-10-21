#pragma once
// Detection mode, source relink and roles (the Tools drawer). Smart Setup, Fractal and Evolve live in the EvolvePanel, the A/B
// snapshots in the ScenesPanel, presets in the PresetBrowser and export in the ExportDrawer; this panel reads and writes the
// Evolve widgets through attachEvolve() when a preset is applied.
#include <vector>

#include "EditorPanels.h"
#include "EvolvePanel.h"

class FeaturePanel : public EditorPanelBase {
 public:
  FeaturePanel(EditorModel&, EditorCommands&);
  void resized() override;

  // Public so tests can drive them without modal dialogs.
  void reroll() { cmd_.reroll(); }
  // The Evolve and Fractal widgets live in the EvolvePanel; the shell attaches it once both exist.
  void attachEvolve(EvolvePanel* e) { evolve_ = e; }
  // The shell's Evolve button drives the same settings as the Evolve drawer (Every, Amount, Depth, Motif).
  void setEvolveEnabled(bool on) { if (evolve_) evolve_->setEvolveEnabled(on); }
  bool evolveEnabled() const { return evolve_ && evolve_->evolveEnabled(); }
  // Applies a preset through EditorCommands (parameters, seed, grid, motif, rules), stops a running Evolve first and shows
  // the preset's Evolve cadence. The pattern is untouched unless `generateAfter`.
  bool applyPreset(const chopfractal::presets::Preset&, bool generateAfter = false);
  bool undoPreset();
  chopfractal::presets::Preset currentPreset(const juce::String& name) const;
  juce::File makeDragFile(bool wav) { return cmd_.makeDragFile(wav, cmd_.exportUi()); }

 private:
  void modelChanged(Change) override;
  FractalUi fractalUi() const { return evolve_ ? evolve_->fractalUi() : FractalUi{}; }
  EvolveUi evolveUi() const { return evolve_ ? evolve_->evolveUi() : EvolveUi{}; }

  juce::TextButton locate_{"Locate Source"}, setRole_{"Set Role"};
  juce::ComboBox role_, detectMode_;
  EvolvePanel* evolve_ = nullptr;
  int undoEvery_ = 2;  // the Evolve cadence before the last preset, for Undo preset
  double undoAmount_ = 0.25;
};
