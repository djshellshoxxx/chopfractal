#include "PluginEditor.h"
#include "ControlHelp.h"
#include "EditorTheme.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <cmath>

namespace cf = chopfractal;

using editor_theme::laneColour;

ChopFractalEditor::ChopFractalEditor(ChopFractalProcessor& processor) : AudioProcessorEditor(&processor), proc_(processor), model_(processor), commands_(processor, model_), controls_(model_, commands_, processor.apvts), status_(model_, commands_), orbit_(model_, commands_), history_(model_, commands_), pattern_(model_, commands_), wave_(model_, commands_) {
  model_.addListener(this);
  setSize(940, 912);
  setResizable(false, false);

  addAndMakeVisible(controls_);
  addAndMakeVisible(status_);
  addAndMakeVisible(orbit_);
  addAndMakeVisible(history_);
  addAndMakeVisible(pattern_);
  addAndMakeVisible(wave_);
  detectMode_.addItemList({"Transients (replace)", "Transients (keep my markers)", "Even grid 1/8", "Even grid 1/16"}, 1);
  detectMode_.setSelectedId(1);
  detectMode_.setTitle("Detection mode");
  detectMode_.onChange = [this] { model_.setDetectMode(detectMode_.getSelectedId()); };
  addAndMakeVisible(detectMode_);
  locate_.onClick = [this] { commands_.locateSource(); };
  addAndMakeVisible(locate_);
  locate_.setEnabled(false);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&reroll_, &savePreset_, &deletePreset_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  reroll_.onClick = [this] { commands_.reroll(); };
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
  setWantsKeyboardFocus(true);

  for (juce::Button* b : std::initializer_list<juce::Button*>{&smart_, &acceptRoles_, &useTempo_, &fractal_, &keep_, &exportWav_, &exportKit_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  acceptRoles_.setEnabled(false);
  useTempo_.setEnabled(false);
  smart_.onClick = [this] { commands_.smartSetup(); };
  acceptRoles_.onClick = [this] { commands_.acceptRoles(); };
  useTempo_.onClick = [this] { commands_.useTempo(); };
  fractal_.onClick = [this] { commands_.applyFractal(fractalUi()); };
  keep_.onClick = [this] { commands_.keepEvolved(); };
  exportWav_.onClick = [this] { commands_.exportWav(exportUi()); };
  exportKit_.onClick = [this] { commands_.exportKit(); };

  for (juce::Button* b : std::initializer_list<juce::Button*>{&storeA_, &recallA_, &storeB_, &recallB_, &setRole_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  for (std::size_t i = 0; i < 2; ++i) {
    juce::Button* store = i == 0 ? &storeA_ : &storeB_;
    juce::Button* recall = i == 0 ? &recallA_ : &recallB_;
    store->onClick = [this, i] { commands_.storeSnapshot(i); };
    recall->onClick = [this, i] { commands_.recallSnapshot(i); };
  }
  for (const auto& r : cf::roles::builtinRoles()) role_.addItem(juce::String(r), role_.getNumItems() + 1);
  role_.setSelectedId(1);
  role_.setTitle("Role for the selected hit's chop");
  addAndMakeVisible(role_);
  setRole_.onClick = [this] { commands_.setRoleOfSelected(role_.getText()); };

  motifLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(motifLabel_);
  motif_.setText("x.xx");
  motif_.setInputRestrictions(8, "xa.");
  motif_.setTitle("Fractal motif: x hit, a accent, dot rest");
  addAndMakeVisible(motif_);
  depthLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(depthLabel_);
  depth_.addItemList({"1", "2", "3"}, 1);
  depth_.setSelectedId(2);
  depth_.setTitle("Fractal depth");
  addAndMakeVisible(depth_);

  evolve_.onClick = [this] { pushEvolve(); };
  addAndMakeVisible(evolve_);
  everyLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(everyLabel_);
  every_.addItemList({"1 loop", "2 loops", "4 loops", "8 loops", "16 loops"}, 1);
  every_.setSelectedId(3);
  every_.setTitle("Evolve every");
  every_.onChange = [this] {
    if (evolve_.getToggleState()) pushEvolve();
  };
  addAndMakeVisible(every_);
  evolveAmount_.setSliderStyle(juce::Slider::LinearHorizontal);
  evolveAmount_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
  evolveAmount_.setRange(0.05, 1.0, 0.01);
  evolveAmount_.setValue(0.25, juce::dontSendNotification);
  evolveAmount_.setTitle("Evolve amount");
  evolveAmount_.onDragEnd = [this] {
    if (evolve_.getToggleState()) pushEvolve();
  };
  addAndMakeVisible(evolveAmount_);

  fmtLabel_.setJustificationType(juce::Justification::centredRight);
  loopsLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(fmtLabel_);
  addAndMakeVisible(loopsLabel_);
  exportFormat_.addItemList({"16-bit", "24-bit", "32-bit float"}, 1);
  exportFormat_.setSelectedId(2);
  exportFormat_.setTitle("Export format");
  exportRate_.addItemList({"44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"}, 1);
  exportRate_.setSelectedId(2);
  exportRate_.setTitle("Export sample rate");
  exportLoops_.addItemList({"1", "2", "4", "8", "16"}, 1);
  exportLoops_.setSelectedId(2);
  exportLoops_.setTitle("Export loops");
  for (juce::ComboBox* c : std::initializer_list<juce::ComboBox*>{&exportFormat_, &exportRate_, &exportLoops_}) addAndMakeVisible(*c);

  control_help::applyToTree(*this);
  refresh(true);
  startTimerHz(10);
}

ChopFractalEditor::~ChopFractalEditor() {
  stopTimer();
  model_.removeListener(this);
}

juce::String ChopFractalEditor::getTooltip() {
  const auto point = getMouseXYRelative();
  if (wave_.getBounds().contains(point))
    return "Source waveform: click a chop to select and audition it; drag across sections to select several. Shift-click adds a marker; Alt-click removes the nearest marker. Drop a sample anywhere to load it.";
  if (pattern_.getBounds().contains(point))
    return "Pattern grid: each lane is a chop. Click a hit or drag a rectangle to select sections. Zoom In subdivides selected hits. M toggles selected hits; Delete removes them. Enable Move / resize hits to edit by dragging.";
  if (history_.getBounds().contains(point))
    return "Variation family tree: click a node to recall it, at the next loop boundary during playback. Right-click for favorite, rename, compare and delete actions. Each Generate, Mutate, Zoom and completed edit adds a branch.";
  if (orbit_.getBounds().contains(point))
    return "Orbit view: concentric rings show chop lanes and their rhythmic positions. The moving line is the playhead. Click a hit to select it in the pattern.";
  return "ChopFractal: load or drop a loop, Detect Chops, Generate and Play. Hover controls for instructions.";
}

void ChopFractalEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  controls_.setBounds(area.removeFromTop(232));  // transport 36, buttons 30, gap 6, seed row 26, gap 6, parameter block 128
  area.removeFromTop(6);
  auto featA = area.removeFromTop(26);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&smart_, &acceptRoles_, &useTempo_}) b->setBounds(featA.removeFromLeft(104).reduced(2));
  motifLabel_.setBounds(featA.removeFromLeft(50));
  motif_.setBounds(featA.removeFromLeft(90).reduced(2));
  depthLabel_.setBounds(featA.removeFromLeft(50));
  depth_.setBounds(featA.removeFromLeft(56).reduced(2));
  fractal_.setBounds(featA.removeFromLeft(90).reduced(2));
  auto featB = area.removeFromTop(26);
  evolve_.setBounds(featB.removeFromLeft(90));
  everyLabel_.setBounds(featB.removeFromLeft(44));
  every_.setBounds(featB.removeFromLeft(90).reduced(2));
  evolveAmount_.setBounds(featB.removeFromLeft(190));
  keep_.setBounds(featB.removeFromLeft(80).reduced(2));
  featB.removeFromLeft(20);
  fmtLabel_.setBounds(featB.removeFromLeft(50));
  exportFormat_.setBounds(featB.removeFromLeft(110).reduced(2));
  exportRate_.setBounds(featB.removeFromLeft(90).reduced(2));
  loopsLabel_.setBounds(featB.removeFromLeft(50));
  exportLoops_.setBounds(featB.removeFromLeft(56).reduced(2));
  auto featC = area.removeFromTop(26);
  exportWav_.setBounds(featC.removeFromLeft(110).reduced(2));
  exportKit_.setBounds(featC.removeFromLeft(110).reduced(2));
  locate_.setBounds(featC.removeFromLeft(110).reduced(2));
  detectMode_.setBounds(featC.removeFromLeft(190).reduced(2));
  featC.removeFromLeft(16);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&storeA_, &recallA_, &storeB_, &recallB_}) b->setBounds(featC.removeFromLeft(78).reduced(2));
  auto featD = area.removeFromTop(26);
  preset_.setBounds(featD.removeFromLeft(190).reduced(2));
  savePreset_.setBounds(featD.removeFromLeft(90).reduced(2));
  deletePreset_.setBounds(featD.removeFromLeft(70).reduced(2));
  reroll_.setBounds(featD.removeFromLeft(80).reduced(2));
  featD.removeFromLeft(16);
  role_.setBounds(featD.removeFromLeft(110).reduced(2));
  setRole_.setBounds(featD.removeFromLeft(80).reduced(2));
  featD.removeFromLeft(16);
  dragMidi_.setBounds(featD.removeFromLeft(100).reduced(2));
  dragWav_.setBounds(featD.removeFromLeft(100).reduced(2));
  area.removeFromTop(6);
  wave_.setBounds(area.removeFromTop(120));
  area.removeFromTop(6);
  pattern_.setBounds(area.removeFromTop(230));
  area.removeFromTop(6);
  status_.setBounds(area.removeFromBottom(74).reduced(0, 2));
  auto bottom = area.removeFromTop(area.getHeight() - 2);
  orbit_.setBounds(bottom.removeFromRight(std::min(bottom.getHeight(), bottom.getWidth() / 2)));
  history_.setBounds(bottom.withTrimmedRight(6));
}

// ---------------------------------------------------------------------------------------------
// state and actions
// ---------------------------------------------------------------------------------------------

bool ChopFractalEditor::isInterestedInFileDrag(const juce::StringArray& files) {
  for (const auto& path : files)
    if (EditorCommands::isAudioFile(juce::File(path))) return true;
  return false;
}

void ChopFractalEditor::refresh(bool force) {
  if (auto* root = getTopLevelComponent(); root != tooltipRoot_) {
    tooltipRoot_ = root;
    // Include the standalone wrapper's Options and feedback-notice Settings controls.
    control_help::applyToTree(*root);
  }
  const bool rebuilt = model_.refresh(force, wave_.getWidth());
  const bool playing = proc_.playheadQuarters() >= 0.0;
  if (force || rebuilt) repaint();
  else if (playing || lastPlaying_) {  // only the playhead views change while the transport runs
    orbit_.repaint();
  }
  lastPlaying_ = playing;
}

// The widget side of what the model publishes: the old refresh() did all of this inline.
void ChopFractalEditor::modelChanged(EditorModel::Change change) {
  using Change = EditorModel::Change;
  switch (change) {
    case Change::Evolve: {
      const bool running = model_.evolveRunning();
      if (evolve_.getToggleState() != running) evolve_.setToggleState(running, juce::dontSendNotification);
      const auto& se = model_.evolve();
      if (!running && (se.everyLoops != pushedEvolve_.everyLoops || se.amount != pushedEvolve_.amount || se.startSeed != pushedEvolve_.startSeed)) {
        pushedEvolve_ = se;
        static const int kEvery[] = {1, 2, 4, 8, 16};
        for (int i = 0; i < 5; ++i)
          if (kEvery[i] == se.everyLoops && every_.getSelectedId() != i + 1) every_.setSelectedId(i + 1, juce::dontSendNotification);
        evolveAmount_.setValue(se.amount, juce::dontSendNotification);
      }
      break;
    }
    case Change::View: {
      const auto& v = model_.view();
      smart_.setEnabled(v.hasSource);
      fractal_.setEnabled(v.hasPattern);
      const bool canExport = v.hasPattern && !model_.sourceMissing();  // exports render the source audio
      for (juce::Button* b : std::initializer_list<juce::Button*>{&exportWav_, &exportKit_}) b->setEnabled(canExport);
      dragMidi_.setEnabled(canExport);
      dragWav_.setEnabled(canExport);
      break;
    }
    case Change::Setup:
      acceptRoles_.setEnabled(model_.haveSetup() && !model_.setup().roles.empty());
      useTempo_.setEnabled(model_.haveSetup() && !model_.setup().loops.empty());
      break;
    case Change::Flags:
      locate_.setEnabled(model_.sourceMissing());
      break;
    case Change::Selection:
      repaint();
      break;
    default:
      break;
  }
}

void ChopFractalEditor::paint(juce::Graphics& g) { g.fillAll(editor_theme::background()); }

// ---------------------------------------------------------------------------------------------
// direct hit editing, presets, chaos reroll, drag-out
// ---------------------------------------------------------------------------------------------

void ChopFractalEditor::reloadPresets() {
  int skipped = 0;
  presets_ = PresetManager::list(&skipped);
  preset_.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < presets_.size(); ++i) preset_.addItem(presets_[i].label, static_cast<int>(i) + 1);
  if (skipped > 0) {
    commands_.say(juce::String(skipped) + " preset file(s) could not be read and were skipped.");
  }
}

void ChopFractalEditor::askSavePreset() {
  auto* w = new juce::AlertWindow("Save preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon, this);
  w->addTextEditor("name", "My preset");
  w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  juce::Component::SafePointer<ChopFractalEditor> self(this);
  w->enterModalState(true, juce::ModalCallbackFunction::create([self, w](int result) {
    if (result != 1 || !self) return;
    const juce::String name = w->getTextEditorContents("name").trim();
    if (name.isEmpty()) return;
    bool exists = false;
    for (const auto& e : self->presets_) exists = exists || (!e.factory && e.label.equalsIgnoreCase(name));
    if (!exists) {
      self->savePresetNamed(name, false);
      return;
    }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Replace preset?").withMessage("A preset named \"" + name + "\" exists. Replace it?").withButton("Replace").withButton("Cancel").withAssociatedComponent(self.getComponent()),
                                 [self, name](int r) {
                                   if (r == 1 && self) self->savePresetNamed(name, true);
                                 });
  }), true);
}

// Keyboard equivalent of the drag gestures: arrows nudge the selected hit by one grid cell, Shift+arrows resize it.

void ChopFractalEditor::filesDropped(const juce::StringArray& files, int, int) { commands_.handleDroppedFiles(files); }

// ---------------------------------------------------------------------------------------------
// widget state handed to the command layer
// ---------------------------------------------------------------------------------------------

FractalUi ChopFractalEditor::fractalUi() const {
  FractalUi f;
  f.motif = motif_.getText().toStdString();
  f.depth = depth_.getSelectedId();
  f.mutation = fractalMutation_;
  f.accent = fractalAccent_;
  return f;
}

EvolveUi ChopFractalEditor::evolveUi() const { return {evolve_.getToggleState(), every_.getSelectedId() - 1, evolveAmount_.getValue()}; }

ExportUi ChopFractalEditor::exportUi() const { return {exportFormat_.getSelectedId() - 1, exportRate_.getSelectedId() - 1, exportLoops_.getSelectedId() - 1}; }

void ChopFractalEditor::pushEvolve() {
  const EvolveUi ui = evolveUi();
  const bool running = commands_.setEvolve(ui, pushedEvolve_);
  if (ui.enabled && !running) evolve_.setToggleState(false, juce::dontSendNotification);  // refused (for example no pattern yet)
}

bool ChopFractalEditor::keyPressed(const juce::KeyPress& k) {
  if (!model_.hasSelection()) return false;
  const auto mods = k.getModifiers();
  if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;  // leave the host's own shortcuts alone
  if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) {
    commands_.nudgeSelected(k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1, mods.isShiftDown());
    return true;
  }
  if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
    commands_.deleteSelected();
    return true;
  }
  if (k.getTextCharacter() == 'm' || k.getTextCharacter() == 'M') {
    commands_.toggleMuteSelected();
    return true;
  }
  if (k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D') {
    commands_.duplicateSelected();
    return true;
  }
  return false;
}

void ChopFractalEditor::doReroll() { commands_.reroll(); }

cf::presets::Preset ChopFractalEditor::currentPreset(const juce::String& name) const { return commands_.currentPreset(name, fractalUi(), evolveUi()); }

void ChopFractalEditor::applyPreset(const cf::presets::Preset& p) {
  commands_.applyPresetParams(p);
  motif_.setText(juce::String(p.motif), juce::dontSendNotification);
  depth_.setSelectedId(std::max(1, std::min(3, p.depth)), juce::dontSendNotification);
  fractalMutation_ = p.mutation;
  fractalAccent_ = p.accentDecay;
  static const int kEvery[] = {1, 2, 4, 8, 16};
  for (int i = 0; i < 5; ++i)
    if (kEvery[i] == p.evolveEvery) every_.setSelectedId(i + 1, juce::dontSendNotification);
  evolveAmount_.setValue(p.evolveAmount, juce::dontSendNotification);
  commands_.say("Preset \"" + juce::String(p.name) + "\" loaded; press Generate to use it.");
}

void ChopFractalEditor::savePresetNamed(const juce::String& name, bool overwrite) {
  commands_.savePreset(currentPreset(name.trim()), overwrite);
  reloadPresets();
}

void ChopFractalEditor::deleteSelectedPreset() {
  const int i = preset_.getSelectedId() - 1;
  if (i < 0 || i >= static_cast<int>(presets_.size()) || presets_[static_cast<std::size_t>(i)].factory) return;
  commands_.deletePreset(presets_[static_cast<std::size_t>(i)].file);
  reloadPresets();
  deletePreset_.setEnabled(false);
}

juce::File ChopFractalEditor::makeDragFile(bool wav) { return commands_.makeDragFile(wav, exportUi()); }
