#include "FeaturePanel.h"

#include <algorithm>
#include <initializer_list>

namespace cf = chopfractal;

FeaturePanel::FeaturePanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "FeaturePanel") {
  for (juce::Button* b : std::initializer_list<juce::Button*>{&smart_, &acceptRoles_, &useTempo_, &fractal_, &keep_, &exportWav_, &exportKit_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  acceptRoles_.setEnabled(false);
  useTempo_.setEnabled(false);
  smart_.onClick = [this] { cmd_.smartSetup(); };
  acceptRoles_.onClick = [this] { cmd_.acceptRoles(); };
  useTempo_.onClick = [this] { cmd_.useTempo(); };
  fractal_.onClick = [this] { cmd_.applyFractal(fractalUi()); };
  keep_.onClick = [this] { cmd_.keepEvolved(); };
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

  for (juce::Button* b : std::initializer_list<juce::Button*>{&storeA_, &recallA_, &storeB_, &recallB_, &setRole_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  for (std::size_t i = 0; i < 2; ++i) {
    juce::Button* store = i == 0 ? &storeA_ : &storeB_;
    juce::Button* recall = i == 0 ? &recallA_ : &recallB_;
    store->onClick = [this, i] { cmd_.storeSnapshot(i); };
    recall->onClick = [this, i] { cmd_.recallSnapshot(i); };
  }
  for (const auto& r : cf::roles::builtinRoles()) role_.addItem(juce::String(r), role_.getNumItems() + 1);
  role_.setSelectedId(model_.extras().roleIndex + 1, juce::dontSendNotification);
  role_.setTitle("Role for the selected hit's chop");
  role_.onChange = [this] { model_.setExtras([this](cf::composition::ProjectExtras& e) { e.roleIndex = role_.getSelectedId() - 1; }); };
  addAndMakeVisible(role_);
  setRole_.onClick = [this] { cmd_.setRoleOfSelected(role_.getText()); };

  motifLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(motifLabel_);
  motif_.setText(juce::String(model_.extras().motif), juce::dontSendNotification);
  motif_.setInputRestrictions(8, "xa.");
  motif_.onTextChange = [this] { model_.setExtras([this](cf::composition::ProjectExtras& e) { e.motif = motif_.getText().toStdString(); }); };
  motif_.setTitle("Fractal motif: x hit, a accent, dot rest");
  addAndMakeVisible(motif_);
  depthLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(depthLabel_);
  depth_.addItemList({"1", "2", "3"}, 1);
  depth_.setSelectedId(model_.extras().fractalDepth, juce::dontSendNotification);
  depth_.onChange = [this] { model_.setExtras([this](cf::composition::ProjectExtras& e) { e.fractalDepth = depth_.getSelectedId(); }); };
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

  dragMidi_.setTooltip("Drag the current pattern into your DAW as a MIDI file");
  dragWav_.setTooltip("Drag the current pattern into your DAW as a WAV file");
  addAndMakeVisible(dragMidi_);
  addAndMakeVisible(dragWav_);
  // Dropped files are not auto-deleted: DAWs commonly link them in place, so removing them would break projects.
}

FractalUi FeaturePanel::fractalUi() const {
  FractalUi f;
  f.motif = motif_.getText().toStdString();
  f.depth = depth_.getSelectedId();
  f.mutation = model_.extras().fractalMutation;
  f.accent = model_.extras().fractalAccentDecay;
  return f;
}

EvolveUi FeaturePanel::evolveUi() const { return {evolve_.getToggleState(), every_.getSelectedId() - 1, evolveAmount_.getValue()}; }

ExportUi FeaturePanel::exportUi() const { return {exportFormat_.getSelectedId() - 1, exportRate_.getSelectedId() - 1, exportLoops_.getSelectedId() - 1}; }

void FeaturePanel::setEvolveEnabled(bool on) {
  evolve_.setToggleState(on, juce::dontSendNotification);
  pushEvolve();
}

void FeaturePanel::pushEvolve() {
  const EvolveUi ui = evolveUi();
  const bool running = cmd_.setEvolve(ui, pushedEvolve_);
  if (ui.enabled && !running) evolve_.setToggleState(false, juce::dontSendNotification);  // refused (for example no pattern yet)
}

void FeaturePanel::modelChanged(Change c) {
  switch (c) {
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
      for (juce::Button* b : std::initializer_list<juce::Button*>{&smart_}) b->setEnabled(v.hasSource);
      fractal_.setEnabled(v.hasPattern);
      updateExportButtons();
      break;
    }
    case Change::Extras: {  // a project was restored: show its saved choices without firing the widgets' own handlers
      const auto& e = model_.extras();
      if (motif_.getText().toStdString() != e.motif) motif_.setText(juce::String(e.motif), juce::dontSendNotification);
      if (depth_.getSelectedId() != e.fractalDepth) depth_.setSelectedId(e.fractalDepth, juce::dontSendNotification);
      if (role_.getSelectedId() != e.roleIndex + 1) role_.setSelectedId(e.roleIndex + 1, juce::dontSendNotification);
      if (detectMode_.getSelectedId() != e.detectMode + 1) detectMode_.setSelectedId(e.detectMode + 1, juce::dontSendNotification);
      if (exportFormat_.getSelectedId() != e.exportUi.formatIndex + 1) exportFormat_.setSelectedId(e.exportUi.formatIndex + 1, juce::dontSendNotification);
      if (exportRate_.getSelectedId() != e.exportUi.rateIndex + 1) exportRate_.setSelectedId(e.exportUi.rateIndex + 1, juce::dontSendNotification);
      if (exportLoops_.getSelectedId() != e.exportUi.loopsIndex + 1) exportLoops_.setSelectedId(e.exportUi.loopsIndex + 1, juce::dontSendNotification);
      break;
    }
    case Change::Setup:
      acceptRoles_.setEnabled(model_.haveSetup() && !model_.setup().roles.empty());
      useTempo_.setEnabled(model_.haveSetup() && !model_.setup().loops.empty());
      break;
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
  role_.setBounds(featD.removeFromLeft(110).reduced(2));
  setRole_.setBounds(featD.removeFromLeft(80).reduced(2));
  featD.removeFromLeft(16);
  dragMidi_.setBounds(featD.removeFromLeft(100).reduced(2));
  dragWav_.setBounds(featD.removeFromLeft(100).reduced(2));
}

// ---------------------------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------------------------

cf::presets::Preset FeaturePanel::currentPreset(const juce::String& name) const { return cmd_.currentPreset(name, fractalUi(), evolveUi()); }

bool FeaturePanel::applyPreset(const cf::presets::Preset& p, bool generateAfter) {
  if (evolve_.getToggleState()) {  // a running Evolve would pick up the new cadence mid-loop: stop it first
    setEvolveEnabled(false);
    cmd_.say("Evolve stopped to apply the preset.");
  }
  const int prevEvery = every_.getSelectedId() - 1;
  const double prevAmount = evolveAmount_.getValue();
  if (!cmd_.applyPreset(p, generateAfter)) return false;
  undoEvery_ = prevEvery;
  undoAmount_ = prevAmount;
  setEvolveValues(p.evolveEvery, p.evolveAmount);  // the motif and depth widgets follow the Extras change
  return true;
}

void FeaturePanel::setEvolveValues(int everyLoops, double amount) {
  static const int kEvery[] = {1, 2, 4, 8, 16};
  for (int i = 0; i < 5; ++i)
    if (kEvery[i] == everyLoops) every_.setSelectedId(i + 1, juce::dontSendNotification);
  evolveAmount_.setValue(amount, juce::dontSendNotification);
}

bool FeaturePanel::undoPreset() {
  if (!cmd_.undoPreset()) return false;
  every_.setSelectedId(undoEvery_ + 1, juce::dontSendNotification);
  evolveAmount_.setValue(undoAmount_, juce::dontSendNotification);
  return true;
}
