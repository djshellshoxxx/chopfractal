#include "PluginEditor.h"
#include "ControlHelp.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <cmath>

namespace cf = chopfractal;

namespace {
juce::Colour laneColour(int chopIndex) {
  return juce::Colour::fromHSV(static_cast<float>(((chopIndex % 8) + 8) % 8) / 8.0f, 0.55f, 0.85f, 1.0f);
}
}  // namespace

ChopFractalEditor::ChopFractalEditor(ChopFractalProcessor& processor) : AudioProcessorEditor(&processor), proc_(processor), capturePanel_(processor), morphPanel_(processor) {
  setSize(940, 992);
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
  editMode_.setTooltip("Off: drag to select sections. On: drag a hit to move or resize it.");
  play_.setTitle("Play");
  stop_.setTitle("Stop");
  play_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff228b44));
  play_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  stop_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffc0392b));
  stop_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  play_.setTooltip("Play the generated pattern from the beginning, including in the standalone app.");
  stop_.setTooltip("Stop local pattern playback. Press Play to restart from the beginning.");
  play_.onClick = [this] { proc_.startPlayback(); refresh(true); };
  stop_.onClick = [this] { proc_.stopPlayback(); refresh(true); };
  followHost_.onClick = [this] { proc_.followHostTransport(); refresh(true); };
  statusMessages_.setTitle("Status messages");
  statusMessages_.setMultiLine(true, true);
  statusMessages_.setReadOnly(true);
  statusMessages_.setScrollbarsShown(true);
  statusMessages_.setCaretVisible(false);
  statusMessages_.setPopupMenuEnabled(true);
  statusMessages_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20252d));
  statusMessages_.setColour(juce::TextEditor::textColourId, juce::Colours::white);
  statusMessages_.setTooltip("Full status message. Scroll to read long messages; select text to copy it.");
  addAndMakeVisible(statusMessages_);
  load_.onClick = [this] { chooseFile(); };
  detect_.onClick = [this] {
    const int mode = detectMode_.getSelectedId();
    const int hostBeats = proc_.hostBeatsPerBar.load(), hostUnit = std::max(1, proc_.hostBeatUnit.load());
    static const int kBars[] = {1, 2, 4, 8};
    const int loopBars = kBars[std::max(0, std::min(3, static_cast<int>(std::lround(proc_.currentParams().get(cf::host::kPatternBars)))))];
    run([=](cf::composition::ProjectSession& s) {
      cf::composition::DetectOptions d;
      d.merge = (mode == 2) ? cf::source::MergeMode::Merge : cf::source::MergeMode::Replace;  // Merge keeps the markers you placed
      if (mode >= 3) {
        d.mode = cf::composition::DetectMode::EvenGrid;
        d.grid.division = mode == 3 ? 8 : 16;
        // Quarter notes in the loop: bars x beats per bar x (4 / beat unit), from the pattern if there is one, else the host meter and Length control.
        const cf::TimeSignature ts = s.pattern() ? s.pattern()->settings.timeSignature : cf::TimeSignature{hostBeats, hostUnit};
        const int bars = s.pattern() ? s.pattern()->settings.bars : loopBars;
        d.grid.loopBeats = static_cast<double>(bars) * static_cast<double>(ts.numerator) * 4.0 / static_cast<double>(ts.denominator);
      }
      return s.applyChops(d);
    });
  };
  detectMode_.addItemList({"Transients (replace)", "Transients (keep my markers)", "Even grid 1/8", "Even grid 1/16"}, 1);
  detectMode_.setSelectedId(1);
  detectMode_.setTitle("Detection mode");
  addAndMakeVisible(detectMode_);
  locate_.onClick = [this] {
    chooser_ = std::make_unique<juce::FileChooser>("Locate the project's original audio file", juce::File(), "*.wav;*.aif;*.aiff");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
      const juce::File f = fc.getResult();
      if (f.existsAsFile()) proc_.loadFileAsync(f, true);
    });
  };
  addAndMakeVisible(capturePanel_);
  addAndMakeVisible(morphPanel_);
  rulesToggle_.setTitle("Open the rules and roles editor");
  addAndMakeVisible(rulesToggle_);
  RulesPanel::Hooks hooks;
  hooks.applyAndMutate = [this] { doMutate(true); };
  hooks.primarySelection = [this] { return selectedHits_.empty() ? cf::EventId{} : selectedHits_.front(); };
  hooks.densityHint = [this] { return proc_.currentParams().get(cf::host::kDensity); };
  hooks.selectBeat = [this](int bar, int beat) {
    selectedHits_.clear();
    proc_.withSession([&](cf::composition::ProjectSession& s) {
      if (!s.pattern() || bar < 0 || bar >= static_cast<int>(s.pattern()->bars.size())) return;
      const auto& beats = s.pattern()->bars[static_cast<std::size_t>(bar)].beats;
      if (beat >= 0 && beat < static_cast<int>(beats.size()))
        for (const auto& e : beats[static_cast<std::size_t>(beat)].events) selectedHits_.push_back(e.id);
    });
    repaint();
  };
  rulesPanel_ = std::make_unique<RulesPanel>(proc_, hooks);
  addChildComponent(*rulesPanel_);
  outputsPanel_ = std::make_unique<OutputsPanel>(proc_);
  addChildComponent(*outputsPanel_);
  outputsToggle_.setTitle("Show output routing");
  addAndMakeVisible(outputsToggle_);
  tempoPanel_ = std::make_unique<TempoPanel>(proc_);
  addChildComponent(*tempoPanel_);
  tempoToggle_.setTitle("Show tempo matching");
  addAndMakeVisible(tempoToggle_);
  padPanel_ = std::make_unique<PadPanel>(proc_);
  addChildComponent(*padPanel_);
  padsToggle_.setTitle("Show the performance pads");
  addAndMakeVisible(padsToggle_);
  // One overlay at a time: opening a panel closes the others.
  auto showOnly = [this](int which) {
    if (which != 0) { rulesPanel_->close(); rulesToggle_.setToggleState(false, juce::dontSendNotification); }
    if (which != 1) { scenePanel_->close(); scenesToggle_.setToggleState(false, juce::dontSendNotification); }
    if (which != 2) { padPanel_->close(); padsToggle_.setToggleState(false, juce::dontSendNotification); }
    if (which != 3) { tempoPanel_->close(); tempoToggle_.setToggleState(false, juce::dontSendNotification); }
    if (which != 4) { outputsPanel_->close(); outputsToggle_.setToggleState(false, juce::dontSendNotification); }
  };
  rulesToggle_.onClick = [this, showOnly] {
    if (rulesToggle_.getToggleState()) {
      showOnly(0);
      rulesToggle_.setToggleState(true, juce::dontSendNotification);
      rulesPanel_->open();
    } else {
      rulesPanel_->close();
    }
  };
  outputsToggle_.onClick = [this, showOnly] {
    if (outputsToggle_.getToggleState()) {
      showOnly(4);
      outputsToggle_.setToggleState(true, juce::dontSendNotification);
      outputsPanel_->open();
    } else {
      outputsPanel_->close();
    }
  };
  tempoToggle_.onClick = [this, showOnly] {
    if (tempoToggle_.getToggleState()) {
      showOnly(3);
      tempoToggle_.setToggleState(true, juce::dontSendNotification);
      tempoPanel_->open();
    } else {
      tempoPanel_->close();
    }
  };
  padsToggle_.onClick = [this, showOnly] {
    if (padsToggle_.getToggleState()) {
      showOnly(2);
      padsToggle_.setToggleState(true, juce::dontSendNotification);
      padPanel_->open();
    } else {
      padPanel_->close();
    }
  };
  scenesToggle_.setTitle("Show the scene list");
  addAndMakeVisible(scenesToggle_);
  scenePanel_ = std::make_unique<ScenePanel>(proc_);
  addChildComponent(*scenePanel_);
  scenesToggle_.onClick = [this, showOnly] {
    if (scenesToggle_.getToggleState()) {
      showOnly(1);
      scenesToggle_.setToggleState(true, juce::dontSendNotification);
      scenePanel_->open();
    } else {
      scenePanel_->close();
    }
  };
  addAndMakeVisible(locate_);
  locate_.setEnabled(false);
  generate_.onClick = [this] { doGenerate(); };
  mutate_.onClick = [this] { doMutate(); };
  undo_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.undo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to undo.")}; });
  };
  redo_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.redo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to redo.")}; });
  };
  zoom_.setTooltip("Subdivide the selected pattern hits into nested rhythms. Select hits or waveform sections first.");
  zoom_.onClick = [this] {
    auto ids = selectedHits_;
    if (ids.empty() && selected_.valid()) ids.push_back(selected_);
    const std::uint64_t seed = currentSeed() + 1;
    if (!ids.empty()) setSeed(seed);
    run([=](cf::composition::ProjectSession& s) {
      if (ids.empty()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a pattern hit or waveform section first, then press Zoom In.")};
      cf::zoom::Settings zs;
      zs.subdivisions = 4;
      zs.density = cf::zoom::Density::Balanced;
      // ProjectSession validates locks, depth and budget for each requested subdivision.
      int zoomed = 0;
      for (const auto id : ids) {
        const auto st = s.zoomIn(id, zs, seed + static_cast<std::uint64_t>(zoomed));
        if (!st.ok()) return st;
        ++zoomed;
      }
      return cf::Status{};
    });
    if (proc_.statusMessage().isEmpty()) {
      proc_.setStatusMessage("Zoomed " + juce::String(static_cast<int>(ids.size())) +
          " selected hits into nested rhythms. Collapse returns to the parent hit.");
      refresh(true);
    }
  };
  collapse_.onClick = [this] {
    const cf::EventId id = selected_;
    run([=](cf::composition::ProjectSession& s) {
      if (!id.valid()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit in the pattern first.")};
      return s.collapse(id);
    });
  };
  lock_.onClick = [this] { doToggleLock(); };
  dice_.onClick = [this] { setSeed(juce::Random::getSystemRandom().nextInt64() & 0x7fffffff); };

  embed_.setToggleState(proc_.embedSource.load(), juce::dontSendNotification);
  embed_.onClick = [this] { proc_.embedSource = embed_.getToggleState(); };
  addAndMakeVisible(embed_);

  seedLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(seedLabel_);
  seed_.setText("1");
  seed_.setInputRestrictions(12, "0123456789");
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
  reroll_.onClick = [this] { doReroll(); };
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
  smart_.onClick = [this] { doSmartSetup(); };
  acceptRoles_.onClick = [this] {
    const auto roles = setup_.roles;
    run([=](cf::composition::ProjectSession& s) {
      auto r = s.acceptRoleSuggestions(roles, 0.5);
      return r.ok() ? cf::Status{} : cf::Status{r.error()};
    });
  };
  useTempo_.onClick = [this] {
    if (haveSetup_ && !setup_.loops.empty()) {
      proc_.manualBpm = setup_.loops.front().bpm;  // only the manual fallback; the host's tempo is never changed
      status_ = "Manual tempo set to " + juce::String(setup_.loops.front().bpm, 1) + " BPM (used when the host gives no tempo).";
      proc_.setStatusMessage(status_);
      repaint();
    }
  };
  fractal_.onClick = [this] { doFractal(); };
  keep_.onClick = [this] { run([](cf::composition::ProjectSession& s) { return s.keepEvolved(); }); };
  exportWav_.onClick = [this] { doExportWav(); };
  exportKit_.onClick = [this] { doExportKit(); };

  for (juce::Button* b : std::initializer_list<juce::Button*>{&storeA_, &recallA_, &storeB_, &recallB_, &setRole_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  for (std::size_t i = 0; i < 2; ++i) {
    juce::Button* store = i == 0 ? &storeA_ : &storeB_;
    juce::Button* recall = i == 0 ? &recallA_ : &recallB_;
    store->onClick = [this, i] { run([=](cf::composition::ProjectSession& s) { return s.storeSnapshot(i); }); };
    recall->onClick = [this, i] { run([=](cf::composition::ProjectSession& s) { return s.recallSnapshot(i); }); };
  }
  for (const auto& r : cf::roles::builtinRoles()) role_.addItem(juce::String(r), role_.getNumItems() + 1);
  role_.setSelectedId(1);
  role_.setTitle("Role for the selected hit's chop");
  addAndMakeVisible(role_);
  setRole_.onClick = [this] {
    int idx = -1;
    for (const auto& r : pattern_.rects)
      if (r.id == selected_) idx = r.chopIndex;
    const juce::String role = role_.getText();
    run([=](cf::composition::ProjectSession& s) {
      if (idx < 0 || idx >= static_cast<int>(s.chops()->chops.size())) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit first; its chop gets the role (used by the next Generate or Mutate).")};
      return s.assignRole(s.chops()->chops[static_cast<std::size_t>(idx)].id, role.toStdString());
    });
  };

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

  evolve_.onClick = [this] { doEvolve(); };
  addAndMakeVisible(evolve_);
  everyLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(everyLabel_);
  every_.addItemList({"1 loop", "2 loops", "4 loops", "8 loops", "16 loops"}, 1);
  every_.setSelectedId(3);
  every_.setTitle("Evolve every");
  every_.onChange = [this] {
    if (evolve_.getToggleState()) doEvolve();
  };
  addAndMakeVisible(every_);
  evolveAmount_.setSliderStyle(juce::Slider::LinearHorizontal);
  evolveAmount_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
  evolveAmount_.setRange(0.05, 1.0, 0.01);
  evolveAmount_.setValue(0.25, juce::dontSendNotification);
  evolveAmount_.setTitle("Evolve amount");
  evolveAmount_.onDragEnd = [this] {
    if (evolve_.getToggleState()) doEvolve();
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

ChopFractalEditor::~ChopFractalEditor() { stopTimer(); }

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
  capturePanel_.setBounds(area.removeFromTop(46));
  {
    auto row = area.removeFromTop(28);
    morphPanel_.setBounds(row.removeFromLeft(row.getWidth() - 430));
    rulesToggle_.setBounds(row.removeFromLeft(80));
    scenesToggle_.setBounds(row.removeFromLeft(90));
    padsToggle_.setBounds(row.removeFromLeft(80));
    tempoToggle_.setBounds(row.removeFromLeft(80));
    outputsToggle_.setBounds(row);
  }
  area.removeFromTop(6);
  waveArea_ = area.removeFromTop(120);
  area.removeFromTop(6);
  patternArea_ = area.removeFromTop(230);
  area.removeFromTop(6);
  statusMessages_.setBounds(area.removeFromBottom(74).reduced(0, 2));
  if (rulesPanel_) rulesPanel_->setBounds(waveArea_.getX(), waveArea_.getY(), waveArea_.getWidth(), area.getBottom() - waveArea_.getY());
  if (outputsPanel_) outputsPanel_->setBounds(waveArea_.getX(), waveArea_.getY(), waveArea_.getWidth(), area.getBottom() - waveArea_.getY());
  if (tempoPanel_) tempoPanel_->setBounds(waveArea_.getX(), waveArea_.getY(), waveArea_.getWidth(), area.getBottom() - waveArea_.getY());
  if (padPanel_) padPanel_->setBounds(waveArea_.getX(), waveArea_.getY(), waveArea_.getWidth(), area.getBottom() - waveArea_.getY());
  if (scenePanel_) scenePanel_->setBounds(waveArea_.getX(), waveArea_.getY(), waveArea_.getWidth(), area.getBottom() - waveArea_.getY());
  auto bottom = area.removeFromTop(area.getHeight() - 2);
  orbitArea_ = bottom.removeFromRight(std::min(bottom.getHeight(), bottom.getWidth() / 2));
  historyArea_ = bottom.withTrimmedRight(6);
}

// ---------------------------------------------------------------------------------------------
// state and actions
// ---------------------------------------------------------------------------------------------

std::uint64_t ChopFractalEditor::currentSeed() const { return static_cast<std::uint64_t>(seed_.getText().getLargeIntValue()); }
void ChopFractalEditor::setSeed(std::uint64_t seed) { seed_.setText(juce::String(static_cast<juce::int64>(seed)), juce::dontSendNotification); }

void ChopFractalEditor::run(const std::function<cf::Status(cf::composition::ProjectSession&)>& action) {
  cf::Status status;
  juce::String notice;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    status = action(s);
    for (const auto& n : s.takeNotices()) notice = juce::String(n.text);
  });
  status_ = status.ok() ? notice : juce::String(status.error().message);
  proc_.setStatusMessage(status_);
  refresh(true);
}

bool ChopFractalEditor::isInterestedInFileDrag(const juce::StringArray& files) {
  for (const auto& path : files)
    if (juce::File(path).hasFileExtension("wav;aif;aiff;flac;ogg;mp3")) return true;
  return false;
}

void ChopFractalEditor::filesDropped(const juce::StringArray& files, int, int) {
  for (const auto& path : files) {
    const juce::File file(path);
    if (!file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3")) continue;
    if (!file.existsAsFile()) {
      proc_.setStatusMessage("The dropped sample could not be found.");
      refresh(true);
      return;
    }
    proc_.stopPlayback();
    selectedHits_.clear();
    selectedChops_.clear();
    selected_ = {};
    proc_.loadFileAsync(file);
    refresh(true);
    return;  // a ChopFractal project has one source loop
  }
  proc_.setStatusMessage("Drop an audio sample (WAV, AIFF, FLAC, OGG or MP3). One source loop is loaded at a time.");
  refresh(true);
}

void ChopFractalEditor::chooseFile() {
  chooser_ = std::make_unique<juce::FileChooser>("Choose a loop (WAV or AIFF, up to 60 s)", juce::File(), "*.wav;*.aif;*.aiff");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
    const juce::File f = fc.getResult();
    if (f.existsAsFile()) proc_.loadFileAsync(f);
  });
}

void ChopFractalEditor::doGenerate() {
  const cf::host::ParamValues params = proc_.currentParams();
  cf::pattern::Settings base;
  base.seed = currentSeed();
  const cf::TimeSignature hostMeter{proc_.hostBeatsPerBar.load(), proc_.hostBeatUnit.load()};
  if (hostMeter.valid()) base.timeSignature = hostMeter;  // bars follow the host's meter (4/4 when it gives none)
  const double v = params.get(cf::host::kVariationAmount);
  base.variation = {v, v, v, v};
  cf::pattern::Settings s = cf::composition::settingsFromParams(params, base);
  s.maxShiftTicks = std::min<cf::Ticks>(20, cf::gridTicks(s.grid) / 8);
  run([=](cf::composition::ProjectSession& session) { return session.generate(s); });
}

void ChopFractalEditor::doMutate(bool rulesPass) {
  const cf::host::ParamValues params = proc_.currentParams();
  const std::uint64_t seed = currentSeed() + 1;
  bool hasPattern = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) { hasPattern = s.pattern() != nullptr; });
  if (hasPattern) setSeed(seed);  // a refused Mutate does not consume a seed
  run([=](cf::composition::ProjectSession& session) {
    if (!session.pattern()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Generate a pattern first.")};
    cf::pattern::Settings live = session.pattern()->settings;  // length, meter and grid stay; the automatable controls refresh
    live.density = params.get(cf::host::kDensity);
    live.swing = params.get(cf::host::kSwing);
    live.allowReverse = params.get(cf::host::kAllowReverse) >= 0.5;
    live.allowPitch = params.get(cf::host::kAllowPitch) >= 0.5;
    live.allowRetrigger = params.get(cf::host::kAllowRetrigger) >= 0.5;
    live.allowFilter = params.get(cf::host::kAllowFilter) >= 0.5;
    live.allowGlide = params.get(cf::host::kAllowGlide) >= 0.5;
    live.allowCrunch = params.get(cf::host::kAllowCrunch) >= 0.5;
    live.fxIntensity = params.get(cf::host::kFxIntensity);
    const double v = params.get(cf::host::kVariationAmount);
    live.variation = {v, v, v, v};
    cf::pattern::MutateOptions opt;
    opt.seed = seed;
    opt.amount = rulesPass ? 1.0 : cf::composition::mutateAmountFromParams(params);  // Apply and Mutate re-evaluates every beat
    live = cf::composition::applyChaos(live, params.get(cf::host::kChaos));
    return session.mutate(opt, &live, rulesPass ? std::string("Mutate (rules)") : std::string());
  });
}

void ChopFractalEditor::doSmartSetup() {
  const double hostBpm = proc_.usedFallbackTempo.load() ? 0.0 : proc_.activeBpm.load();
  cf::composition::SetupSuggestion found;
  bool ok = false;
  run([&](cf::composition::ProjectSession& s) {
    auto r = s.smartSetup(hostBpm);
    if (!r.ok()) return cf::Status{r.error()};
    found = std::move(r.value());
    ok = true;
    return cf::Status{};
  });
  if (!ok) return;
  setup_ = found;
  haveSetup_ = true;
  acceptRoles_.setEnabled(!setup_.roles.empty());
  useTempo_.setEnabled(!setup_.loops.empty());
  int confident = 0;
  for (const auto& r : setup_.roles) confident += (r.role != cf::insight::Role::Other && r.confidence >= 0.5);
  status_ = juce::String(confident) + " of " + juce::String(static_cast<int>(setup_.roles.size())) + " chops have a role suggestion (Accept Roles applies them to untagged chops).";
  if (!setup_.loops.empty()) status_ << " Best loop: " << juce::String(setup_.loops.front().note) << " (Use Tempo copies it to the manual tempo).";
  proc_.setStatusMessage(status_);
  repaint();
}

void ChopFractalEditor::doFractal() {
  cf::fractal::Settings fs;
  fs.motif = motif_.getText().toStdString();
  fs.depth = depth_.getSelectedId();
  fs.seed = currentSeed();
  fs.mutation = fractalMutation_ >= 0.0 ? fractalMutation_ : proc_.currentParams().get(cf::host::kVariationAmount) * 0.5;
  fs.mirror = false;
  fs.accentDecay = fractalAccent_;
  run([=](cf::composition::ProjectSession& s) { return s.applyFractal(fs); });
}

void ChopFractalEditor::doEvolve() {
  cf::evolve::Settings es = pushedEvolve_;  // keeps ramp and ceiling from a restored project
  proc_.withSession([&](cf::composition::ProjectSession& s) { es = s.evolveSettings(); });
  es.enabled = evolve_.getToggleState();
  static const int kEvery[] = {1, 2, 4, 8, 16};
  es.everyLoops = kEvery[std::max(0, std::min(4, every_.getSelectedId() - 1))];
  es.amount = evolveAmount_.getValue();
  if (!es.enabled || !pushedEvolve_.enabled) es.startSeed = currentSeed();
  pushedEvolve_ = es;
  run([=](cf::composition::ProjectSession& s) { return s.setEvolve(es); });
  if (es.enabled) {
    bool running = false;
    proc_.withSession([&](cf::composition::ProjectSession& s) { running = s.evolveRunning(); });
    if (!running) evolve_.setToggleState(false, juce::dontSendNotification);  // refused (for example no pattern yet)
  }
}

void ChopFractalEditor::doExportWav() {
  chooser_ = std::make_unique<juce::FileChooser>("Export the pattern as WAV", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("chopfractal.wav"), "*.wav");
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                        [this](const juce::FileChooser& fc) {
                          juce::File f = fc.getResult();
                          if (f == juce::File()) return;
                          const bool extensionAdded = !f.hasFileExtension("wav");
                          if (extensionAdded) f = f.getParentDirectory().getChildFile(f.getFileName() + ".wav");  // "my.loop" becomes "my.loop.wav", not a WAV named .loop
                          cf::composition::ExportWavOptions o;
                          static const cf::wav::Format kFmt[] = {cf::wav::Format::Pcm16, cf::wav::Format::Pcm24, cf::wav::Format::Float32};
                          static const int kRate[] = {44100, 48000, 88200, 96000};
                          static const int kLoops[] = {1, 2, 4, 8, 16};
                          o.wav.format = kFmt[std::max(0, std::min(2, exportFormat_.getSelectedId() - 1))];
                          o.wav.sampleRate = kRate[std::max(0, std::min(3, exportRate_.getSelectedId() - 1))];
                          o.loops = kLoops[std::max(0, std::min(4, exportLoops_.getSelectedId() - 1))];
                          o.bpm = proc_.activeBpm.load();
                          o.overwrite = !extensionAdded;  // the native dialog asked about the name as typed; a name we extended is checked again
                          const std::string path = f.getFullPathName().toStdString();
                          cf::Status status;
                          cf::composition::ExportReport rep;
                          proc_.withSession([&](cf::composition::ProjectSession& s) {
                            auto r = s.exportWav(path, o);
                            if (r.ok()) rep = r.value(); else status = r.error();
                          });
                          status_ = status.ok() ? "Exported " + f.getFileName() + (rep.clipped ? " (" + juce::String(static_cast<juce::int64>(rep.clipped)) + " samples clipped; lower the output gain)" : juce::String())
                                                : juce::String(status.error().message);
                          proc_.setStatusMessage(status_);
                          repaint();
                        });
}

void ChopFractalEditor::historyMenu(cf::history::NodeId id) {
  juce::PopupMenu m;
  m.addItem(1, "Favorite / unfavorite");
  m.addItem(2, "Rename...");
  m.addItem(3, "Compare with active variation");
  m.addItem(4, "Delete this branch");
  juce::Component::SafePointer<ChopFractalEditor> self(this);
  m.showMenuAsync(juce::PopupMenu::Options(), [self, id](int choice) {
    if (self == nullptr) return;
    auto* const ed = self.getComponent();
    if (choice == 1) {
      bool now = false;
      ed->proc_.withSession([&](cf::composition::ProjectSession& s) {
        for (const auto& n : s.history().listAll())
          if (n.id == id) now = n.favorite;
      });
      ed->run([=](cf::composition::ProjectSession& s) { return s.setFavorite(id, !now); });
    } else if (choice == 2) {
      juce::String currentName;
      ed->proc_.withSession([&](cf::composition::ProjectSession& s) {
        for (const auto& n : s.history().listAll())
          if (n.id == id) currentName = juce::String(n.label);
      });
      auto* w = new juce::AlertWindow("Rename variation", "Name:", juce::MessageBoxIconType::NoIcon);
      w->addTextEditor("name", currentName);
      w->addButton("OK", 1);
      w->addButton("Cancel", 0);
      w->enterModalState(true, juce::ModalCallbackFunction::create([self, w, id](int r) {
                           if (r == 1 && self != nullptr) {
                             const std::string name = w->getTextEditorContents("name").toStdString();
                             if (name.empty()) return;
                             self->run([=](cf::composition::ProjectSession& s) { return s.renameNode(id, name); });
                           }
                         }),
                         true);
    } else if (choice == 3) {
      std::string text;
      cf::Status st;
      ed->proc_.withSession([&](cf::composition::ProjectSession& s) {
        auto r = s.compareNodes(s.history().active(), id);
        if (r.ok()) text = r.value(); else st = r.error();
      });
      ed->status_ = st.ok() ? juce::String(text) : juce::String(st.error().message);
      ed->proc_.setStatusMessage(ed->status_);
      ed->repaint();
    } else if (choice == 4) {
      ed->run([=](cf::composition::ProjectSession& s) { return s.deleteBranch(id); });
    }
  });
}

void ChopFractalEditor::doExportKit() {
  chooser_ = std::make_unique<juce::FileChooser>("Choose a folder for the Producer Kit", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
    const juce::File dir = fc.getResult();
    if (dir == juce::File()) return;
    cf::composition::KitOptions k;
    k.bpm = proc_.activeBpm.load();
    const std::string path = dir.getFullPathName().toStdString();
    cf::Status status;
    cf::composition::ExportReport rep;
    proc_.withSession([&](cf::composition::ProjectSession& s) {
      auto r = s.exportKit(path, k);  // refuses before writing if any file already exists
      if (r.ok()) rep = r.value(); else status = r.error();
    });
    status_ = status.ok() ? "Producer Kit written to " + dir.getFileName() + " (" + juce::String(static_cast<int>(rep.files.size())) + " files)" : juce::String(status.error().message);
    proc_.setStatusMessage(status_);
    repaint();
  });
}

int ChopFractalEditor::selectedBar() const {
  for (const auto& r : pattern_.rects)
    if (r.id == selected_) return r.bar;
  return 0;
}

void ChopFractalEditor::doToggleLock() {
  bool found = false;
  for (const auto& r : pattern_.rects) found = found || r.id == selected_;
  if (!found) {
    status_ = "Select a hit first; Lock Bar locks the bar that hit is in.";
    proc_.setStatusMessage(status_);
    repaint();
    return;
  }
  const int bar = selectedBar();
  run([=](cf::composition::ProjectSession& s) {
    return s.edit([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
      const bool now = bar < static_cast<int>(p.bars.size()) && p.bars[static_cast<std::size_t>(bar)].locked;
      return cf::pattern::setLock(p, {cf::ScopeLevel::Bar, bar, 0, {}}, !now);
    });
  });
}

void ChopFractalEditor::refresh(bool force) {
  if (auto* root = getTopLevelComponent(); root != tooltipRoot_) {
    tooltipRoot_ = root;
    // Include the standalone wrapper's Options and feedback-notice Settings controls.
    control_help::applyToTree(*root);
  }
  juce::String sig;
  bool rebuilt = false;
  bool havePattern = false, haveSource = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    sig << juce::String::toHexString(reinterpret_cast<std::uintptr_t>(s.playback().get())) << "|" << static_cast<int>(s.history().size()) << "|"
        << static_cast<juce::int64>(s.history().active()) << "|" << (s.chopMap() ? static_cast<int>(s.chopMap()->markers().size()) : -1);
    sourceMissing_ = s.sourceMissing();
    const bool running = s.evolveRunning();
    if (evolve_.getToggleState() != running) evolve_.setToggleState(running, juce::dontSendNotification);
    const auto& se = s.evolveSettings();
    if (!running && (se.everyLoops != pushedEvolve_.everyLoops || se.amount != pushedEvolve_.amount || se.startSeed != pushedEvolve_.startSeed)) {
      pushedEvolve_ = se;
      static const int kEvery[] = {1, 2, 4, 8, 16};
      for (int i = 0; i < 5; ++i)
        if (kEvery[i] == s.evolveSettings().everyLoops && every_.getSelectedId() != i + 1) every_.setSelectedId(i + 1, juce::dontSendNotification);
      evolveAmount_.setValue(s.evolveSettings().amount, juce::dontSendNotification);
    }
    if (!force && sig == signature_) return;
    rebuilt = true;
    signature_ = sig;
    sourceFrames_ = 0;
    markers_.clear();
    chops_.clear();
    pattern_ = {};
    orbit_ = {};
    tree_.clear();
    const auto pb = s.playback();
    if (s.chopMap()) sourceFrames_ = s.chopMap()->info().frames;
    if (pb && pb->source && (peaks_.min.empty() || peaksSource_ != pb->source.get() || peaksWidth_ != waveArea_.getWidth())) {
      peaksSource_ = pb->source.get();
      peaksWidth_ = waveArea_.getWidth();
      const auto ptrs = std::vector<const float*>{pb->source->channel(0), pb->source->channel(pb->source->channels - 1)};
      peaks_ = cf::ui::computePeaks(ptrs.data(), pb->source->channels, pb->source->frames, {0, pb->source->frames}, std::max(1, waveArea_.getWidth()));
    }
    if (!pb || !pb->source) peaks_ = {};
    if (s.chopMap()) {
      markers_ = cf::ui::buildMarkerViews(*s.chopMap(), cf::ui::ViewWindow{0, std::max<std::int64_t>(1, sourceFrames_), static_cast<double>(waveArea_.getWidth())});
      if (s.chops()) chops_ = s.chops()->chops;
      if (s.pattern() && s.chops()) {
        const cf::pattern::Pattern& shown = *s.displayPattern();  // the morph preview while morphing
        pattern_ = cf::ui::buildPatternView(shown, *s.chops(), [&](cf::ChopId id) { return s.roleMap().roleOf(id); });
        orbit_ = cf::ui::buildOrbitView(shown, *s.chops());
        barTicks_ = cf::pattern::barTicks(s.pattern()->settings);
        gridTicks_ = cf::gridTicks(s.pattern()->settings.grid);
        barCount_ = static_cast<int>(s.pattern()->bars.size());
      }
    }
    tree_ = cf::ui::layoutHistoryTree(s.history().listAll(), s.history().active());
    treeCols_ = treeRows_ = 0;
    for (const auto& n : tree_) {
      treeCols_ = std::max(treeCols_, n.column);
      treeRows_ = std::max(treeRows_, n.depth);
    }
    haveSetup_ = false;  // Smart Setup suggestions belong to the chops they were made for
    havePattern = s.pattern() != nullptr;
    haveSource = sourceFrames_ > 0;
    bool stillThere = false;
    for (const auto& r : pattern_.rects) stillThere = stillThere || r.id == selected_;
    if (!stillThere) selected_ = cf::EventId{};
    selectedHits_.erase(std::remove_if(selectedHits_.begin(), selectedHits_.end(), [&](cf::EventId id) {
      return std::none_of(pattern_.rects.begin(), pattern_.rects.end(), [&](const cf::ui::EventRect& r) { return r.id == id; });
    }), selectedHits_.end());
    selectedChops_.erase(std::remove_if(selectedChops_.begin(), selectedChops_.end(), [&](cf::ChopId id) {
      return std::none_of(chops_.begin(), chops_.end(), [&](const cf::ChopInfo& chop) { return chop.id == id; });
    }), selectedChops_.end());
  });
  locate_.setEnabled(sourceMissing_);
  if (rebuilt) {
    acceptRoles_.setEnabled(false);
    useTempo_.setEnabled(false);
    for (juce::Button* b : std::initializer_list<juce::Button*>{&detect_, &generate_, &smart_}) b->setEnabled(haveSource);
    for (juce::Button* b : std::initializer_list<juce::Button*>{&mutate_, &fractal_}) b->setEnabled(havePattern);
    const bool canExport = havePattern && !sourceMissing_;  // exports render the source audio
    for (juce::Button* b : std::initializer_list<juce::Button*>{&exportWav_, &exportKit_}) b->setEnabled(canExport);
    dragMidi_.setEnabled(canExport);
    dragWav_.setEnabled(canExport);
  }
  if (embed_.getToggleState() != proc_.embedSource.load()) embed_.setToggleState(proc_.embedSource.load(), juce::dontSendNotification);
  const bool playing = proc_.playheadQuarters() >= 0.0;
  if (force || rebuilt) repaint();
  else if (playing || lastPlaying_) {  // only the playhead views change while the transport runs
    repaint(orbitArea_);
    repaint(patternArea_);
  }
  lastPlaying_ = playing;
  // Messages arrive from other threads' work too (file loading, Evolve, restore): repaint the status strip when it changes.
  const juce::String statusKey = proc_.statusMessage() + (proc_.usedFallbackTempo.load() ? "|t" : "") + (proc_.usedFallbackMeter.load() ? "|m" : "") + juce::String(proc_.manualBpm.load());
  if (statusKey != lastStatusKey_) {
    lastStatusKey_ = statusKey;
    juce::String line = proc_.statusMessage();
    if (proc_.usedFallbackTempo.load())
      line << (line.isEmpty() ? "" : "\n") << "Using local tempo: " << juce::String(proc_.manualBpm.load(), 1) << " BPM";
    if (proc_.usedFallbackMeter.load()) line << "   |   Assuming 4/4";
    statusMessages_.setText(line, false);
    statusMessages_.moveCaretToTop(false);
  }
}

// ---------------------------------------------------------------------------------------------
// drawing and mouse
// ---------------------------------------------------------------------------------------------

void ChopFractalEditor::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xff15181d));
  g.setColour(juce::Colours::white.withAlpha(0.85f));
  g.setFont(13.0f);

  // Source waveform with chop markers.
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(waveArea_);
  if (!peaks_.min.empty()) {
    g.setColour(juce::Colour(0xff6fb1ff));
    const int n = static_cast<int>(peaks_.min.size());
    const float cy = static_cast<float>(waveArea_.getCentreY());
    const float h = static_cast<float>(waveArea_.getHeight()) * 0.45f;
    for (int i = 0; i < n; ++i)
      g.drawVerticalLine(waveArea_.getX() + i, cy - peaks_.max[static_cast<std::size_t>(i)] * h, cy - peaks_.min[static_cast<std::size_t>(i)] * h);
  } else {
    g.setColour(juce::Colours::grey);
    g.drawText(sourceMissing_ ? "The project's audio file was not found. Press Locate Source (your markers, pattern and history are kept)."
                              : "Drop an audio sample here, or press Load Loop. Then Detect Chops, Generate and Play.",
               waveArea_, juce::Justification::centred);
  }
  // Waveform selections correspond to complete chop sections, not destructive source trims.
  for (const auto& chop : chops_)
    if (std::find(selectedChops_.begin(), selectedChops_.end(), chop.id) != selectedChops_.end() && sourceFrames_ > 0) {
      const float x = static_cast<float>(waveArea_.getX()) + static_cast<float>(chop.range.start) / static_cast<float>(sourceFrames_) * waveArea_.getWidth();
      const float width = static_cast<float>(chop.range.length()) / static_cast<float>(sourceFrames_) * waveArea_.getWidth();
      g.setColour(juce::Colour(0xff6fb1ff).withAlpha(0.3f));
      g.fillRect(x, static_cast<float>(waveArea_.getY()), width, static_cast<float>(waveArea_.getHeight()));
    }
  int markerNumber = 0;
  for (const auto& m : markers_) {
    ++markerNumber;
    g.setColour(m.enabled ? juce::Colour(0xffffc857) : juce::Colours::grey);
    const int x = waveArea_.getX() + static_cast<int>(m.x);
    g.drawVerticalLine(x, static_cast<float>(waveArea_.getY()), static_cast<float>(waveArea_.getBottom()));
    g.drawText(juce::String(markerNumber), x + 2, waveArea_.getY(), 28, 14, juce::Justification::left);
  }

  // Pattern grid: lanes by chop, numbered so colour is never the only indicator.
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(patternArea_);
  const int lanes = std::max(1, static_cast<int>(chops_.size()));
  const float laneH = static_cast<float>(patternArea_.getHeight()) / static_cast<float>(lanes);
  const float w = static_cast<float>(patternArea_.getWidth());
  g.setColour(juce::Colours::white.withAlpha(0.12f));
  for (double x : pattern_.beatLines) g.drawVerticalLine(patternArea_.getX() + static_cast<int>(x * w), static_cast<float>(patternArea_.getY()), static_cast<float>(patternArea_.getBottom()));
  for (std::size_t b = 0; b < pattern_.barLines.size(); ++b) {
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    const int x = patternArea_.getX() + static_cast<int>(pattern_.barLines[b] * w);
    g.drawVerticalLine(x, static_cast<float>(patternArea_.getY()), static_cast<float>(patternArea_.getBottom()));
    if (b < pattern_.barLocked.size() && pattern_.barLocked[b]) {
      g.setColour(juce::Colour(0xffff6b6b));
      g.drawText("locked", x + 3, patternArea_.getBottom() - 16, 60, 14, juce::Justification::left);
    }
  }
  for (const auto& r : pattern_.rects) {
    const float inset = static_cast<float>(r.depth) * 3.0f;
    const float y = static_cast<float>(patternArea_.getY()) + static_cast<float>(std::max(0, r.chopIndex)) * laneH + inset;
    juce::Rectangle<float> rect(static_cast<float>(patternArea_.getX()) + static_cast<float>(r.x0) * w, y, std::max(2.0f, static_cast<float>(r.x1 - r.x0) * w - 1.0f),
                                std::max(4.0f, laneH - 2.0f - 2.0f * inset));
    g.setColour(laneColour(r.chopIndex).withAlpha(r.depth > 0 ? 0.95f : 0.75f));
    g.fillRect(rect);
    if (r.id == selected_ || std::find(selectedHits_.begin(), selectedHits_.end(), r.id) != selectedHits_.end()) {
      g.setColour(juce::Colours::white);
      g.drawRect(rect, 2.0f);
    } else if (r.locked || r.hasChild) {
      g.setColour(r.locked ? juce::Colour(0xffff6b6b) : juce::Colours::white);
      g.drawRect(rect, 1.0f);
    }
    if (rect.getWidth() > 14.0f && rect.getHeight() > 12.0f) {
      g.setColour(juce::Colours::black);
      g.drawText(juce::String(r.chopIndex + 1), rect.toNearestInt(), juce::Justification::centred);
    }
  }
  if (pattern_.rects.empty()) {
    g.setColour(juce::Colours::grey);
    g.drawText("Detect chops, then press Generate.", patternArea_, juce::Justification::centred);
  }

  // Variation family tree.
  g.saveState();
  g.reduceClipRegion(historyArea_);
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(historyArea_);
  for (const auto& n : tree_) {
    const auto np = treePos(n);
    const int x = np.x, y = np.y;
    for (const auto& p : tree_)
      if (p.id == n.parent) {
        g.setColour(juce::Colours::grey);
        const auto pp = treePos(p);
        g.drawLine(static_cast<float>(pp.x), static_cast<float>(pp.y), static_cast<float>(x), static_cast<float>(y));
      }
    g.setColour(n.active ? juce::Colour(0xff7cf29a) : (n.favorite ? juce::Colour(0xffffc857) : juce::Colour(0xff9aa4b2)));
    g.fillEllipse(static_cast<float>(x - 8), static_cast<float>(y - 8), 16.0f, 16.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.drawText(n.label.empty() ? juce::String(static_cast<juce::int64>(n.id)) : juce::String(n.label), x + 10, y - 7, 90, 14, juce::Justification::left);
  }
  if (tree_.empty()) {
    g.setColour(juce::Colours::grey);
    g.drawText("Each Generate, Mutate and Zoom is saved here as a branch you can return to.", historyArea_, juce::Justification::centred);
  }
  g.restoreState();

  // Orbit View: one ring per chop (ring 1 outermost, numbered), the playhead sweeping clockwise from 12 o'clock.
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(orbitArea_);
  if (!orbit_.arcs.empty()) {
    const auto c = orbitArea_.getCentre().toFloat();
    const double R = std::min(orbitArea_.getWidth(), orbitArea_.getHeight()) / 2.0 - 4.0;
    const double ph = proc_.playheadQuarters();
    const double posTicks = ph >= 0.0 ? ph * cf::kTicksPerQuarter : -1.0;
    auto pt = [&](double r, double a) { return juce::Point<float>(c.x + static_cast<float>(r * std::sin(a)), c.y - static_cast<float>(r * std::cos(a))); };
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    for (double a : orbit_.beatAngles) g.drawLine(juce::Line<float>(pt(R * 0.25, a), pt(R, a)), 0.5f);
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    for (double a : orbit_.barAngles) g.drawLine(juce::Line<float>(pt(R * 0.25, a), pt(R + 3.0, a)), 1.2f);
    for (const auto& a : orbit_.arcs) {
      const auto rr = cf::ui::arcRadii(orbit_, a, R);
      const double pulse = posTicks >= 0.0 ? cf::ui::hitPulse(a, posTicks, orbit_.lengthTicks) : 0.0;
      juce::Path path;
      path.addCentredArc(c.x, c.y, static_cast<float>((rr.inner + rr.outer) / 2.0), static_cast<float>((rr.inner + rr.outer) / 2.0), 0.0f,
                         static_cast<float>(a.startAngle), static_cast<float>(a.startAngle + a.sweep), true);
      g.setColour(laneColour(a.ring).withAlpha(a.childActive ? 0.35f : static_cast<float>(0.45 + 0.4 * std::min(1.0, static_cast<double>(a.level))) ).brighter(static_cast<float>(pulse)));
      g.strokePath(path, juce::PathStrokeType(static_cast<float>(std::max(2.0, rr.outer - rr.inner)), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
      if (a.locked) {
        g.setColour(juce::Colour(0xffff6b6b));
        g.strokePath(path, juce::PathStrokeType(1.0f));
      }
      if (a.id == selected_) {
        g.setColour(juce::Colours::white);
        g.strokePath(path, juce::PathStrokeType(2.0f));
      }
    }
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(10.0f);
    const double ringW = (R - 0.25 * R) / std::max(1, orbit_.rings);
    for (int ring = 0; ring < orbit_.rings; ++ring) g.drawText(juce::String(ring + 1), static_cast<int>(c.x) - 8, static_cast<int>(c.y - R + ring * ringW), 16, 10, juce::Justification::centred);
    if (posTicks >= 0.0) {
      const double a = cf::ui::playheadAngle(ph, orbit_.lengthTicks);
      g.setColour(juce::Colour(0xff7cf29a));
      g.drawLine(juce::Line<float>(pt(0.0, a), pt(R + 3.0, a)), 1.5f);
    }
  } else {
    g.setColour(juce::Colours::grey);
    g.drawText("Orbit view", orbitArea_, juce::Justification::centred);
  }

  // Ghost outline of the hit being dragged (the edit is applied on release).
  if (dragMode_ != DragMode::None)
    for (const auto& r : pattern_.rects)
      if (r.id == dragId_) {
        const float pw = static_cast<float>(patternArea_.getWidth());
        const float lh = static_cast<float>(patternArea_.getHeight()) / static_cast<float>(std::max<std::size_t>(1, chops_.size()));
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
    g.setColour(juce::Colour(0xff6fb1ff).withAlpha(0.2f));
    g.fillRect(rectangle);
    g.setColour(juce::Colour(0xff6fb1ff));
    g.drawRect(rectangle, 1);
  }
  if (morphPanel_.previewing()) {
    g.setColour(juce::Colour(0xffffc857));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("Morph preview", patternArea_.withHeight(16).reduced(6, 0), juce::Justification::centredRight);
  }
}

void ChopFractalEditor::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  selectingSections_ = false;
  if (!e.mods.isShiftDown() && !e.mods.isAltDown() && !e.mods.isPopupMenu() &&
      ((waveArea_.contains(pt) && sourceFrames_ > 0) || (patternArea_.contains(pt) && !editMode_.getToggleState()))) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;
    selectingSections_ = true;
    selectingWave_ = waveArea_.contains(pt);
    selectionStart_ = selectionEnd_ = pt;
    updateSectionSelection(pt);
    if (selectingWave_)
      for (const auto id : selectedChops_) { proc_.audition(id); break; }
    return;
  }
  if (waveArea_.contains(pt) && sourceFrames_ > 0) {
    const cf::ui::ViewWindow w{0, sourceFrames_, static_cast<double>(waveArea_.getWidth())};
    const std::int64_t frame = cf::ui::xToFrame(w, static_cast<double>(pt.x - waveArea_.getX()));
    if (e.mods.isAltDown()) {  // Alt-click removes the nearest marker
      cf::ChopId nearest;
      double best = 8.0;
      for (const auto& m : markers_)
        if (std::fabs(m.x - (pt.x - waveArea_.getX())) < best) {
          best = std::fabs(m.x - (pt.x - waveArea_.getX()));
          nearest = m.id;
        }
      if (nearest.valid()) run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.deleteMarker(nearest); }); });
    } else if (e.mods.isShiftDown()) {  // Shift-click adds a marker; a plain click only auditions the chop under the cursor
      run([=](cf::composition::ProjectSession& s) {
        return s.editMarkers([=](cf::source::ChopMap& m) {
          auto r = m.addMarker(frame);
          return r.ok() ? cf::Status{} : cf::Status{r.error()};
        });
      });
    } else {
      for (const auto& c : chops_)
        if (frame >= c.range.start && frame < c.range.end) {
          proc_.audition(c.id);
          break;
        }
    }
  } else if (patternArea_.contains(pt)) {
    grabKeyboardFocus();
    selected_ = cf::EventId{};
    selectedHits_.clear();
    selectedChops_.clear();
    dragMode_ = DragMode::None;
    const double w = static_cast<double>(patternArea_.getWidth());
    const double fx = static_cast<double>(pt.x - patternArea_.getX()) / w;
    const int lane = cf::ui::laneAtY(static_cast<double>(pt.y - patternArea_.getY()), static_cast<double>(patternArea_.getHeight()), static_cast<int>(chops_.size()));
    for (auto it = pattern_.rects.rbegin(); it != pattern_.rects.rend(); ++it)
      if (it->chopIndex == lane && fx >= it->x0 && fx <= it->x1) {
        selected_ = it->id;
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
    if (!selected_.valid() && e.mods.isShiftDown() && lane >= 0 && gridTicks_ > 0 && barTicks_ > 0 && barCount_ > 0) {  // Shift-click on empty space adds a hit
      const cf::Ticks total = barTicks_ * barCount_;
      const auto pos = cf::ui::splitTick(cf::ui::snapDown(cf::ui::xToTick(static_cast<double>(pt.x - patternArea_.getX()), w, total), gridTicks_), barTicks_, barCount_);
      const cf::Ticks dur = std::min<cf::Ticks>(gridTicks_, barTicks_ - pos.startInBar);
      editSelected([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> cf::Result<cf::pattern::Pattern> {
        if (lane >= static_cast<int>(chops.chops.size())) return cf::makeError(cf::ErrorCode::OutOfRange, "no chop in that lane");
        return cf::pattern::addEvent(p, pos.bar, chops.chops[static_cast<std::size_t>(lane)].id, pos.startInBar, dur, chops);
      });
    }
    repaint();
  } else if (orbitArea_.contains(pt)) {
    const double R = std::min(orbitArea_.getWidth(), orbitArea_.getHeight()) / 2.0 - 4.0;
    const auto c = orbitArea_.getCentre();
    grabKeyboardFocus();
    selected_ = cf::ui::hitTest(orbit_, static_cast<double>(pt.x - c.x), static_cast<double>(pt.y - c.y), R);
    repaint();
  } else if (historyArea_.contains(pt)) {
    for (const auto& n : tree_) {
      const auto np = treePos(n);
      const int x = np.x, y = np.y;
      if (std::abs(pt.x - x) < 11 && std::abs(pt.y - y) < 11) {
        const cf::history::NodeId id = n.id;
        if (e.mods.isPopupMenu()) {
          historyMenu(id);
          break;
        }
        const auto when = proc_.hostPlaying.load() ? cf::composition::Quantize::LoopBoundary : cf::composition::Quantize::Immediate;
        run([=](cf::composition::ProjectSession& s) { return s.activate(id, when); });
        break;
      }
    }
  }
}

void ChopFractalEditor::mouseDoubleClick(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  if (!waveArea_.contains(pt) || sourceFrames_ <= 0) return;
  const cf::ui::ViewWindow w{0, sourceFrames_, static_cast<double>(waveArea_.getWidth())};
  const std::int64_t frame = cf::ui::xToFrame(w, static_cast<double>(pt.x - waveArea_.getX()));
  for (const auto& c : chops_)
    if (frame >= c.range.start && frame < c.range.end) {
      proc_.audition(c.id);  // plays through the output without touching the pattern playhead
      return;
    }
}

// ---------------------------------------------------------------------------------------------
// direct hit editing, presets, chaos reroll, drag-out
// ---------------------------------------------------------------------------------------------

void ChopFractalEditor::editSelected(const std::function<cf::Result<cf::pattern::Pattern>(const cf::pattern::Pattern&, const cf::ChopSnapshot&)>& fn) {
  run([=](cf::composition::ProjectSession& s) {
    cf::Status st = s.edit(fn);
    if (st.ok()) st = s.commitEdit("Edited hits");  // each finished gesture becomes a variation you can return to
    return st;
  });
}

void ChopFractalEditor::updateSectionSelection(juce::Point<int> point) {
  const auto area = selectingWave_ ? waveArea_ : patternArea_;
  selectionEnd_ = {juce::jlimit(area.getX(), area.getRight() - 1, point.x),
                   juce::jlimit(area.getY(), area.getBottom() - 1, point.y)};
  const auto rect = juce::Rectangle<int>(selectionStart_, selectionEnd_);
  selectedHits_.clear();
  selectedChops_.clear();
  selected_ = {};
  const bool click = rect.getWidth() < 3 && rect.getHeight() < 3;
  if (selectingWave_) {
    const double lo = static_cast<double>(std::min(selectionStart_.x, selectionEnd_.x) - area.getX()) / area.getWidth() * sourceFrames_;
    const double hi = static_cast<double>(std::max(selectionStart_.x, selectionEnd_.x) - area.getX()) / area.getWidth() * sourceFrames_;
    for (const auto& chop : chops_)
      if (click ? (lo >= chop.range.start && lo < chop.range.end) : (hi >= chop.range.start && lo < chop.range.end))
        selectedChops_.push_back(chop.id);
    for (const auto& hit : pattern_.rects)
      if (hit.depth == 0 && hit.chopIndex >= 0 && hit.chopIndex < static_cast<int>(chops_.size()) &&
          std::find(selectedChops_.begin(), selectedChops_.end(), chops_[static_cast<std::size_t>(hit.chopIndex)].id) != selectedChops_.end())
        selectedHits_.push_back(hit.id);
  } else {
    const float laneHeight = static_cast<float>(area.getHeight()) / static_cast<float>(std::max<std::size_t>(1, chops_.size()));
    for (const auto& hit : pattern_.rects) {
      const juce::Rectangle<float> bounds(
          area.getX() + static_cast<float>(hit.x0) * area.getWidth(),
          area.getY() + hit.chopIndex * laneHeight,
          static_cast<float>(hit.x1 - hit.x0) * area.getWidth(), laneHeight);
      if (hit.depth == 0 && (click ? bounds.contains(selectionEnd_.toFloat()) : bounds.intersects(rect.toFloat())))
        selectedHits_.push_back(hit.id);
    }
  }
  if (!selectedHits_.empty()) selected_ = selectedHits_.front();
  proc_.setStatusMessage(juce::String(static_cast<int>(selectedChops_.size())) + " source sections, " +
      juce::String(static_cast<int>(selectedHits_.size())) + " pattern hits selected. Zoom In subdivides selected hits.");
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
  if (gridTicks_ <= 0 || barTicks_ <= 0 || barCount_ <= 0) return;
  const cf::EventId id = dragId_;
  const cf::Ticks total = barTicks_ * barCount_;
  const double w = static_cast<double>(patternArea_.getWidth());
  const cf::Ticks grid = gridTicks_, barT = barTicks_;
  const int bars = barCount_;
  const cf::Ticks dTicks = static_cast<cf::Ticks>(std::llround(static_cast<double>(dx) / w * static_cast<double>(total)));
  const int newLane = cf::ui::laneAtY(static_cast<double>(dragNow_.y - patternArea_.getY()), static_cast<double>(patternArea_.getHeight()), static_cast<int>(chops_.size()));
  const bool copy = dragCopy_;
  const int oldLane = dragLane_;
  editSelected([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot& chops) -> cf::Result<cf::pattern::Pattern> {
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

bool ChopFractalEditor::keyPressed(const juce::KeyPress& k) {
  if (!selected_.valid()) return false;
  const auto mods = k.getModifiers();
  if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;  // leave the host's own shortcuts alone
  const cf::EventId id = selected_;
  if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) {
    nudgeSelected(k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1, k.getModifiers().isShiftDown());
    return true;
  }
  if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
    auto ids = selectedHits_;
    if (ids.empty()) ids.push_back(id);
    editSelected([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
      cf::pattern::Pattern edited = p;
      for (const auto hit : ids) {
        auto result = cf::pattern::deleteEvent(edited, hit);
        if (!result.ok()) return result.error();
        edited = std::move(result.value());
      }
      return edited;  // session commits all selected deletions as one undoable edit
    });
    return true;
  }
  if (k.getTextCharacter() == 'm' || k.getTextCharacter() == 'M') {
    auto ids = selectedHits_;
    if (ids.empty()) ids.push_back(id);
    editSelected([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
      cf::pattern::Pattern edited = p;
      for (const auto hit : ids) {
        const auto* ev = cf::pattern::findEvent(edited, hit);
        if (!ev) return cf::makeError(cf::ErrorCode::NotFound, "that hit no longer exists");
        auto result = cf::pattern::setEventEnabled(edited, hit, !ev->enabled);
        if (!result.ok()) return result.error();
        edited = std::move(result.value());
      }
      return edited;
    });
    return true;
  }
  if (k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D') {
    const cf::Ticks grid = gridTicks_;
    editSelected([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
      const cf::Event* ev = cf::pattern::findEvent(p, id);
      if (!ev) return cf::makeError(cf::ErrorCode::NotFound, "that hit no longer exists");
      return cf::pattern::duplicateEvent(p, id, ev->start + std::max<cf::Ticks>(grid, ev->duration));
    });
    return true;
  }
  return false;
}

void ChopFractalEditor::doReroll() {
  setSeed(static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64()) & 0x7fffffffffffffffULL);
  doGenerate();
}

cf::presets::Preset ChopFractalEditor::currentPreset(const juce::String& name) const {
  const cf::host::ParamValues v = proc_.currentParams();
  cf::presets::Preset p;
  p.name = name.toStdString();
  static const int kBars[] = {1, 2, 4, 8};
  p.bars = kBars[std::max(0, std::min(3, static_cast<int>(std::lround(v.get(cf::host::kPatternBars)))))];
  p.density = v.get(cf::host::kDensity);
  p.swing = v.get(cf::host::kSwing);
  p.variation = v.get(cf::host::kVariationAmount);
  p.fxIntensity = v.get(cf::host::kFxIntensity);
  p.chaos = v.get(cf::host::kChaos);
  p.allowReverse = v.get(cf::host::kAllowReverse) >= 0.5;
  p.allowPitch = v.get(cf::host::kAllowPitch) >= 0.5;
  p.allowRetrigger = v.get(cf::host::kAllowRetrigger) >= 0.5;
  p.allowFilter = v.get(cf::host::kAllowFilter) >= 0.5;
  p.allowGlide = v.get(cf::host::kAllowGlide) >= 0.5;
  p.allowCrunch = v.get(cf::host::kAllowCrunch) >= 0.5;
  p.motif = motif_.getText().toStdString();
  p.depth = std::max(1, std::min(3, depth_.getSelectedId()));
  p.mutation = fractalMutation_ >= 0.0 ? fractalMutation_ : v.get(cf::host::kVariationAmount) * 0.5;
  p.accentDecay = fractalAccent_;
  static const int kEvery[] = {1, 2, 4, 8, 16};
  p.evolveEvery = kEvery[std::max(0, std::min(4, every_.getSelectedId() - 1))];
  p.evolveAmount = evolveAmount_.getValue();
  return p;
}

void ChopFractalEditor::applyPreset(const cf::presets::Preset& p) {
  auto setParam = [this](const char* id, double plain) {
    if (auto* prm = proc_.apvts.getParameter(id)) {
      prm->beginChangeGesture();  // hosts in touch/latch automation modes expect a gesture around each change
      prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(plain)));
      prm->endChangeGesture();
    }
  };
  setParam("density", p.density);
  setParam("swing", p.swing);
  setParam("variation_amount", p.variation);
  setParam("fx_intensity", p.fxIntensity);
  setParam("chaos", p.chaos);
  setParam("allow_reverse", p.allowReverse ? 1.0 : 0.0);
  setParam("allow_pitch", p.allowPitch ? 1.0 : 0.0);
  setParam("allow_retrigger", p.allowRetrigger ? 1.0 : 0.0);
  setParam("allow_filter", p.allowFilter ? 1.0 : 0.0);
  setParam("allow_glide", p.allowGlide ? 1.0 : 0.0);
  setParam("allow_crunch", p.allowCrunch ? 1.0 : 0.0);
  const int barsIdx = p.bars == 1 ? 0 : p.bars == 2 ? 1 : p.bars == 4 ? 2 : 3;
  setParam("pattern_bars", barsIdx);
  motif_.setText(juce::String(p.motif), juce::dontSendNotification);
  depth_.setSelectedId(std::max(1, std::min(3, p.depth)), juce::dontSendNotification);
  fractalMutation_ = p.mutation;
  fractalAccent_ = p.accentDecay;
  static const int kEvery[] = {1, 2, 4, 8, 16};
  for (int i = 0; i < 5; ++i)
    if (kEvery[i] == p.evolveEvery) every_.setSelectedId(i + 1, juce::dontSendNotification);
  evolveAmount_.setValue(p.evolveAmount, juce::dontSendNotification);
  status_ = "Preset \"" + juce::String(p.name) + "\" loaded; press Generate to use it.";
  proc_.setStatusMessage(status_);
  repaint();
}

void ChopFractalEditor::reloadPresets() {
  int skipped = 0;
  presets_ = PresetManager::list(&skipped);
  preset_.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < presets_.size(); ++i) preset_.addItem(presets_[i].label, static_cast<int>(i) + 1);
  if (skipped > 0) {
    status_ = juce::String(skipped) + " preset file(s) could not be read and were skipped.";
    proc_.setStatusMessage(status_);
  }
}

void ChopFractalEditor::savePresetNamed(const juce::String& name, bool overwrite) {
  const cf::Status st = PresetManager::save(currentPreset(name.trim()), overwrite);
  status_ = st.ok() ? "Saved preset \"" + name.trim() + "\"" : juce::String(st.error().message);
  proc_.setStatusMessage(status_);
  reloadPresets();
  repaint();
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

void ChopFractalEditor::deleteSelectedPreset() {
  const int i = preset_.getSelectedId() - 1;
  if (i < 0 || i >= static_cast<int>(presets_.size()) || presets_[static_cast<std::size_t>(i)].factory) return;
  const cf::Status st = PresetManager::remove(presets_[static_cast<std::size_t>(i)].file);
  status_ = st.ok() ? "Deleted preset" : juce::String(st.error().message);
  proc_.setStatusMessage(status_);
  reloadPresets();
  deletePreset_.setEnabled(false);
  repaint();
}

juce::File ChopFractalEditor::makeDragFile(bool wav) {
  cf::Status status;
  std::vector<std::uint8_t> bytes;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (wav) {
      cf::composition::ExportWavOptions o;
      static const cf::wav::Format kFmt[] = {cf::wav::Format::Pcm16, cf::wav::Format::Pcm24, cf::wav::Format::Float32};
      static const int kRate[] = {44100, 48000, 88200, 96000};
      static const int kLoops[] = {1, 2, 4, 8, 16};
      o.wav.format = kFmt[std::max(0, std::min(2, exportFormat_.getSelectedId() - 1))];  // same choices as Export WAV
      o.wav.sampleRate = kRate[std::max(0, std::min(3, exportRate_.getSelectedId() - 1))];
      o.loops = kLoops[std::max(0, std::min(4, exportLoops_.getSelectedId() - 1))];
      o.bpm = proc_.activeBpm.load();
      auto r = s.renderWavBytes(o);
      if (r.ok()) bytes = std::move(r.value()); else status = r.error();
    } else {
      cf::composition::KitOptions k;
      k.bpm = proc_.activeBpm.load();
      auto r = s.renderMidi(k);
      if (r.ok()) bytes = std::move(r.value()); else status = r.error();
    }
  });
  juce::File f;
  if (status.ok()) {
    f = dragout::writeTemp("chopfractal", wav ? "wav" : "mid", bytes);
    if (f == juce::File()) status = cf::makeError(cf::ErrorCode::InvalidArgument, "could not write the temporary file");
  }
  status_ = status.ok() ? juce::String() : juce::String(status.error().message);
  proc_.setStatusMessage(status_);
  repaint();
  return f;
}

juce::Point<int> ChopFractalEditor::treePos(const cf::ui::TreeNodeView& n) const {
  const int stepX = treeCols_ > 0 ? std::min(64, std::max(6, (historyArea_.getWidth() - 60) / treeCols_)) : 64;
  const int stepY = treeRows_ > 0 ? std::min(30, std::max(6, (historyArea_.getHeight() - 40) / treeRows_)) : 30;
  return {historyArea_.getX() + 24 + n.column * stepX, historyArea_.getY() + 22 + n.depth * stepY};
}

// Keyboard equivalent of the drag gestures: arrows nudge the selected hit by one grid cell, Shift+arrows resize it.
void ChopFractalEditor::nudgeSelected(int cells, bool resize) {
  if (!selected_.valid() || gridTicks_ <= 0 || barTicks_ <= 0 || barCount_ <= 0) return;
  const cf::EventId id = selected_;
  const cf::Ticks grid = gridTicks_, barT = barTicks_;
  const int bars = barCount_;
  editSelected([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
    const cf::Event* ev = cf::pattern::findEvent(p, id);
    const auto loc = cf::pattern::locate(p, id);
    if (!ev || !loc) return cf::makeError(cf::ErrorCode::NotFound, "that hit no longer exists");
    if (resize) return cf::pattern::resizeEvent(p, id, std::max(std::min(grid, barT - ev->start), std::min(ev->duration + cells * grid, barT - ev->start)));
    const cf::Ticks abs = static_cast<cf::Ticks>(loc->bar) * barT + ev->start + cells * grid;
    if (abs < 0 || abs > barT * bars - grid) return cf::makeError(cf::ErrorCode::OutOfRange, "the hit is already at the edge of the pattern");
    const auto pos = cf::ui::splitTick(abs, barT, bars);
    return cf::pattern::moveEvent(p, id, pos.bar, pos.startInBar);
  });
}
