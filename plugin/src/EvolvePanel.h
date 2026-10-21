#pragma once
// Evolve drawer: Evolve (run, cadence, amount, keep), Fractal (motif presets, motif, depth, accent decay) and the Smart Setup
// suggestions (role guesses with their confidence and reason, the loop and tempo guesses, Accept Roles, Use Tempo). It owns
// the widgets that used to live in the Features panel; presets read and write them through the small API below.
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/preset_store/presets.hpp>

#include "DrawerPanel.h"
#include "EditorCommands.h"

// Read-only list of Smart Setup's suggestions, one line each.
class SuggestionList : public juce::Component, public juce::SettableTooltipClient {
 public:
  SuggestionList() { setComponentID("canvas-suggestions"); }
  void set(std::vector<juce::String> loopLines, std::vector<juce::String> roleLines, const juce::String& emptyText);
  int lineCount() const { return static_cast<int>(loops_.size() + roles_.size()); }
  const std::vector<juce::String>& loopLines() const { return loops_; }
  const std::vector<juce::String>& roleLines() const { return roles_; }
  int heightFor() const;
  void paint(juce::Graphics&) override;

 private:
  std::vector<juce::String> loops_, roles_;
  juce::String empty_;
};

class EvolvePanel : public DrawerPanel {
 public:
  EvolvePanel(EditorModel&, EditorCommands&);

  // ---- used by the Features panel (presets) and the shell's Evolve button ----
  FractalUi fractalUi() const;
  EvolveUi evolveUi() const;
  void setEvolveEnabled(bool on);
  bool evolveEnabled() const { return evolve_.getToggleState(); }
  void showPreset(const chopfractal::presets::Preset&);  // motif, depth, cadence and amount, without firing the handlers
  void setCadence(int everyIndex, double amount);        // the Every box (0..4) and the Amount slider, without firing the handlers

  // The motif choices of the preset box (name, motif).
  static const std::vector<std::pair<const char*, const char*>>& motifPresets();
  SuggestionList& suggestions() { return suggestions_; }

 private:
  int layoutBody(int width) override;
  void tick() override {}
  void modelChanged(Change) override;
  void pushEvolve();
  void refreshSuggestions();

  chopfractal::evolve::Settings pushedEvolve_;  // the settings this panel last sent; a different session value (a restored project) is shown
  juce::Label motifLabel_{{}, "Motif"}, presetLabel_{{}, "Motif preset"}, depthLabel_{{}, "Depth"}, accentLabel_{{}, "Accent decay"}, everyLabel_{{}, "Every"},
      amountLabel_{{}, "Amount"}, evolveStatus_;
  juce::TextEditor motif_;
  juce::ComboBox motifPreset_, depth_, every_;
  juce::Slider accent_, evolveAmount_;
  juce::ToggleButton evolve_{"Run Evolve"};
  juce::TextButton fractal_{"Fractal"}, keep_{"Keep"}, smart_{"Smart Setup"}, acceptRoles_{"Accept Roles"}, useTempo_{"Use Tempo"};
  SuggestionList suggestions_;
  bool updating_ = false;
};
