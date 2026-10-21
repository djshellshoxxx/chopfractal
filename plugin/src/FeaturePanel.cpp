#include "FeaturePanel.h"

#include <algorithm>
#include <initializer_list>

namespace cf = chopfractal;

FeaturePanel::FeaturePanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "FeaturePanel") {
  for (juce::Button* b : std::initializer_list<juce::Button*>{&exportWav_, &exportKit_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  exportWav_.onClick = [this] { cmd_.exportWav(exportUi()); };
  exportKit_.onClick = [this] { cmd_.exportKit(); };

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

  fmtLabel_.setJustificationType(juce::Justification::centredRight);
  loopsLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(fmtLabel_);
  addAndMakeVisible(loopsLabel_);
  exportFormat_.addItemList({"16-bit", "24-bit", "32-bit float"}, 1);
  exportFormat_.setSelectedId(model_.extras().exportUi.formatIndex + 1, juce::dontSendNotification);
  exportFormat_.setTitle("Export format");
  exportRate_.addItemList({"44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"}, 1);
  exportRate_.setSelectedId(model_.extras().exportUi.rateIndex + 1, juce::dontSendNotification);
  exportRate_.setTitle("Export sample rate");
  exportLoops_.addItemList({"1", "2", "4", "8", "16"}, 1);
  exportLoops_.setSelectedId(model_.extras().exportUi.loopsIndex + 1, juce::dontSendNotification);
  exportLoops_.setTitle("Export loops");
  for (juce::ComboBox* c : std::initializer_list<juce::ComboBox*>{&exportFormat_, &exportRate_, &exportLoops_})
    c->onChange = [this] { model_.setExtras([this](cf::composition::ProjectExtras& e) { e.exportUi.formatIndex = exportFormat_.getSelectedId() - 1;
      e.exportUi.rateIndex = exportRate_.getSelectedId() - 1;
      e.exportUi.loopsIndex = exportLoops_.getSelectedId() - 1; }); };
  for (juce::ComboBox* c : std::initializer_list<juce::ComboBox*>{&exportFormat_, &exportRate_, &exportLoops_}) addAndMakeVisible(*c);

  for (juce::Button* b : std::initializer_list<juce::Button*>{&savePreset_, &deletePreset_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  savePreset_.onClick = [this] { askSavePreset(); };
  deletePreset_.onClick = [this] { deleteSelectedPreset(); };
  preset_.setTitle("Preset");
  preset_.setTextWhenNothingSelected("Choose a preset...");
  preset_.onChange = [this] {
    const int i = preset_.getSelectedId() - 1;
    if (i >= 0 && i < static_cast<int>(presets_.size())) applyPreset(presets_[static_cast<std::size_t>(i)].preset);
    deletePreset_.setEnabled(i >= 0 && i < static_cast<int>(presets_.size()) && !presets_[static_cast<std::size_t>(i)].factory);
  };
  addAndMakeVisible(preset_);
  deletePreset_.setEnabled(false);
  reloadPresets();
  dragMidi_.setTooltip("Drag the current pattern into your DAW as a MIDI file");
  dragWav_.setTooltip("Drag the current pattern into your DAW as a WAV file");
  addAndMakeVisible(dragMidi_);
  addAndMakeVisible(dragWav_);
  // Dropped files are not auto-deleted: DAWs commonly link them in place, so removing them would break projects.
}

ExportUi FeaturePanel::exportUi() const { return {exportFormat_.getSelectedId() - 1, exportRate_.getSelectedId() - 1, exportLoops_.getSelectedId() - 1}; }

juce::String FeaturePanel::presetName() const {
  const int i = preset_.getSelectedId() - 1;
  return i >= 0 && i < static_cast<int>(presets_.size()) ? preset_.getText() : juce::String();
}

void FeaturePanel::modelChanged(Change c) {
  switch (c) {
    case Change::View:
      updateExportButtons();
      break;
    case Change::Extras: {  // a project was restored: show its saved choices without firing the widgets' own handlers
      const auto& e = model_.extras();
      if (role_.getSelectedId() != e.roleIndex + 1) role_.setSelectedId(e.roleIndex + 1, juce::dontSendNotification);
      if (detectMode_.getSelectedId() != e.detectMode + 1) detectMode_.setSelectedId(e.detectMode + 1, juce::dontSendNotification);
      if (exportFormat_.getSelectedId() != e.exportUi.formatIndex + 1) exportFormat_.setSelectedId(e.exportUi.formatIndex + 1, juce::dontSendNotification);
      if (exportRate_.getSelectedId() != e.exportUi.rateIndex + 1) exportRate_.setSelectedId(e.exportUi.rateIndex + 1, juce::dontSendNotification);
      if (exportLoops_.getSelectedId() != e.exportUi.loopsIndex + 1) exportLoops_.setSelectedId(e.exportUi.loopsIndex + 1, juce::dontSendNotification);
      break;
    }
    case Change::Flags:
      locate_.setEnabled(model_.sourceMissing());
      updateExportButtons();
      break;
    default:
      break;
  }
}

void FeaturePanel::updateExportButtons() {
  const auto& v = model_.view();
  const bool canExport = v.hasPattern && !model_.sourceMissing();  // exports render the source audio
  for (juce::Button* b : std::initializer_list<juce::Button*>{&exportWav_, &exportKit_}) b->setEnabled(canExport && !model_.exportRunning());  // one export at a time
  dragMidi_.setEnabled(canExport);
  dragWav_.setEnabled(canExport);
}

void FeaturePanel::resized() {
  auto area = getLocalBounds();
  auto row1 = area.removeFromTop(26);
  fmtLabel_.setBounds(row1.removeFromLeft(50));
  exportFormat_.setBounds(row1.removeFromLeft(110).reduced(2));
  exportRate_.setBounds(row1.removeFromLeft(90).reduced(2));
  loopsLabel_.setBounds(row1.removeFromLeft(50));
  exportLoops_.setBounds(row1.removeFromLeft(56).reduced(2));
  auto row2 = area.removeFromTop(26);
  exportWav_.setBounds(row2.removeFromLeft(110).reduced(2));
  exportKit_.setBounds(row2.removeFromLeft(110).reduced(2));
  locate_.setBounds(row2.removeFromLeft(110).reduced(2));
  detectMode_.setBounds(row2.removeFromLeft(190).reduced(2));
  auto row3 = area.removeFromTop(26);
  preset_.setBounds(row3.removeFromLeft(190).reduced(2));
  savePreset_.setBounds(row3.removeFromLeft(90).reduced(2));
  deletePreset_.setBounds(row3.removeFromLeft(70).reduced(2));
  row3.removeFromLeft(16);
  role_.setBounds(row3.removeFromLeft(110).reduced(2));
  setRole_.setBounds(row3.removeFromLeft(80).reduced(2));
  auto row4 = area.removeFromTop(26);
  dragMidi_.setBounds(row4.removeFromLeft(100).reduced(2));
  dragWav_.setBounds(row4.removeFromLeft(100).reduced(2));
}

// ---------------------------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------------------------

cf::presets::Preset FeaturePanel::currentPreset(const juce::String& name) const { return cmd_.currentPreset(name, fractalUi(), evolveUi()); }

void FeaturePanel::applyPreset(const cf::presets::Preset& p) {
  cmd_.applyPresetParams(p);
  if (evolve_) evolve_->showPreset(p);
  model_.setExtras([&p](cf::composition::ProjectExtras& e) {  // the widgets were set silently, so the persisted copy follows here
    e.motif = p.motif;
    e.fractalDepth = std::max(1, std::min(3, p.depth));
    e.fractalMutation = p.mutation;
    e.fractalAccentDecay = p.accentDecay;
  });
  cmd_.say("Preset \"" + juce::String(p.name) + "\" loaded; press Generate to use it.");
}

void FeaturePanel::reloadPresets() {
  int skipped = 0;
  presets_ = PresetManager::list(&skipped);
  preset_.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < presets_.size(); ++i) preset_.addItem(presets_[i].label, static_cast<int>(i) + 1);
  if (skipped > 0) cmd_.say(juce::String(skipped) + " preset file(s) could not be read and were skipped.");
}

void FeaturePanel::savePresetNamed(const juce::String& name, bool overwrite) {
  cmd_.savePreset(currentPreset(name.trim()), overwrite);
  reloadPresets();
}

void FeaturePanel::askSavePreset() {
  auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  auto* w = new juce::AlertWindow("Save preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon, editor);
  w->addTextEditor("name", "My preset");
  w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  juce::Component::SafePointer<FeaturePanel> self(this);
  juce::Component::SafePointer<juce::Component> owner(editor);
  w->enterModalState(true, juce::ModalCallbackFunction::create([self, w, owner](int result) {
    if (result != 1 || !self) return;
    const juce::String name = w->getTextEditorContents("name").trim();
    if (name.isEmpty()) return;
    bool exists = false;
    for (const auto& e : self->presets_) exists = exists || (!e.factory && e.label.equalsIgnoreCase(name));
    if (!exists) {
      self->savePresetNamed(name, false);
      return;
    }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Replace preset?").withMessage("A preset named \"" + name + "\" exists. Replace it?").withButton("Replace").withButton("Cancel").withAssociatedComponent(owner.getComponent()),
                                 [self, name](int r) {
                                   if (r == 1 && self) self->savePresetNamed(name, true);
                                 });
  }), true);
}

void FeaturePanel::deleteSelectedPreset() {
  const int i = preset_.getSelectedId() - 1;
  if (i < 0 || i >= static_cast<int>(presets_.size()) || presets_[static_cast<std::size_t>(i)].factory) return;
  cmd_.deletePreset(presets_[static_cast<std::size_t>(i)].file);
  reloadPresets();
  deletePreset_.setEnabled(false);
}
