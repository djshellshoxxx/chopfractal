#include "FeaturePanel.h"

#include <algorithm>
#include <initializer_list>

namespace cf = chopfractal;

FeaturePanel::FeaturePanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "FeaturePanel") {
  detectMode_.addItemList({"Transients (replace)", "Transients (keep my markers)", "Even grid 1/8", "Even grid 1/16"}, 1);
  detectMode_.setSelectedId(model_.detectMode(), juce::dontSendNotification);
  detectMode_.setTitle("Detection mode");
  detectMode_.onChange = [this] { model_.setDetectMode(detectMode_.getSelectedId()); };
  addAndMakeVisible(detectMode_);
  locate_.onClick = [this] { cmd_.locateSource(); };
  addAndMakeVisible(locate_);
  locate_.setEnabled(false);

  addAndMakeVisible(setRole_);
  setRole_.setTitle(setRole_.getButtonText());
  for (const auto& r : cf::roles::builtinRoles()) role_.addItem(juce::String(r), role_.getNumItems() + 1);
  role_.setSelectedId(model_.extras().roleIndex + 1, juce::dontSendNotification);
  role_.setTitle("Role for the selected hit's chop");
  role_.onChange = [this] { model_.setExtras([this](cf::composition::ProjectExtras& e) { e.roleIndex = role_.getSelectedId() - 1; }); };
  addAndMakeVisible(role_);
  setRole_.onClick = [this] { cmd_.setRoleOfSelected(role_.getText()); };
}

void FeaturePanel::modelChanged(Change c) {
  switch (c) {
    case Change::Extras: {  // a project was restored: show its saved choices without firing the widgets' own handlers
      const auto& e = model_.extras();
      if (role_.getSelectedId() != e.roleIndex + 1) role_.setSelectedId(e.roleIndex + 1, juce::dontSendNotification);
      if (detectMode_.getSelectedId() != e.detectMode + 1) detectMode_.setSelectedId(e.detectMode + 1, juce::dontSendNotification);
      break;
    }
    case Change::Flags:
      locate_.setEnabled(model_.sourceMissing());
      break;
    default:
      break;
  }
}

void FeaturePanel::resized() {
  auto area = getLocalBounds();
  auto row1 = area.removeFromTop(26);
  locate_.setBounds(row1.removeFromLeft(110).reduced(2));
  detectMode_.setBounds(row1.removeFromLeft(190).reduced(2));
  auto row2 = area.removeFromTop(26);
  role_.setBounds(row2.removeFromLeft(110).reduced(2));
  setRole_.setBounds(row2.removeFromLeft(80).reduced(2));
}

// ---------------------------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------------------------

cf::presets::Preset FeaturePanel::currentPreset(const juce::String& name) const { return cmd_.currentPreset(name, fractalUi(), evolveUi()); }

bool FeaturePanel::applyPreset(const cf::presets::Preset& p, bool generateAfter) {
  if (evolveEnabled()) {  // a running Evolve would pick up the new cadence mid-loop: stop it first
    setEvolveEnabled(false);
    cmd_.say("Evolve stopped to apply the preset.");
  }
  const EvolveUi before = evolveUi();
  if (!cmd_.applyPreset(p, generateAfter)) return false;
  undoEvery_ = before.everyIndex;
  undoAmount_ = before.amount;
  if (evolve_) evolve_->showPreset(p);  // motif, depth, accent decay, cadence and amount without firing the handlers
  return true;
}

bool FeaturePanel::undoPreset() {
  if (!cmd_.undoPreset()) return false;
  if (evolve_) evolve_->setCadence(undoEvery_, undoAmount_);
  return true;
}
