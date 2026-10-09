#include "PluginEditor.h"

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

ChopFractalEditor::ChopFractalEditor(ChopFractalProcessor& processor) : AudioProcessorEditor(&processor), proc_(processor) {
  setSize(940, 860);
  setResizable(false, false);

  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &generate_, &mutate_, &undo_, &redo_, &zoom_, &collapse_, &lock_, &dice_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  load_.onClick = [this] { chooseFile(); };
  detect_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.applyChops(cf::composition::DetectOptions{}); });
  };
  generate_.onClick = [this] { doGenerate(); };
  mutate_.onClick = [this] { doMutate(); };
  undo_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.undo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to undo.")}; });
  };
  redo_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.redo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to redo.")}; });
  };
  zoom_.onClick = [this] {
    const cf::EventId id = selected_;
    const std::uint64_t seed = currentSeed() + 1;
    setSeed(seed);
    run([=](cf::composition::ProjectSession& s) {
      if (!id.valid()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit in the pattern first.")};
      cf::zoom::Settings zs;
      zs.subdivisions = 4;
      zs.density = cf::zoom::Density::Balanced;
      return s.zoomIn(id, zs, seed);
    });
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

  refresh(true);
  startTimerHz(10);
}

ChopFractalEditor::~ChopFractalEditor() { stopTimer(); }

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
  auto sliders = area.removeFromTop(104);
  auto left = sliders.removeFromLeft(sliders.getWidth() / 2);
  density_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  swing_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  variation_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  fxIntensity_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
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
  featC.removeFromLeft(16);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&storeA_, &recallA_, &storeB_, &recallB_}) b->setBounds(featC.removeFromLeft(78).reduced(2));
  featC.removeFromLeft(16);
  role_.setBounds(featC.removeFromLeft(110).reduced(2));
  setRole_.setBounds(featC.removeFromLeft(80).reduced(2));
  area.removeFromTop(6);
  waveArea_ = area.removeFromTop(120);
  area.removeFromTop(6);
  patternArea_ = area.removeFromTop(230);
  area.removeFromTop(6);
  auto bottom = area.removeFromTop(area.getHeight() - 26);
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
  const double v = params.get(cf::host::kVariationAmount);
  base.variation = {v, v, v, v};
  cf::pattern::Settings s = cf::composition::settingsFromParams(params, base);
  s.maxShiftTicks = std::min<cf::Ticks>(20, cf::gridTicks(s.grid) / 8);
  run([=](cf::composition::ProjectSession& session) { return session.generate(s); });
}

void ChopFractalEditor::doMutate() {
  const cf::host::ParamValues params = proc_.currentParams();
  const std::uint64_t seed = currentSeed() + 1;
  setSeed(seed);
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
    cf::pattern::MutateOptions opt;
    opt.seed = seed;
    opt.amount = cf::composition::mutateAmountFromParams(params);
    return session.mutate(opt, &live);
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
  fs.mutation = proc_.currentParams().get(cf::host::kVariationAmount) * 0.5;
  fs.mirror = false;
  fs.accentDecay = 0.85;
  run([=](cf::composition::ProjectSession& s) { return s.applyFractal(fs); });
}

void ChopFractalEditor::doEvolve() {
  cf::evolve::Settings es;
  es.enabled = evolve_.getToggleState();
  static const int kEvery[] = {1, 2, 4, 8, 16};
  es.everyLoops = kEvery[std::max(0, std::min(4, every_.getSelectedId() - 1))];
  es.amount = evolveAmount_.getValue();
  es.startSeed = currentSeed();
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
                          f = f.withFileExtension("wav");
                          cf::composition::ExportWavOptions o;
                          static const cf::wav::Format kFmt[] = {cf::wav::Format::Pcm16, cf::wav::Format::Pcm24, cf::wav::Format::Float32};
                          static const int kRate[] = {44100, 48000, 88200, 96000};
                          static const int kLoops[] = {1, 2, 4, 8, 16};
                          o.wav.format = kFmt[std::max(0, std::min(2, exportFormat_.getSelectedId() - 1))];
                          o.wav.sampleRate = kRate[std::max(0, std::min(3, exportRate_.getSelectedId() - 1))];
                          o.loops = kLoops[std::max(0, std::min(4, exportLoops_.getSelectedId() - 1))];
                          o.bpm = proc_.activeBpm.load();
                          o.overwrite = true;  // the native dialog already asked
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
  m.showMenuAsync(juce::PopupMenu::Options(), [this, id](int choice) {
    if (choice == 1) {
      bool now = false;
      proc_.withSession([&](cf::composition::ProjectSession& s) {
        for (const auto& n : s.history().listAll())
          if (n.id == id) now = n.favorite;
      });
      run([=](cf::composition::ProjectSession& s) { return s.setFavorite(id, !now); });
    } else if (choice == 2) {
      auto* w = new juce::AlertWindow("Rename variation", "Name:", juce::MessageBoxIconType::NoIcon);
      w->addTextEditor("name", "");
      w->addButton("OK", 1);
      w->addButton("Cancel", 0);
      w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, id](int r) {
                           if (r == 1) {
                             const std::string name = w->getTextEditorContents("name").toStdString();
                             run([=](cf::composition::ProjectSession& s) { return s.renameNode(id, name); });
                           }
                         }),
                         true);
    } else if (choice == 3) {
      std::string text;
      cf::Status st;
      proc_.withSession([&](cf::composition::ProjectSession& s) {
        auto r = s.compareNodes(s.history().active(), id);
        if (r.ok()) text = r.value(); else st = r.error();
      });
      status_ = st.ok() ? juce::String(text) : juce::String(st.error().message);
      proc_.setStatusMessage(status_);
      repaint();
    } else if (choice == 4) {
      run([=](cf::composition::ProjectSession& s) { return s.deleteBranch(id); });
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
  const int bar = selectedBar();
  run([=](cf::composition::ProjectSession& s) {
    return s.edit([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
      const bool now = bar < static_cast<int>(p.bars.size()) && p.bars[static_cast<std::size_t>(bar)].locked;
      return cf::pattern::setLock(p, {cf::ScopeLevel::Bar, bar, 0, {}}, !now);
    });
  });
}

void ChopFractalEditor::refresh(bool force) {
  juce::String sig;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    sig << juce::String::toHexString(reinterpret_cast<std::uintptr_t>(s.playback().get())) << "|" << static_cast<int>(s.history().size()) << "|"
        << static_cast<juce::int64>(s.history().active()) << "|" << (s.chopMap() ? static_cast<int>(s.chopMap()->markers().size()) : -1);
    if (!force && sig == signature_) return;
    signature_ = sig;
    sourceFrames_ = 0;
    markers_.clear();
    chops_.clear();
    pattern_ = {};
    orbit_ = {};
    tree_.clear();
    const auto pb = s.playback();
    if (s.chopMap()) sourceFrames_ = s.chopMap()->info().frames;
    if (pb && pb->source && peaks_.min.empty()) {
      const auto ptrs = std::vector<const float*>{pb->source->channel(0), pb->source->channel(pb->source->channels - 1)};
      peaks_ = cf::ui::computePeaks(ptrs.data(), pb->source->channels, pb->source->frames, {0, pb->source->frames}, std::max(1, waveArea_.getWidth()));
    }
    if (!pb || !pb->source) peaks_ = {};
    if (s.chopMap()) {
      markers_ = cf::ui::buildMarkerViews(*s.chopMap(), cf::ui::ViewWindow{0, std::max<std::int64_t>(1, sourceFrames_), static_cast<double>(waveArea_.getWidth())});
      if (s.chops()) chops_ = s.chops()->chops;
      if (s.pattern() && s.chops()) {
        pattern_ = cf::ui::buildPatternView(*s.pattern(), *s.chops(), [&](cf::ChopId id) { return s.roleMap().roleOf(id); });
        orbit_ = cf::ui::buildOrbitView(*s.pattern(), *s.chops());
      }
    }
    tree_ = cf::ui::layoutHistoryTree(s.history().listAll(), s.history().active());
  });
  if (force || sig != signature_) repaint();
  else repaint();
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
    g.drawText("Load a loop (WAV or AIFF). Until a pattern is generated, the input passes through unchanged.", waveArea_, juce::Justification::centred);
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
    if (r.id == selected_) {
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
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(historyArea_);
  for (const auto& n : tree_) {
    const int x = historyArea_.getX() + 24 + n.column * 64;
    const int y = historyArea_.getY() + 22 + n.depth * 30;
    for (const auto& p : tree_)
      if (p.id == n.parent) {
        g.setColour(juce::Colours::grey);
        g.drawLine(static_cast<float>(historyArea_.getX() + 24 + p.column * 64), static_cast<float>(historyArea_.getY() + 22 + p.depth * 30), static_cast<float>(x), static_cast<float>(y));
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
    const double w = (R - 0.25 * R) / std::max(1, orbit_.rings);
    for (int ring = 0; ring < orbit_.rings; ++ring) g.drawText(juce::String(ring + 1), static_cast<int>(c.x) - 8, static_cast<int>(c.y - R + ring * w), 16, 10, juce::Justification::centred);
    if (posTicks >= 0.0) {
      const double a = cf::ui::playheadAngle(ph, orbit_.lengthTicks);
      g.setColour(juce::Colour(0xff7cf29a));
      g.drawLine(juce::Line<float>(pt(0.0, a), pt(R + 3.0, a)), 1.5f);
    }
  } else {
    g.setColour(juce::Colours::grey);
    g.drawText("Orbit view", orbitArea_, juce::Justification::centred);
  }

  // Status line: user-visible messages and host-fallback hints.
  juce::String line = status_.isNotEmpty() ? status_ : proc_.statusMessage();
  if (proc_.usedFallbackTempo.load()) line << (line.isEmpty() ? "" : "   |   ") << "No host tempo: using " << juce::String(proc_.manualBpm.load(), 1) << " BPM";
  if (proc_.usedFallbackMeter.load()) line << "   |   Assuming 4/4";
  g.setColour(juce::Colours::white.withAlpha(0.8f));
  g.drawText(line, getLocalBounds().removeFromBottom(24).reduced(10, 0), juce::Justification::centredLeft, true);
}

void ChopFractalEditor::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
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
    } else {
      run([=](cf::composition::ProjectSession& s) {
        return s.editMarkers([=](cf::source::ChopMap& m) {
          auto r = m.addMarker(frame);
          return r.ok() ? cf::Status{} : cf::Status{r.error()};
        });
      });
    }
  } else if (patternArea_.contains(pt)) {
    selected_ = cf::EventId{};
    const float w = static_cast<float>(patternArea_.getWidth());
    const double fx = static_cast<double>(pt.x - patternArea_.getX()) / static_cast<double>(w);
    const float laneH = static_cast<float>(patternArea_.getHeight()) / static_cast<float>(std::max<std::size_t>(1, chops_.size()));
    const int lane = static_cast<int>(static_cast<float>(pt.y - patternArea_.getY()) / laneH);
    for (auto it = pattern_.rects.rbegin(); it != pattern_.rects.rend(); ++it)
      if (it->chopIndex == lane && fx >= it->x0 && fx <= it->x1) {
        selected_ = it->id;
        break;
      }
    repaint();
  } else if (orbitArea_.contains(pt)) {
    const double R = std::min(orbitArea_.getWidth(), orbitArea_.getHeight()) / 2.0 - 4.0;
    const auto c = orbitArea_.getCentre();
    selected_ = cf::ui::hitTest(orbit_, static_cast<double>(pt.x - c.x), static_cast<double>(pt.y - c.y), R);
    repaint();
  } else if (historyArea_.contains(pt)) {
    for (const auto& n : tree_) {
      const int x = historyArea_.getX() + 24 + n.column * 64;
      const int y = historyArea_.getY() + 22 + n.depth * 30;
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
