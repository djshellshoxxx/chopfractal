#include "PluginEditor.h"
#include "ControlHelp.h"
#include "EditorTheme.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <cmath>

namespace cf = chopfractal;

using editor_theme::laneColour;

ChopFractalEditor::ChopFractalEditor(ChopFractalProcessor& processor) : AudioProcessorEditor(&processor), proc_(processor), model_(processor), commands_(processor, model_), status_(model_, commands_) {
  model_.addListener(this);
  setSize(940, 912);
  setResizable(false, false);

  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &generate_, &mutate_, &undo_, &redo_, &zoom_, &collapse_, &lock_, &dice_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  addAndMakeVisible(play_);
  addAndMakeVisible(stop_);
  addAndMakeVisible(followHost_);
  addAndMakeVisible(transportHint_);
  addAndMakeVisible(editMode_);
  editMode_.onClick = [this] { model_.setEditMode(editMode_.getToggleState()); };
  editMode_.setTooltip("Off: drag to select sections. On: drag a hit to move or resize it.");
  play_.setTitle("Play");
  stop_.setTitle("Stop");
  play_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff228b44));
  play_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  stop_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffc0392b));
  stop_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  play_.setTooltip("Play the generated pattern from the beginning, including in the standalone app.");
  stop_.setTooltip("Stop local pattern playback. Press Play to restart from the beginning.");
  play_.onClick = [this] { commands_.play(); };
  stop_.onClick = [this] { commands_.stop(); };
  followHost_.onClick = [this] { commands_.followHost(); };
  addAndMakeVisible(status_);
  load_.onClick = [this] { commands_.chooseFile(); };
  detect_.onClick = [this] { commands_.detectChops(static_cast<DetectChoice>(detectMode_.getSelectedId())); };
  detectMode_.addItemList({"Transients (replace)", "Transients (keep my markers)", "Even grid 1/8", "Even grid 1/16"}, 1);
  detectMode_.setSelectedId(1);
  detectMode_.setTitle("Detection mode");
  addAndMakeVisible(detectMode_);
  locate_.onClick = [this] { commands_.locateSource(); };
  addAndMakeVisible(locate_);
  locate_.setEnabled(false);
  generate_.onClick = [this] { commands_.generate(); };
  mutate_.onClick = [this] { commands_.mutate(); };
  undo_.onClick = [this] { commands_.undo(); };
  redo_.onClick = [this] { commands_.redo(); };
  zoom_.setTooltip("Subdivide the selected pattern hits into nested rhythms. Select hits or waveform sections first.");
  zoom_.onClick = [this] { commands_.zoomInSelected(); };
  collapse_.onClick = [this] { commands_.collapseSelected(); };
  lock_.onClick = [this] { commands_.toggleLockSelectedBar(); };
  dice_.onClick = [this] { commands_.newSeed(); };

  embed_.setToggleState(model_.embedSource(), juce::dontSendNotification);
  embed_.onClick = [this] { model_.setEmbedSource(embed_.getToggleState()); };
  addAndMakeVisible(embed_);

  seedLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(seedLabel_);
  seed_.setText("1");
  seed_.setInputRestrictions(12, "0123456789");
  seed_.onTextChange = [this] { model_.setSeed(static_cast<std::uint64_t>(seed_.getText().getLargeIntValue())); };
  seed_.setTitle("Seed");
  addAndMakeVisible(seed_);

  addSlider(density_, "density", "Density");
  addSlider(swing_, "swing", "Swing");
  addSlider(variation_, "variation_amount", "Variation");
  addSlider(dry_, "dry_mix", "Dry mix");
  addSlider(gain_, "output_gain_db", "Output gain (dB)");

  bars_.addItemList(juce::StringArray::fromTokens(cf::host::parameterManifest()[cf::host::kPatternBars].choices, "|", ""), 1);
  bars_.setTitle("Pattern length in bars");
  addAndMakeVisible(bars_);
  barsAttachment_ = std::make_unique<Attachment::ComboBoxAttachment>(proc_.apvts, "pattern_bars", bars_);
  barsLabel_.attachToComponent(&bars_, true);

  addToggle(enable_, "effect_enable", "Effect on");
  addToggle(reverse_, "allow_reverse", "Reverse");
  addToggle(pitch_, "allow_pitch", "Pitch");
  addToggle(retrigger_, "allow_retrigger", "Retrigger");

  addToggle(filter_, "allow_filter", "Filter");
  addToggle(glide_, "allow_glide", "Tape glide");
  addToggle(crunch_, "allow_crunch", "Crunch");
  addSlider(fxIntensity_, "fx_intensity", "FX intensity");
  addSlider(chaos_, "chaos", "Chaos");
  chaos_.slider.setTooltip("One control that pushes variation, density, transforms and effects up together");
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
  if (waveArea_.contains(point))
    return "Source waveform: click a chop to select and audition it; drag across sections to select several. Shift-click adds a marker; Alt-click removes the nearest marker. Drop a sample anywhere to load it.";
  if (patternArea_.contains(point))
    return "Pattern grid: each lane is a chop. Click a hit or drag a rectangle to select sections. Zoom In subdivides selected hits. M toggles selected hits; Delete removes them. Enable Move / resize hits to edit by dragging.";
  if (historyArea_.contains(point))
    return "Variation family tree: click a node to recall it, at the next loop boundary during playback. Right-click for favorite, rename, compare and delete actions. Each Generate, Mutate, Zoom and completed edit adds a branch.";
  if (orbitArea_.contains(point))
    return "Orbit view: concentric rings show chop lanes and their rhythmic positions. The moving line is the playhead. Click a hit to select it in the pattern.";
  return "ChopFractal: load or drop a loop, Detect Chops, Generate and Play. Hover controls for instructions.";
}

void ChopFractalEditor::addSlider(ParamSlider& s, const char* paramId, const juce::String& title) {
  s.slider.setSliderStyle(juce::Slider::LinearHorizontal);
  s.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
  s.slider.setNumDecimalPlacesToDisplay(2);
  s.slider.setTitle(title);
  addAndMakeVisible(s.slider);
  s.label.setText(title, juce::dontSendNotification);
  s.label.attachToComponent(&s.slider, true);
  s.attachment = std::make_unique<Attachment::SliderAttachment>(proc_.apvts, paramId, s.slider);
}

void ChopFractalEditor::addToggle(ParamToggle& t, const char* paramId, const juce::String& title) {
  t.button.setButtonText(title);
  t.button.setTitle(title);
  addAndMakeVisible(t.button);
  t.attachment = std::make_unique<Attachment::ButtonAttachment>(proc_.apvts, paramId, t.button);
}

void ChopFractalEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  auto transport = area.removeFromTop(36);
  play_.setBounds(transport.removeFromLeft(104).reduced(2));
  stop_.setBounds(transport.removeFromLeft(104).reduced(2));
  followHost_.setBounds(transport.removeFromLeft(100).reduced(2));
  editMode_.setBounds(transport.removeFromRight(180).reduced(2));
  transportHint_.setBounds(transport.reduced(8, 2));
  auto row1 = area.removeFromTop(30);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &generate_, &mutate_, &undo_, &redo_, &zoom_, &collapse_, &lock_})
    b->setBounds(row1.removeFromLeft(96).reduced(2));
  area.removeFromTop(6);
  auto row2 = area.removeFromTop(26);
  seedLabel_.setBounds(row2.removeFromLeft(50));
  seed_.setBounds(row2.removeFromLeft(110).reduced(2));
  dice_.setBounds(row2.removeFromLeft(90).reduced(2));
  embed_.setBounds(row2.removeFromLeft(210).reduced(2));
  bars_.setBounds(row2.removeFromRight(90).reduced(2));
  area.removeFromTop(6);
  auto sliders = area.removeFromTop(128);
  auto left = sliders.removeFromLeft(sliders.getWidth() / 2);
  density_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  swing_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  variation_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  fxIntensity_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  chaos_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  dry_.slider.setBounds(sliders.removeFromTop(24).withTrimmedLeft(130));
  gain_.slider.setBounds(sliders.removeFromTop(24).withTrimmedLeft(130));
  auto toggles = sliders.removeFromTop(24);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&enable_.button, &reverse_.button, &pitch_.button, &retrigger_.button}) b->setBounds(toggles.removeFromLeft(100));
  auto toggles2 = sliders.removeFromTop(24);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&filter_.button, &glide_.button, &crunch_.button}) b->setBounds(toggles2.removeFromLeft(110));
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
  waveArea_ = area.removeFromTop(120);
  area.removeFromTop(6);
  patternArea_ = area.removeFromTop(230);
  area.removeFromTop(6);
  status_.setBounds(area.removeFromBottom(74).reduced(0, 2));
  auto bottom = area.removeFromTop(area.getHeight() - 2);
  orbitArea_ = bottom.removeFromRight(std::min(bottom.getHeight(), bottom.getWidth() / 2));
  historyArea_ = bottom.withTrimmedRight(6);
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
  const bool rebuilt = model_.refresh(force, waveArea_.getWidth());
  const bool playing = proc_.playheadQuarters() >= 0.0;
  if (force || rebuilt) repaint();
  else if (playing || lastPlaying_) {  // only the playhead views change while the transport runs
    repaint(orbitArea_);
    repaint(patternArea_);
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
      for (juce::Button* b : std::initializer_list<juce::Button*>{&detect_, &generate_, &smart_}) b->setEnabled(v.hasSource);
      for (juce::Button* b : std::initializer_list<juce::Button*>{&mutate_, &fractal_}) b->setEnabled(v.hasPattern);
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
      if (embed_.getToggleState() != model_.embedSource()) embed_.setToggleState(model_.embedSource(), juce::dontSendNotification);
      break;
    case Change::Seed:
      if (static_cast<std::uint64_t>(seed_.getText().getLargeIntValue()) != model_.seed())
        seed_.setText(juce::String(static_cast<juce::int64>(model_.seed())), juce::dontSendNotification);
      break;
    case Change::Selection:
      repaint();
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------------------------
// drawing and mouse
// ---------------------------------------------------------------------------------------------

void ChopFractalEditor::paint(juce::Graphics& g) {
  g.fillAll(editor_theme::background());
  g.setColour(juce::Colours::white.withAlpha(0.85f));
  g.setFont(13.0f);

  // Source waveform with chop markers.
  g.setColour(editor_theme::canvas());
  g.fillRect(waveArea_);
  if (!model_.view().peaks.min.empty()) {
    g.setColour(editor_theme::accentBlue());
    const int n = static_cast<int>(model_.view().peaks.min.size());
    const float cy = static_cast<float>(waveArea_.getCentreY());
    const float h = static_cast<float>(waveArea_.getHeight()) * 0.45f;
    for (int i = 0; i < n; ++i)
      g.drawVerticalLine(waveArea_.getX() + i, cy - model_.view().peaks.max[static_cast<std::size_t>(i)] * h, cy - model_.view().peaks.min[static_cast<std::size_t>(i)] * h);
  } else {
    g.setColour(juce::Colours::grey);
    g.drawText(model_.sourceMissing() ? "The project's audio file was not found. Press Locate Source (your markers, pattern and history are kept)."
                              : "Drop an audio sample here, or press Load Loop. Then Detect Chops, Generate and Play.",
               waveArea_, juce::Justification::centred);
  }
  // Waveform selections correspond to complete chop sections, not destructive source trims.
  for (const auto& chop : model_.view().chops)
    if (std::find(model_.selectedChops().begin(), model_.selectedChops().end(), chop.id) != model_.selectedChops().end() && model_.view().sourceFrames > 0) {
      const float x = static_cast<float>(waveArea_.getX()) + static_cast<float>(chop.range.start) / static_cast<float>(model_.view().sourceFrames) * waveArea_.getWidth();
      const float width = static_cast<float>(chop.range.length()) / static_cast<float>(model_.view().sourceFrames) * waveArea_.getWidth();
      g.setColour(editor_theme::accentBlue().withAlpha(0.3f));
      g.fillRect(x, static_cast<float>(waveArea_.getY()), width, static_cast<float>(waveArea_.getHeight()));
    }
  int markerNumber = 0;
  for (const auto& m : model_.view().markers) {
    ++markerNumber;
    g.setColour(m.enabled ? editor_theme::accentAmber() : juce::Colours::grey);
    const int x = waveArea_.getX() + static_cast<int>(m.x);
    g.drawVerticalLine(x, static_cast<float>(waveArea_.getY()), static_cast<float>(waveArea_.getBottom()));
    g.drawText(juce::String(markerNumber), x + 2, waveArea_.getY(), 28, 14, juce::Justification::left);
  }

  // Pattern grid: lanes by chop, numbered so colour is never the only indicator.
  g.setColour(editor_theme::canvas());
  g.fillRect(patternArea_);
  const int lanes = std::max(1, static_cast<int>(model_.view().chops.size()));
  const float laneH = static_cast<float>(patternArea_.getHeight()) / static_cast<float>(lanes);
  const float w = static_cast<float>(patternArea_.getWidth());
  g.setColour(juce::Colours::white.withAlpha(0.12f));
  for (double x : model_.view().pattern.beatLines) g.drawVerticalLine(patternArea_.getX() + static_cast<int>(x * w), static_cast<float>(patternArea_.getY()), static_cast<float>(patternArea_.getBottom()));
  for (std::size_t b = 0; b < model_.view().pattern.barLines.size(); ++b) {
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    const int x = patternArea_.getX() + static_cast<int>(model_.view().pattern.barLines[b] * w);
    g.drawVerticalLine(x, static_cast<float>(patternArea_.getY()), static_cast<float>(patternArea_.getBottom()));
    if (b < model_.view().pattern.barLocked.size() && model_.view().pattern.barLocked[b]) {
      g.setColour(editor_theme::accentRed());
      g.drawText("locked", x + 3, patternArea_.getBottom() - 16, 60, 14, juce::Justification::left);
    }
  }
  for (const auto& r : model_.view().pattern.rects) {
    const float inset = static_cast<float>(r.depth) * 3.0f;
    const float y = static_cast<float>(patternArea_.getY()) + static_cast<float>(std::max(0, r.chopIndex)) * laneH + inset;
    juce::Rectangle<float> rect(static_cast<float>(patternArea_.getX()) + static_cast<float>(r.x0) * w, y, std::max(2.0f, static_cast<float>(r.x1 - r.x0) * w - 1.0f),
                                std::max(4.0f, laneH - 2.0f - 2.0f * inset));
    g.setColour(laneColour(r.chopIndex).withAlpha(r.depth > 0 ? 0.95f : 0.75f));
    g.fillRect(rect);
    if (r.id == model_.selected() || std::find(model_.selectedHits().begin(), model_.selectedHits().end(), r.id) != model_.selectedHits().end()) {
      g.setColour(juce::Colours::white);
      g.drawRect(rect, 2.0f);
    } else if (r.locked || r.hasChild) {
      g.setColour(r.locked ? editor_theme::accentRed() : juce::Colours::white);
      g.drawRect(rect, 1.0f);
    }
    if (rect.getWidth() > 14.0f && rect.getHeight() > 12.0f) {
      g.setColour(juce::Colours::black);
      g.drawText(juce::String(r.chopIndex + 1), rect.toNearestInt(), juce::Justification::centred);
    }
  }
  if (model_.view().pattern.rects.empty()) {
    g.setColour(juce::Colours::grey);
    g.drawText("Detect chops, then press Generate.", patternArea_, juce::Justification::centred);
  }

  // Variation family tree.
  g.saveState();
  g.reduceClipRegion(historyArea_);
  g.setColour(editor_theme::canvas());
  g.fillRect(historyArea_);
  for (const auto& n : model_.view().tree) {
    const auto np = treePos(n);
    const int x = np.x, y = np.y;
    for (const auto& p : model_.view().tree)
      if (p.id == n.parent) {
        g.setColour(juce::Colours::grey);
        const auto pp = treePos(p);
        g.drawLine(static_cast<float>(pp.x), static_cast<float>(pp.y), static_cast<float>(x), static_cast<float>(y));
      }
    g.setColour(n.active ? editor_theme::accentGreen() : (n.favorite ? editor_theme::accentAmber() : editor_theme::neutralGrey()));
    g.fillEllipse(static_cast<float>(x - 8), static_cast<float>(y - 8), 16.0f, 16.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.drawText(n.label.empty() ? juce::String(static_cast<juce::int64>(n.id)) : juce::String(n.label), x + 10, y - 7, 90, 14, juce::Justification::left);
  }
  if (model_.view().tree.empty()) {
    g.setColour(juce::Colours::grey);
    g.drawText("Each Generate, Mutate and Zoom is saved here as a branch you can return to.", historyArea_, juce::Justification::centred);
  }
  g.restoreState();

  // Orbit View: one ring per chop (ring 1 outermost, numbered), the playhead sweeping clockwise from 12 o'clock.
  g.setColour(editor_theme::canvas());
  g.fillRect(orbitArea_);
  if (!model_.view().orbit.arcs.empty()) {
    const auto c = orbitArea_.getCentre().toFloat();
    const double R = std::min(orbitArea_.getWidth(), orbitArea_.getHeight()) / 2.0 - 4.0;
    const double ph = proc_.playheadQuarters();
    const double posTicks = ph >= 0.0 ? ph * cf::kTicksPerQuarter : -1.0;
    auto pt = [&](double r, double a) { return juce::Point<float>(c.x + static_cast<float>(r * std::sin(a)), c.y - static_cast<float>(r * std::cos(a))); };
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    for (double a : model_.view().orbit.beatAngles) g.drawLine(juce::Line<float>(pt(R * 0.25, a), pt(R, a)), 0.5f);
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    for (double a : model_.view().orbit.barAngles) g.drawLine(juce::Line<float>(pt(R * 0.25, a), pt(R + 3.0, a)), 1.2f);
    for (const auto& a : model_.view().orbit.arcs) {
      const auto rr = cf::ui::arcRadii(model_.view().orbit, a, R);
      const double pulse = posTicks >= 0.0 ? cf::ui::hitPulse(a, posTicks, model_.view().orbit.lengthTicks) : 0.0;
      juce::Path path;
      path.addCentredArc(c.x, c.y, static_cast<float>((rr.inner + rr.outer) / 2.0), static_cast<float>((rr.inner + rr.outer) / 2.0), 0.0f,
                         static_cast<float>(a.startAngle), static_cast<float>(a.startAngle + a.sweep), true);
      g.setColour(laneColour(a.ring).withAlpha(a.childActive ? 0.35f : static_cast<float>(0.45 + 0.4 * std::min(1.0, static_cast<double>(a.level))) ).brighter(static_cast<float>(pulse)));
      g.strokePath(path, juce::PathStrokeType(static_cast<float>(std::max(2.0, rr.outer - rr.inner)), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
      if (a.locked) {
        g.setColour(editor_theme::accentRed());
        g.strokePath(path, juce::PathStrokeType(1.0f));
      }
      if (a.id == model_.selected()) {
        g.setColour(juce::Colours::white);
        g.strokePath(path, juce::PathStrokeType(2.0f));
      }
    }
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(10.0f);
    const double ringW = (R - 0.25 * R) / std::max(1, model_.view().orbit.rings);
    for (int ring = 0; ring < model_.view().orbit.rings; ++ring) g.drawText(juce::String(ring + 1), static_cast<int>(c.x) - 8, static_cast<int>(c.y - R + ring * ringW), 16, 10, juce::Justification::centred);
    if (posTicks >= 0.0) {
      const double a = cf::ui::playheadAngle(ph, model_.view().orbit.lengthTicks);
      g.setColour(editor_theme::accentGreen());
      g.drawLine(juce::Line<float>(pt(0.0, a), pt(R + 3.0, a)), 1.5f);
    }
  } else {
    g.setColour(juce::Colours::grey);
    g.drawText("Orbit view", orbitArea_, juce::Justification::centred);
  }

  // Ghost outline of the hit being dragged (the edit is applied on release).
  if (dragMode_ != DragMode::None)
    for (const auto& r : model_.view().pattern.rects)
      if (r.id == dragId_) {
        const float pw = static_cast<float>(patternArea_.getWidth());
        const float lh = static_cast<float>(patternArea_.getHeight()) / static_cast<float>(std::max<std::size_t>(1, model_.view().chops.size()));
        const float dx = static_cast<float>(dragNow_.x - dragStart_.x);
        float x = static_cast<float>(patternArea_.getX()) + static_cast<float>(r.x0) * pw;
        float width = static_cast<float>(r.x1 - r.x0) * pw;
        if (dragMode_ == DragMode::Move) x += dx; else width = std::max(2.0f, width + dx);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawRect(x, static_cast<float>(patternArea_.getY()) + static_cast<float>(r.chopIndex) * lh + 2.0f, width, lh - 4.0f, 1.5f);
        break;
      }

  if (selectingSections_) {
    const auto rectangle = juce::Rectangle<int>(selectionStart_, selectionEnd_);
    g.setColour(editor_theme::accentBlue().withAlpha(0.2f));
    g.fillRect(rectangle);
    g.setColour(editor_theme::accentBlue());
    g.drawRect(rectangle, 1);
  }
}

void ChopFractalEditor::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  selectingSections_ = false;
  if (!e.mods.isShiftDown() && !e.mods.isAltDown() && !e.mods.isPopupMenu() &&
      ((waveArea_.contains(pt) && model_.view().sourceFrames > 0) || (patternArea_.contains(pt) && !model_.editMode()))) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;
    selectingSections_ = true;
    selectingWave_ = waveArea_.contains(pt);
    selectionStart_ = selectionEnd_ = pt;
    updateSectionSelection(pt);
    if (selectingWave_)
      for (const auto id : model_.selectedChops()) { commands_.audition(id); break; }
    return;
  }
  if (waveArea_.contains(pt) && model_.view().sourceFrames > 0) {
    const cf::ui::ViewWindow w{0, model_.view().sourceFrames, static_cast<double>(waveArea_.getWidth())};
    const std::int64_t frame = cf::ui::xToFrame(w, static_cast<double>(pt.x - waveArea_.getX()));
    if (e.mods.isAltDown())  // Alt-click removes the nearest marker
      commands_.removeMarkerNear(static_cast<double>(pt.x - waveArea_.getX()));
    else if (e.mods.isShiftDown())  // Shift-click adds a marker; a plain click only auditions the chop under the cursor
      commands_.addMarkerAt(frame);
    else
      commands_.auditionFrame(frame);
  } else if (patternArea_.contains(pt)) {
    grabKeyboardFocus();
    model_.clearSelection();
    dragMode_ = DragMode::None;
    const double w = static_cast<double>(patternArea_.getWidth());
    const double fx = static_cast<double>(pt.x - patternArea_.getX()) / w;
    const int lane = cf::ui::laneAtY(static_cast<double>(pt.y - patternArea_.getY()), static_cast<double>(patternArea_.getHeight()), static_cast<int>(model_.view().chops.size()));
    for (auto it = model_.view().pattern.rects.rbegin(); it != model_.view().pattern.rects.rend(); ++it)
      if (it->chopIndex == lane && fx >= it->x0 && fx <= it->x1) {
        model_.select(it->id);
        if (it->depth == 0 && !it->locked && !e.mods.isPopupMenu()) {  // only top-level, unlocked hits can be dragged
          const double edgePx = (it->x1 - fx) * w;
          const double edgeZone = std::min(6.0, (it->x1 - it->x0) * w / 3.0);  // narrow hits keep a move zone
          dragMode_ = edgePx <= edgeZone ? DragMode::Resize : DragMode::Move;
          dragId_ = it->id;
          dragStart_ = dragNow_ = pt;
          dragLane_ = lane;
          dragCopy_ = e.mods.isAltDown();
        }
        break;
      }
    if (!model_.selected().valid() && e.mods.isShiftDown() && lane >= 0 && model_.view().gridTicks > 0 && model_.view().barTicks > 0 && model_.view().barCount > 0) {  // Shift-click on empty space adds a hit
      const cf::Ticks total = model_.view().barTicks * model_.view().barCount;
      const auto pos = cf::ui::splitTick(cf::ui::snapDown(cf::ui::xToTick(static_cast<double>(pt.x - patternArea_.getX()), w, total), model_.view().gridTicks), model_.view().barTicks, model_.view().barCount);
      const cf::Ticks dur = std::min<cf::Ticks>(model_.view().gridTicks, model_.view().barTicks - pos.startInBar);
      commands_.editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> cf::Result<cf::pattern::Pattern> {
        if (lane >= static_cast<int>(chops.chops.size())) return cf::makeError(cf::ErrorCode::OutOfRange, "no chop in that lane");
        return cf::pattern::addEvent(p, pos.bar, chops.chops[static_cast<std::size_t>(lane)].id, pos.startInBar, dur, chops);
      });
    }
    repaint();
  } else if (orbitArea_.contains(pt)) {
    const double R = std::min(orbitArea_.getWidth(), orbitArea_.getHeight()) / 2.0 - 4.0;
    const auto c = orbitArea_.getCentre();
    grabKeyboardFocus();
    model_.select(cf::ui::hitTest(model_.view().orbit, static_cast<double>(pt.x - c.x), static_cast<double>(pt.y - c.y), R));
    repaint();
  } else if (historyArea_.contains(pt)) {
    for (const auto& n : model_.view().tree) {
      const auto np = treePos(n);
      const int x = np.x, y = np.y;
      if (std::abs(pt.x - x) < 11 && std::abs(pt.y - y) < 11) {
        const cf::history::NodeId id = n.id;
        if (e.mods.isPopupMenu()) {
          commands_.historyMenu(id, this);
          break;
        }
        commands_.activateNode(id);
        break;
      }
    }
  }
}

void ChopFractalEditor::mouseDoubleClick(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  if (!waveArea_.contains(pt) || model_.view().sourceFrames <= 0) return;
  const cf::ui::ViewWindow w{0, model_.view().sourceFrames, static_cast<double>(waveArea_.getWidth())};
  const std::int64_t frame = cf::ui::xToFrame(w, static_cast<double>(pt.x - waveArea_.getX()));
  commands_.auditionFrame(frame);
}

// ---------------------------------------------------------------------------------------------
// direct hit editing, presets, chaos reroll, drag-out
// ---------------------------------------------------------------------------------------------

void ChopFractalEditor::updateSectionSelection(juce::Point<int> point) {
  const auto area = selectingWave_ ? waveArea_ : patternArea_;
  selectionEnd_ = {juce::jlimit(area.getX(), area.getRight() - 1, point.x),
                   juce::jlimit(area.getY(), area.getBottom() - 1, point.y)};
  const auto rect = juce::Rectangle<int>(selectionStart_, selectionEnd_);
  const bool click = rect.getWidth() < 3 && rect.getHeight() < 3;
  if (selectingWave_)
    model_.selectSectionsInWave(selectionStart_.x - area.getX(), selectionEnd_.x - area.getX(), click, area.getWidth());
  else
    model_.selectSectionsInPattern(rect.translated(-area.getX(), -area.getY()), selectionEnd_ - area.getPosition(), click, area.withZeroOrigin());
  repaint();
}

void ChopFractalEditor::mouseDrag(const juce::MouseEvent& e) {
  if (selectingSections_) { updateSectionSelection(e.getPosition()); return; }
  if (dragMode_ == DragMode::None) return;
  dragNow_ = e.getPosition();
  repaint(patternArea_);
}

void ChopFractalEditor::mouseUp(const juce::MouseEvent& e) {
  if (selectingSections_) {
    updateSectionSelection(e.getPosition());
    selectingSections_ = false;
    refresh(true);
    return;
  }
  if (dragMode_ == DragMode::None) return;
  const DragMode mode = dragMode_;
  dragMode_ = DragMode::None;
  dragNow_ = e.getPosition();
  const int dx = dragNow_.x - dragStart_.x;
  const int dy = dragNow_.y - dragStart_.y;
  if (std::abs(dx) < 3 && std::abs(dy) < 3) {
    repaint(patternArea_);
    return;  // a click, not a drag
  }
  repaint(patternArea_);
  if (model_.view().gridTicks <= 0 || model_.view().barTicks <= 0 || model_.view().barCount <= 0) return;
  const cf::EventId id = dragId_;
  const cf::Ticks total = model_.view().barTicks * model_.view().barCount;
  const double w = static_cast<double>(patternArea_.getWidth());
  const cf::Ticks grid = model_.view().gridTicks, barT = model_.view().barTicks;
  const int bars = model_.view().barCount;
  const cf::Ticks dTicks = static_cast<cf::Ticks>(std::llround(static_cast<double>(dx) / w * static_cast<double>(total)));
  const int newLane = cf::ui::laneAtY(static_cast<double>(dragNow_.y - patternArea_.getY()), static_cast<double>(patternArea_.getHeight()), static_cast<int>(model_.view().chops.size()));
  const bool copy = dragCopy_;
  const int oldLane = dragLane_;
  commands_.editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> cf::Result<cf::pattern::Pattern> {
    const cf::Event* ev = cf::pattern::findEvent(p, id);
    const auto loc = cf::pattern::locate(p, id);
    if (!ev || !loc) return cf::makeError(cf::ErrorCode::NotFound, "that hit no longer exists");
    const cf::Ticks absStart = static_cast<cf::Ticks>(loc->bar) * barT + ev->start;
    if (mode == DragMode::Resize) {
      const cf::Ticks absEnd = cf::ui::snapNearest(absStart + ev->duration + dTicks, grid);
      const cf::Ticks minDur = std::min(grid, barT - ev->start);  // never shrink a hit to an inaudible sliver
      const cf::Ticks dur = std::max(minDur, std::min(absEnd - absStart, barT - ev->start));
      return cf::pattern::resizeEvent(p, id, dur);
    }
    const cf::Ticks lastSlot = std::max<cf::Ticks>(0, barT * bars - grid);  // snapping may round up to the very end: keep a full cell
    const auto pos = cf::ui::splitTick(std::min(lastSlot, cf::ui::snapNearest(std::max<cf::Ticks>(0, absStart + dTicks), grid)), barT, bars);
    cf::Result<cf::pattern::Pattern> r = cf::makeError(cf::ErrorCode::InvalidArgument, "unreachable");
    if (copy) {
      if (pos.bar != loc->bar) return cf::makeError(cf::ErrorCode::InvalidArgument, "Alt-drag copies within the same bar.");
      r = cf::pattern::duplicateEvent(p, id, pos.startInBar);
    } else {
      r = cf::pattern::moveEvent(p, id, pos.bar, pos.startInBar);
    }
    if (!r.ok() || copy || newLane < 0 || newLane == oldLane || newLane >= static_cast<int>(chops.chops.size())) return r;
    // Vertical drag re-assigns the hit to the chop of the lane it was dropped on.
    const cf::Event* moved = cf::pattern::findEvent(r.value(), id);
    return moved ? cf::pattern::setEventChop(r.value(), id, chops.chops[static_cast<std::size_t>(newLane)].id, chops) : r;
  });
}

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

juce::Point<int> ChopFractalEditor::treePos(const cf::ui::TreeNodeView& n) const {
  const int stepX = model_.view().treeCols > 0 ? std::min(64, std::max(6, (historyArea_.getWidth() - 60) / model_.view().treeCols)) : 64;
  const int stepY = model_.view().treeRows > 0 ? std::min(30, std::max(6, (historyArea_.getHeight() - 40) / model_.view().treeRows)) : 30;
  return {historyArea_.getX() + 24 + n.column * stepX, historyArea_.getY() + 22 + n.depth * stepY};
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
