#include "EditorCommands.h"

#include <algorithm>
#include <cmath>

#include <chopfractal/composition/preset_apply.hpp>
#include <chopfractal/plugin_ui_adapter/export_view.hpp>
#include <chopfractal/plugin_ui_adapter/hit_edit.hpp>
#include <chopfractal/plugin_ui_adapter/settings_view.hpp>

#include "BuildInfo.h"
#include "CdlTheme.h"
#include "DragOut.h"
#include "PresetManager.h"

namespace cf = chopfractal;

EditorCommands::EditorCommands(ChopFractalProcessor& processor, EditorModel& model) : proc_(processor), model_(model) {}

cf::Status EditorCommands::run(const std::function<cf::Status(cf::composition::ProjectSession&)>& action) {
  cf::Status status;
  std::vector<cf::composition::Notice> notices;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    status = action(s);
    notices = s.takeNotices();
  });
  auto severityOf = [](cf::composition::Notice::Level l) {
    return l == cf::composition::Notice::Level::Error ? Severity::Error : l == cf::composition::Notice::Level::Warning ? Severity::Warning : Severity::Info;
  };
  // Every notice of the action reaches the log; the last one (or the error) stays on the status line.
  for (std::size_t i = 0; i + 1 < notices.size(); ++i) proc_.noticeLog().add(severityOf(notices[i].level), juce::String(notices[i].text));
  if (!status.ok())
    model_.setStatus(juce::String(status.error().message), Severity::Error);
  else if (!notices.empty())
    model_.setStatus(juce::String(notices.back().text), severityOf(notices.back().level));
  else
    model_.setStatus({});
  model_.rebuild();
  return status;
}

// ---------------------------------------------------------------------------------------------
// transport and sources
// ---------------------------------------------------------------------------------------------

void EditorCommands::play() {
  const bool started = proc_.startPlayback();
  if (started && onMilestone) onMilestone(QuickStep::Play);
  model_.rebuild();
}

void EditorCommands::stop() {
  proc_.stopPlayback();
  model_.rebuild();
}

void EditorCommands::followHost() {
  proc_.followHostTransport();
  model_.rebuild();
}

void EditorCommands::chooseFile() {
  chooser_ = std::make_unique<juce::FileChooser>("Choose a loop (WAV or AIFF, up to 60 s)", juce::File(), "*.wav;*.aif;*.aiff");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
    const juce::File f = fc.getResult();
    if (f.existsAsFile()) requestLoad(f);
  });
}

bool EditorCommands::projectHasWork() {
  bool work = false;
  proc_.withSession([&work](cf::composition::ProjectSession& s) { work = s.pattern() != nullptr || s.history().size() > 0; });
  return work;
}

void EditorCommands::requestLoad(const juce::File& file) {
  auto load = [this, file](bool confirmed) {
    if (!confirmed) {
      model_.setStatus("Load cancelled. The current loop is unchanged.");
      return;
    }
    proc_.stopPlayback();
    model_.clearSelection();
    proc_.loadFileAsync(file);
    model_.rebuild();
  };
  if (!projectHasWork()) {
    load(true);
    return;
  }
  if (confirmReplace) {
    confirmReplace(load);
    return;
  }
  juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::QuestionIcon, "Replace the current loop?",
                                                                            "Loading a new loop resets the markers, pattern and family tree of this project.", "Replace", "Cancel"),
                               [load](int result) { load(result == 1); });  // OK is 1, Cancel is 0
}

void EditorCommands::locateSource() {
  chooser_ = std::make_unique<juce::FileChooser>("Locate the project's original audio file", juce::File(), "*.wav;*.aif;*.aiff");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
    const juce::File f = fc.getResult();
    if (f.existsAsFile()) proc_.loadFileAsync(f, true);
  });
}

bool EditorCommands::isAudioFile(const juce::File& file) { return file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3"); }

void EditorCommands::handleDroppedFiles(const juce::StringArray& files) {
  int accepted = 0;
  for (const auto& path : files)
    if (isAudioFile(juce::File(path))) ++accepted;
  if (accepted > 1) proc_.noticeLog().add(Severity::Info, "Only the first file was loaded.");
  for (const auto& path : files) {
    const juce::File file(path);
    if (!isAudioFile(file)) continue;
    if (!file.existsAsFile()) {
      proc_.setStatusMessage("The dropped sample could not be found.", Severity::Warning);
      model_.rebuild();
      return;
    }
    requestLoad(file);
    return;  // a ChopFractal project has one source loop
  }
  proc_.setStatusMessage("Drop an audio sample (WAV, AIFF, FLAC, OGG or MP3). One source loop is loaded at a time.");
  model_.rebuild();
}

// ---------------------------------------------------------------------------------------------
// generation and history
// ---------------------------------------------------------------------------------------------

cf::composition::DetectOptions EditorCommands::detectOptionsFor(int mode, cf::composition::ProjectSession& s) const {
  const int hostBeats = proc_.hostBeatsPerBar.load(), hostUnit = std::max(1, proc_.hostBeatUnit.load());
  static const int kBars[] = {1, 2, 4, 8};
  const int loopBars = kBars[std::max(0, std::min(3, static_cast<int>(std::lround(proc_.currentParams().get(cf::host::kPatternBars)))))];
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
  return d;
}

void EditorCommands::detectChops(DetectChoice choice) {
  const int mode = static_cast<int>(choice);
  run([=](cf::composition::ProjectSession& s) { return s.applyChops(detectOptionsFor(mode, s)); });
}

void EditorCommands::generate() {
  const cf::host::ParamValues params = proc_.currentParams();
  cf::pattern::Settings base;
  base.seed = model_.seed();
  base.grid = {model_.extras().gridDivision, model_.extras().gridTriplet};  // the persisted generation grid (default 1/16 straight)
  const cf::TimeSignature hostMeter{proc_.hostBeatsPerBar.load(), proc_.hostBeatUnit.load()};
  if (hostMeter.valid()) base.timeSignature = hostMeter;  // bars follow the host's meter (4/4 when it gives none)
  const double v = params.get(cf::host::kVariationAmount);
  base.variation = {v, v, v, v};
  cf::pattern::Settings s = cf::composition::settingsFromParams(params, base);
  s.brk.tempoHint = std::min(999.0, std::max(20.0, proc_.activeBpm.load()));  // turns the engine's millisecond timing into ticks
  s.maxShiftTicks = std::min<cf::Ticks>(20, cf::gridTicks(s.grid) / 8);
  run([=](cf::composition::ProjectSession& session) { return session.generate(s); });
}

void EditorCommands::mutate() { mutateImpl(-1.0, {}); }

void EditorCommands::mutateAll() { mutateImpl(1.0, "Mutate (rules)"); }

void EditorCommands::mutateImpl(double amountOverride, const std::string& label) {
  const cf::host::ParamValues params = proc_.currentParams();
  const std::uint64_t seed = model_.seed() + 1;
  const double tempo = std::min(999.0, std::max(20.0, proc_.activeBpm.load()));
  bool hasPattern = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) { hasPattern = s.pattern() != nullptr; });
  if (hasPattern) model_.setSeed(seed);  // a refused Mutate does not consume a seed
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
    opt.amount = amountOverride >= 0.0 ? amountOverride : cf::composition::mutateAmountFromParams(params);
    cf::composition::applyBreakParams(params, live, false);  // profile, anchors, groove, fill; the engine version stays the pattern's own
    live.brk.tempoHint = tempo;
    live = cf::composition::applyChaos(live, params.get(cf::host::kChaos));
    return session.mutate(opt, &live, label);
  });
}

void EditorCommands::reroll() {
  model_.setSeed(static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64()) & 0x7fffffffffffffffULL);
  generate();
}

void EditorCommands::undo() {
  run([](cf::composition::ProjectSession& s) { return s.undo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to undo.")}; });
}

void EditorCommands::redo() {
  run([](cf::composition::ProjectSession& s) { return s.redo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to redo.")}; });
}

void EditorCommands::zoomInSelected() {
  auto ids = model_.selectedHits();
  if (ids.empty() && model_.selected().valid()) ids.push_back(model_.selected());
  const std::uint64_t seed = model_.seed() + 1;
  if (!ids.empty()) model_.setSeed(seed);
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
    model_.setStatus("Zoomed " + juce::String(static_cast<int>(ids.size())) + " selected hits into nested rhythms. Collapse returns to the parent hit.");
    model_.rebuild();
  }
}

void EditorCommands::collapseSelected() {
  const cf::EventId id = model_.selected();
  run([=](cf::composition::ProjectSession& s) {
    if (!id.valid()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit in the pattern first.")};
    return s.collapse(id);
  });
}

void EditorCommands::toggleLockSelectedBar() {
  const auto& rects = model_.view().pattern.rects;
  bool found = false;
  for (const auto& r : rects) found = found || r.id == model_.selected();
  if (!found) {
    model_.setStatus("Select a hit first; Lock Bar locks the bar that hit is in.");
    return;
  }
  const int bar = model_.selectedBar();
  run([=](cf::composition::ProjectSession& s) {
    return s.edit([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
      const bool now = bar < static_cast<int>(p.bars.size()) && p.bars[static_cast<std::size_t>(bar)].locked;
      return cf::pattern::setLock(p, {cf::ScopeLevel::Bar, bar, 0, {}}, !now);
    });
  });
}

void EditorCommands::newSeed() { model_.setSeed(juce::Random::getSystemRandom().nextInt64() & 0x7fffffff); }

void EditorCommands::storeSnapshot(std::size_t slot) {
  run([=](cf::composition::ProjectSession& s) { return s.storeSnapshot(slot); });
}

void EditorCommands::recallSnapshot(std::size_t slot) {
  run([=](cf::composition::ProjectSession& s) { return s.recallSnapshot(slot); });
}

void EditorCommands::setRoleOfSelected(const juce::String& role) {
  const int idx = model_.selectedChopIndex();
  run([=](cf::composition::ProjectSession& s) {
    if (idx < 0 || idx >= static_cast<int>(s.chops()->chops.size())) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit first; its chop gets the role (used by the next Generate or Mutate).")};
    return s.assignRole(s.chops()->chops[static_cast<std::size_t>(idx)].id, role.toStdString());
  });
}

void EditorCommands::activateNode(cf::history::NodeId id) {
  const auto when = proc_.hostPlaying.load() ? cf::composition::Quantize::LoopBoundary : cf::composition::Quantize::Immediate;
  const auto st = run([=](cf::composition::ProjectSession& s) { return s.activate(id, when); });
  if (!st.ok()) return;
  // Queued during playback: say so now; the model says "Switched to" when the loop boundary applies it.
  juce::String label = "#" + juce::String(static_cast<juce::int64>(id));
  bool queued = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    queued = s.pendingActivationNode() == id;
    for (const auto& n : s.history().listAll())
      if (n.id == id && !n.label.empty()) label = juce::String(n.label);
  });
  model_.setStatus(queued ? "Variation \"" + label + "\" will start at the next loop." : "Switched to \"" + label + "\".");
}

void EditorCommands::historyMenu(cf::history::NodeId id, juce::Component* anchor) {
  juce::PopupMenu m;
  m.addItem(1, "Favorite / unfavorite");
  m.addItem(2, "Rename...");
  m.addItem(3, "Compare with active variation");
  m.addItem(4, "Delete this branch");
  juce::Component::SafePointer<juce::Component> alive(anchor);  // the panel lives exactly as long as this object
  m.showMenuAsync(juce::PopupMenu::Options(), [this, alive, id](int choice) {
    if (alive == nullptr) return;
    if (choice == 1) {
      bool now = false;
      proc_.withSession([&](cf::composition::ProjectSession& s) {
        for (const auto& n : s.history().listAll())
          if (n.id == id) now = n.favorite;
      });
      run([=](cf::composition::ProjectSession& s) { return s.setFavorite(id, !now); });
    } else if (choice == 2) {
      juce::String currentName;
      proc_.withSession([&](cf::composition::ProjectSession& s) {
        for (const auto& n : s.history().listAll())
          if (n.id == id) currentName = juce::String(n.label);
      });
      auto* w = new juce::AlertWindow("Rename variation", "Name:", juce::MessageBoxIconType::NoIcon);
      w->addTextEditor("name", currentName);
      w->addButton("OK", 1);
      w->addButton("Cancel", 0);
      w->enterModalState(true, juce::ModalCallbackFunction::create([this, alive, w, id](int r) {
                           if (r == 1 && alive != nullptr) {
                             const std::string name = w->getTextEditorContents("name").toStdString();
                             if (name.empty()) return;
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
      model_.setStatus(st.ok() ? juce::String(text) : juce::String(st.error().message));
    } else if (choice == 4) {
      run([=](cf::composition::ProjectSession& s) { return s.deleteBranch(id); });
    }
  });
}

// ---------------------------------------------------------------------------------------------
// Smart Setup, Fractal, Evolve, export
// ---------------------------------------------------------------------------------------------

void EditorCommands::smartSetup() {
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
  model_.setSetup(found);
  int confident = 0;
  for (const auto& r : found.roles) confident += (r.role != cf::insight::Role::Other && r.confidence >= 0.5);
  juce::String status = juce::String(confident) + " of " + juce::String(static_cast<int>(found.roles.size())) + " chops have a role suggestion (Accept Roles applies them to untagged chops).";
  if (!found.loops.empty()) status << " Best loop: " << juce::String(found.loops.front().note) << " (Use Tempo copies it to the manual tempo).";
  model_.setStatus(status);
}

void EditorCommands::acceptRoles() {
  const auto roles = model_.setup().roles;
  run([=](cf::composition::ProjectSession& s) {
    auto r = s.acceptRoleSuggestions(roles, 0.5);
    return r.ok() ? cf::Status{} : cf::Status{r.error()};
  });
}

void EditorCommands::useTempo() {
  if (model_.haveSetup() && !model_.setup().loops.empty()) {
    const double bpm = model_.setup().loops.front().bpm;
    model_.setManualBpm(bpm);  // only the manual fallback; the host's tempo is never changed
    model_.setStatus("Manual tempo set to " + juce::String(bpm, 1) + " BPM (used when the host gives no tempo).");
  }
}

void EditorCommands::applyFractal(const FractalUi& ui) {
  cf::fractal::Settings fs;
  fs.motif = ui.motif;
  fs.depth = ui.depth;
  fs.seed = model_.seed();
  fs.mutation = ui.mutation >= 0.0 ? ui.mutation : proc_.currentParams().get(cf::host::kVariationAmount) * 0.5;
  fs.mirror = false;
  fs.accentDecay = ui.accent;
  run([=](cf::composition::ProjectSession& s) { return s.applyFractal(fs); });
}

bool EditorCommands::setEvolve(const EvolveUi& ui, cf::evolve::Settings& pushed) {
  cf::evolve::Settings es = pushed;  // keeps ramp and ceiling from a restored project
  proc_.withSession([&](cf::composition::ProjectSession& s) { es = s.evolveSettings(); });
  es.enabled = ui.enabled;
  static const int kEvery[] = {1, 2, 4, 8, 16};
  es.everyLoops = kEvery[std::max(0, std::min(4, ui.everyIndex))];
  es.amount = ui.amount;
  if (!es.enabled || !pushed.enabled) es.startSeed = model_.seed();
  pushed = es;
  run([=](cf::composition::ProjectSession& s) { return s.setEvolve(es); });
  bool running = false;
  if (es.enabled) proc_.withSession([&](cf::composition::ProjectSession& s) { running = s.evolveRunning(); });
  return running;
}

void EditorCommands::keepEvolved() {
  run([](cf::composition::ProjectSession& s) { return s.keepEvolved(); });
}

ExportUi EditorCommands::exportUi() const {
  const auto& e = model_.extras().exportUi;
  ExportUi ui;
  ui.formatIndex = e.formatIndex;
  ui.rateIndex = e.rateIndex;
  ui.loopsIndex = e.loopsIndex;
  ui.normalize = normalizeExport_;
  return ui;
}

void EditorCommands::setExportUi(const ExportUi& ui) {
  normalizeExport_ = ui.normalize;
  model_.setExtras([&ui](cf::composition::ProjectExtras& e) {
    e.exportUi.formatIndex = std::max(0, std::min(2, ui.formatIndex));
    e.exportUi.rateIndex = std::max(0, std::min(3, ui.rateIndex));
    e.exportUi.loopsIndex = std::max(0, std::min(4, ui.loopsIndex));
  });
}

void EditorCommands::setMidiSettings(const cf::composition::MidiExportSettings& m) {
  model_.setExtras([&m](cf::composition::ProjectExtras& e) { e.exportUi.midi = m; });
}

cf::composition::ExportWavOptions EditorCommands::wavOptions(const ExportUi& ui) const {
  cf::composition::ExportWavOptions o;
  static const cf::wav::Format kFmt[] = {cf::wav::Format::Pcm16, cf::wav::Format::Pcm24, cf::wav::Format::Float32};
  o.wav.format = kFmt[std::max(0, std::min(2, ui.formatIndex))];
  o.wav.sampleRate = cf::ui::exportRateHz(ui.rateIndex);
  o.loops = cf::ui::exportLoopCount(ui.loopsIndex);
  o.wav.normalize = ui.normalize;
  o.wav.ceilingDb = -1.f;
  o.bpm = proc_.activeBpm.load();
  return o;
}

cf::composition::KitOptions EditorCommands::kitOptions() const {
  const auto& m = model_.extras().exportUi.midi;
  cf::composition::KitOptions k;
  k.baseNote = m.baseNote;
  k.curve = m.curve;
  k.fixedVelocity = m.fixedVelocity;
  k.channel = m.channel;
  k.loops = m.loops;
  k.bpm = proc_.activeBpm.load();
  return k;
}

cf::composition::MidiExportOptions EditorCommands::midiOptions() const {
  const auto& m = model_.extras().exportUi.midi;
  cf::composition::MidiExportOptions o;
  o.baseNote = m.baseNote;
  o.curve = m.curve;
  o.fixedVelocity = m.fixedVelocity;
  o.channel = m.channel;
  o.loops = m.loops;
  o.bpm = proc_.activeBpm.load();
  return o;
}

void EditorCommands::exportWav(const ExportUi& ui) {
  chooser_ = std::make_unique<juce::FileChooser>("Export the pattern as WAV", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("chopfractal.wav"), "*.wav");
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, ui](const juce::FileChooser& fc) {
                          juce::File f = fc.getResult();
                          if (f == juce::File()) return;
                          const bool extensionAdded = !f.hasFileExtension("wav");
                          if (extensionAdded) f = f.getParentDirectory().getChildFile(f.getFileName() + ".wav");  // "my.loop" becomes "my.loop.wav", not a WAV named .loop
                          ExportRequest req;
                          req.kind = ExportRequest::Kind::Wav;
                          req.wav = wavOptions(ui);
                          req.wav.overwrite = !extensionAdded;  // the native dialog asked about the name as typed; a name we extended is checked again
                          req.path = f.getFullPathName().toStdString();
                          startExport(req);
                        });
}

void EditorCommands::exportKit() {
  chooser_ = std::make_unique<juce::FileChooser>("Choose a folder for the Producer Kit", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
    const juce::File dir = fc.getResult();
    if (dir == juce::File()) return;
    ExportRequest req;
    req.kind = ExportRequest::Kind::Kit;
    req.kit = kitOptions();
    req.path = dir.getFullPathName().toStdString();
    startExport(req);  // refuses before writing if any file already exists
  });
}

// Starts the worker; the progress text, the final message and the Cancel Export button come from the processor and the
// 10 Hz refresh. A request that cannot start (nothing to render, file exists, another export running) says why.
cf::Status EditorCommands::startExport(const ExportRequest& request) {
  const cf::Status status = proc_.startExport(request);
  if (!status.ok()) model_.setStatus(juce::String(status.error().message));
  model_.rebuild();  // publishes the running flag at once (Export buttons disable, Cancel Export appears)
  return status;
}

void EditorCommands::cancelExport() { proc_.cancelExport(); }

juce::File EditorCommands::makeDragFile(bool wav, const ExportUi& ui) {
  cf::Status status;
  std::vector<std::uint8_t> bytes;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (wav) {
      auto r = s.renderWavBytes(wavOptions(ui));
      if (r.ok()) bytes = std::move(r.value()); else status = r.error();
    } else {
      auto r = s.renderCurrentMidi(midiOptions());
      if (r.ok()) bytes = std::move(r.value()); else status = r.error();
    }
  });
  juce::File f;
  if (status.ok()) {
    f = dragout::writeTemp("chopfractal", wav ? "wav" : "mid", bytes);
    if (f == juce::File()) status = cf::makeError(cf::ErrorCode::InvalidArgument, "could not write the temporary file");
  }
  model_.setStatus(status.ok() ? juce::String() : juce::String(cf::ui::dragRenderErrorMessage(status.error().message)));
  return f;
}

std::unique_ptr<dragout::DragFileSet> EditorCommands::makeKitDragSet() {
  cf::Result<std::vector<cf::composition::KitFile>> files = std::vector<cf::composition::KitFile>{};
  proc_.withSession([&](cf::composition::ProjectSession& s) { files = s.renderKitFiles(kitOptions()); });
  auto set = std::make_unique<dragout::DragFileSet>();
  if (!files.ok()) {
    model_.setStatus(juce::String(cf::ui::dragRenderErrorMessage(files.error().message)));
    return set;
  }
  for (const auto& f : files.value()) set->add(juce::String(f.name), f.bytes);
  if (set->failed()) model_.setStatus(juce::String(cf::ui::dragTempFileErrorMessage()));
  return set;
}

// ---------------------------------------------------------------------------------------------
// arrangement export (F18)
// ---------------------------------------------------------------------------------------------

ArrangementState EditorCommands::arrangementState() {
  ArrangementState st;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    st.arrangement = s.arrangement();
    st.rows = s.arrangementRows();
    int n = 0;
    for (const auto& sc : s.sceneList().scenes) {
      ++n;
      const std::string prefix = "Scene " + std::to_string(n);
      st.scenes.push_back({sc.id, sc.name.compare(0, prefix.size(), prefix) == 0 ? sc.name : prefix + " " + sc.name});
    }
    st.hasSource = s.hasSource();
    if (const auto* job = s.arrangementJob()) {
      st.running = !job->done();
      const auto pr = job->progress();
      st.jobSection = pr.section;
      st.jobSections = pr.sections;
      st.jobFraction = pr.fraction;
    }
  });
  return st;
}

bool EditorCommands::editArrangement(const std::function<void(cf::composition::Arrangement&)>& edit) {
  cf::Status st;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    cf::composition::Arrangement a = s.arrangement();
    edit(a);
    st = s.setArrangement(a);
  });
  if (!st.ok()) model_.setStatus(juce::String(st.error().message));
  return st.ok();
}

void EditorCommands::arrangementFromChain() { run([](cf::composition::ProjectSession& s) { return s.fillArrangementFromChain(); }); }
void EditorCommands::autoFillArrangement() { run([](cf::composition::ProjectSession& s) { return s.autoFillArrangement(); }); }

cf::Status EditorCommands::startArrangementExport(const std::string& folder) {
  const ExportUi ui = exportUi();
  cf::composition::ArrangementExportSettings st;
  st.root = folder;
  st.bpm = proc_.activeBpm.load();
  st.wavOptions = wavOptions(ui).wav;
  cf::Status status;
  proc_.withSession([&](cf::composition::ProjectSession& s) { status = s.startArrangementExport(s.arrangement(), "chopfractal", st); });
  model_.setStatus(status.ok() ? juce::String(cf::ui::arrangementProgressMessage(1, 1, 0.0)) : juce::String(status.error().message));
  return status;
}

void EditorCommands::exportArrangement() {
  chooser_ = std::make_unique<juce::FileChooser>("Choose a folder for the arrangement", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
    const juce::File dir = fc.getResult();
    if (dir != juce::File()) startArrangementExport(dir.getFullPathName().toStdString());
  });
}

void EditorCommands::cancelArrangement() { proc_.withSession([](cf::composition::ProjectSession& s) { s.cancelArrangementExport(); }); }

bool EditorCommands::pollArrangement() {
  bool running = false, finished = false;
  cf::composition::ArrangementResult result;
  cf::composition::ArrangementProgress progress;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    auto* job = s.arrangementJob();
    if (!job) return;
    if (!job->done()) {
      running = true;
      progress = job->progress();
      return;
    }
    result = job->result();
    finished = true;
    s.releaseArrangementJob();
  });
  if (running) {
    model_.setProgress(juce::String(cf::ui::arrangementProgressMessage(std::max(1, progress.section), std::max(1, progress.sections), progress.fraction)));
  } else if (finished) {
    if (result.status.ok() && onMilestone) onMilestone(QuickStep::Export);
    if (result.status.ok())
      model_.setStatus(juce::String(cf::ui::arrangementExportedMessage(juce::File(juce::String(result.wavPath)).getFileName().toStdString(), result.bars, result.seconds)));
    else if (result.status.error().code == cf::ErrorCode::Cancelled)
      model_.setStatus(juce::String(cf::ui::arrangementCancelledMessage()));
    else
      model_.setStatus(juce::String(result.status.error().message));
  }
  return running;
}

// ---------------------------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------------------------

cf::presets::Preset EditorCommands::currentPreset(const juce::String& name, const FractalUi& fractal, const EvolveUi& evolve) const {
  cf::composition::ProjectExtras extras = model_.extras();
  extras.motif = fractal.motif;
  extras.fractalDepth = std::max(1, std::min(3, fractal.depth));
  extras.fractalMutation = fractal.mutation;
  extras.fractalAccentDecay = fractal.accent;
  cf::roles::RuleSet rules;
  proc_.withSession([&](cf::composition::ProjectSession& s) { rules = s.rules(); });
  static const int kEvery[] = {1, 2, 4, 8, 16};
  return cf::composition::capturePreset(name.toStdString(), proc_.currentParams(), extras, rules, kEvery[std::max(0, std::min(4, evolve.everyIndex))], evolve.amount);
}

void EditorCommands::setParamPlain(const std::string& id, double plain) {
  if (auto* prm = proc_.apvts.getParameter(id)) {
    prm->beginChangeGesture();  // hosts in touch/latch automation modes expect a gesture around each change
    prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(plain)));
    prm->endChangeGesture();
  }
}

double EditorCommands::setParameter(const std::string& id, double plain) {
  const auto* def = cf::host::findParam(id);
  if (def == nullptr || proc_.apvts.getParameter(id) == nullptr) return std::nan("");
  setParamPlain(id, cf::host::snapValue(*def, plain));
  return parameter(id);
}

double EditorCommands::parameter(const std::string& id) const {
  if (auto* prm = proc_.apvts.getParameter(id)) return static_cast<double>(prm->convertFrom0to1(prm->getValue()));
  return std::nan("");
}

bool EditorCommands::applyPreset(const cf::presets::Preset& p, bool generateAfter) {
  const cf::Status valid = cf::presets::validate(p);
  if (!valid.ok()) {
    model_.setStatus(juce::String(valid.error().message));
    return false;
  }
  const cf::composition::PresetParams pp = cf::composition::presetParams(p);
  PresetUndo undo;
  for (const auto& v : pp.values)
    if (auto* prm = proc_.apvts.getParameter(v.id)) undo.params.push_back({v.id, static_cast<double>(prm->convertFrom0to1(prm->getValue()))});
  undo.extras = model_.extras();
  proc_.withSession([&](cf::composition::ProjectSession& s) { undo.rules = s.rules(); });
  undo.valid = true;
  for (const auto& v : pp.values) setParamPlain(v.id, v.value);
  const cf::Status st = run([&p](cf::composition::ProjectSession& s) { return cf::composition::applyPresetToSession(s, p); });
  if (!st.ok()) {  // all or nothing: put the parameters back
    for (const auto& v : undo.params) setParamPlain(v.id, v.value);
    return false;
  }
  presetUndo_ = std::move(undo);
  juce::String msg = "Preset \"" + juce::String(p.name) + "\" applied.";
  if (pp.ignored > 0) msg = juce::String(pp.ignored) + " setting(s) from a newer version were ignored. " + msg;
  model_.setStatus(msg);
  if (generateAfter) {
    generate();
    model_.setStatus("Preset \"" + juce::String(p.name) + "\" applied and generated.");
  }
  return true;
}

bool EditorCommands::undoPreset() {
  if (!presetUndo_.valid) return false;
  const PresetUndo u = std::move(presetUndo_);
  presetUndo_ = {};
  for (const auto& v : u.params) setParamPlain(v.id, v.value);
  run([&u](cf::composition::ProjectSession& s) {
    cf::Status st = s.setExtras(u.extras);
    if (!st.ok()) return st;
    return s.setRules(u.rules);
  });
  model_.setStatus("Preset undone.");
  return true;
}

void EditorCommands::savePreset(const cf::presets::Preset& p, bool overwrite) {
  const cf::Status st = PresetManager::save(p, overwrite);
  model_.setStatus(st.ok() ? "Saved preset \"" + juce::String(p.name) + "\"" : juce::String(st.error().message));
}

void EditorCommands::deletePreset(const juce::File& file) {
  const cf::Status st = PresetManager::remove(file);
  model_.setStatus(st.ok() ? "Deleted preset" : juce::String(st.error().message));
}

// ---------------------------------------------------------------------------------------------
// canvas gestures
// ---------------------------------------------------------------------------------------------

void EditorCommands::audition(cf::ChopId id) {
  proc_.audition(id);
  if (onMilestone) onMilestone(QuickStep::Play);
}

void EditorCommands::auditionFrame(std::int64_t frame) {
  for (const auto& c : model_.view().chops)
    if (frame >= c.range.start && frame < c.range.end) {
      proc_.audition(c.id);  // plays through the output without touching the pattern playhead
      if (onMilestone) onMilestone(QuickStep::Play);
      return;
    }
}

void EditorCommands::addMarkerAt(std::int64_t frame) {
  run([=](cf::composition::ProjectSession& s) {
    return s.editMarkers([=](cf::source::ChopMap& m) {
      auto r = m.addMarker(frame);
      return r.ok() ? cf::Status{} : cf::Status{r.error()};
    });
  });
}

void EditorCommands::removeMarkerNear(double xPx, double tolerancePx) {
  cf::ChopId nearest;
  double best = tolerancePx;
  for (const auto& m : model_.view().markers)
    if (std::fabs(m.x - xPx) < best) {
      best = std::fabs(m.x - xPx);
      nearest = m.id;
    }
  if (nearest.valid()) run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.deleteMarker(nearest); }); });
}

void EditorCommands::editHits(const std::function<cf::Result<cf::pattern::Pattern>(const cf::pattern::Pattern&, const cf::ChopSnapshot&)>& fn) {
  run([=](cf::composition::ProjectSession& s) {
    cf::Status st = s.edit(fn);
    if (st.ok()) st = s.commitEdit("Edited hits");  // each finished gesture becomes a variation you can return to
    return st;
  });
}

// Keyboard equivalent of the drag gestures: arrows nudge the selected hit by one grid cell, Shift+arrows resize it.
void EditorCommands::nudgeSelected(int cells, bool resize) {
  const auto& v = model_.view();
  if (!model_.selected().valid() || v.gridTicks <= 0 || v.barTicks <= 0 || v.barCount <= 0) return;
  const cf::EventId id = model_.selected();
  const cf::Ticks grid = v.gridTicks, barT = v.barTicks;
  const int bars = v.barCount;
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
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

void EditorCommands::deleteSelected() {
  auto ids = model_.selectedHits();
  if (ids.empty()) ids.push_back(model_.selected());
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
    cf::pattern::Pattern edited = p;
    for (const auto hit : ids) {
      auto result = cf::pattern::deleteEvent(edited, hit);
      if (!result.ok()) return result.error();
      edited = std::move(result.value());
    }
    return edited;  // session commits all selected deletions as one undoable edit
  });
}

void EditorCommands::toggleMuteSelected() {
  auto ids = model_.selectedHits();
  if (ids.empty()) ids.push_back(model_.selected());
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
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
}

void EditorCommands::duplicateSelected() {
  const cf::EventId id = model_.selected();
  const cf::Ticks grid = model_.view().gridTicks;
  editHits([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
    const cf::Event* ev = cf::pattern::findEvent(p, id);
    if (!ev) return cf::makeError(cf::ErrorCode::NotFound, "that hit no longer exists");
    return cf::pattern::duplicateEvent(p, id, ev->start + std::max<cf::Ticks>(grid, ev->duration));
  });
}

// ---------------------------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------------------------

juce::String EditorCommands::diagnosticsText() const {
  cf::ui::DiagnosticsInput d;
  d.version = cfbuild::kVersionNum;
  switch (proc_.wrapperType) {
    case juce::AudioProcessor::wrapperType_VST3: d.format = "VST3"; break;
    case juce::AudioProcessor::wrapperType_Standalone: d.format = "Standalone"; break;
    case juce::AudioProcessor::wrapperType_Undefined: d.format = "CLAP or other"; break;
    default: d.format = "Plugin"; break;
  }
  d.host = juce::PluginHostType().getHostDescription();
  if (d.host == "Unknown") d.host.clear();
  d.sampleRate = proc_.getSampleRate();
  d.blockSize = proc_.getBlockSize();
  d.theme = cdl::currentTheme() == cdl::ThemeId::Concrete ? "Concrete" : "Night";
  const auto u = cdl::loadUserSettings();
  d.uiScalePercent = u.uiScalePercent;
  d.reducedMotion = u.reducedMotion;
  d.advanced = u.advanced;
  d.windowWidth = u.width;
  d.windowHeight = u.height;
  const auto& v = model_.view();
  d.hasSource = v.hasSource;
  d.chops = static_cast<int>(v.chops.size());
  d.events = static_cast<int>(v.pattern.rects.size());
  d.bars = v.barCount;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (const auto* m = s.chopMap()) {
      d.sourceName = m->info().name;
      d.sourceSeconds = m->info().sampleRate > 0 ? static_cast<double>(m->info().frames) / m->info().sampleRate : 0.0;
    }
  });
  d.bpm = model_.activeBpm();
  d.localTempo = model_.usedFallbackTempo();
  const auto pv = proc_.currentParams();
  d.limiterOn = pv.get(cf::host::kLimiterEnable) >= 0.5;
  d.levelMatchOn = pv.get(cf::host::kLevelMatch) >= 0.5;
  d.limiterCeilingDb = pv.get(cf::host::kLimiterCeilingDb);
  return juce::String(cf::ui::buildDiagnosticsText(d));
}

void EditorCommands::copyDiagnostics() {
  juce::SystemClipboard::copyTextToClipboard(diagnosticsText());
  model_.setStatus("Diagnostics copied to the clipboard.");
}
