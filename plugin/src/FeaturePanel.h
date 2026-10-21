#pragma once
// Export, roles, detection mode, presets and drag-out (the Tools drawer). Smart Setup, Fractal and Evolve moved to the
// EvolvePanel and the A/B snapshots to the ScenesPanel; presets read and write their widgets through attachEvolve().
#include <vector>

#include "DragOut.h"
#include "EvolvePanel.h"
#include "EditorPanels.h"
#include "PresetManager.h"

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
  juce::String presetName() const;  // the preset chosen in the list, empty after a manual change
  void applyPreset(const chopfractal::presets::Preset&);
  chopfractal::presets::Preset currentPreset(const juce::String& name) const;
  void savePresetNamed(const juce::String& name, bool overwrite);
  void deleteSelectedPreset();
  void reloadPresets();
  juce::File makeDragFile(bool wav) { return cmd_.makeDragFile(wav, exportUi()); }

 private:
  void modelChanged(Change) override;
  void updateExportButtons();
  void askSavePreset();
  FractalUi fractalUi() const { return evolve_ ? evolve_->fractalUi() : FractalUi{}; }
  EvolveUi evolveUi() const { return evolve_ ? evolve_->evolveUi() : EvolveUi{}; }
  ExportUi exportUi() const;

  juce::TextButton exportWav_{"Export WAV"}, exportKit_{"Export Kit"}, locate_{"Locate Source"}, setRole_{"Set Role"};
  juce::TextButton savePreset_{"Save Preset"}, deletePreset_{"Delete"};
  juce::ComboBox role_, preset_, detectMode_;
  std::vector<PresetManager::Entry> presets_;
  DragHandle dragMidi_{"Drag MIDI", [this] { return makeDragFile(false); }};
  DragHandle dragWav_{"Drag WAV", [this] { return makeDragFile(true); }};
  EvolvePanel* evolve_ = nullptr;
  juce::Label fmtLabel_{{}, "Format"}, loopsLabel_{{}, "Loops"};
  juce::ComboBox exportFormat_, exportRate_, exportLoops_;
};
