#include "PerformCommands.h"

#include <algorithm>
#include <chopfractal/chop_insight/insight.hpp>

namespace cf = chopfractal;

PerformCommands::PerformCommands(ChopFractalProcessor& proc, EditorModel& model, EditorCommands& commands) : proc_(proc), model_(model), cmd_(commands), stretch_(proc) {
  exportFolder = [] { return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("ChopFractal"); };
  confirm = [alive = alive_](const juce::String& title, const juce::String& message, const juce::String& ok, std::function<void()> onOk) {
    auto options = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle(title).withMessage(message).withButton(ok).withButton("Cancel");
    juce::AlertWindow::showAsync(options, [onOk, alive](int result) {
      if (result == 1 && onOk && *alive) onOk();  // with the non-native alert window the first of two buttons returns 1
    });
  };
}

PerformCommands::~PerformCommands() { *alive_ = false; }

void PerformCommands::drainNotices() {
  juce::String notice;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& n : s.takeNotices()) notice = juce::String(n.text);
  });
  if (notice.isNotEmpty()) {
    model_.setStatus(notice);
    model_.rebuild();
  }
}

// ---------------------------------------------------------------------------------------------
// Input capture
// ---------------------------------------------------------------------------------------------

cf::ui::CaptureView PerformCommands::captureView(const CaptureChoice& c) { return cf::ui::buildCaptureView(proc_.captureUiInput(c.mode, c.bars, c.maxSeconds)); }

void PerformCommands::arm(const CaptureChoice& c, bool needsConfirm) {
  auto go = [this, c] {
    const cf::Status st = proc_.armCapture(c.mode, c.bars, c.maxSeconds);
    if (!st.ok()) model_.setStatus(juce::String::fromUTF8(st.error().message.c_str()));
  };
  if (needsConfirm) confirm("Discard the unsaved capture?", "The last capture has not been kept and will be lost.", "Discard", go);
  else go();
}

void PerformCommands::startCapture() {
  const cf::Status st = proc_.startCapture();
  if (!st.ok()) model_.setStatus(juce::String::fromUTF8(st.error().message.c_str()));
}

void PerformCommands::keepCapture(const cf::ui::CaptureView& view, bool chopAfter) {
  auto go = [this, chopAfter] { proc_.keepCaptureAsync(chopAfter); };
  if (view.keepNeedsConfirm) confirm(juce::String::fromUTF8(view.keepConfirmTitle.c_str()), juce::String::fromUTF8(view.keepConfirmMessage.c_str()), "Keep", go);
  else go();
}

void PerformCommands::undoKeep() {
  if (proc_.undoKeep()) model_.rebuild();
}

// ---------------------------------------------------------------------------------------------
// Tempo match
// ---------------------------------------------------------------------------------------------

PerformCommands::SourceBrief PerformCommands::sourceBrief() {
  SourceBrief b;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (const auto* map = s.chopMap()) {
      b.present = true;
      b.missing = s.sourceMissing();
      b.frames = map->info().frames;
      b.sampleRate = map->info().sampleRate;
      b.channels = map->info().channels;
    }
  });
  return b;
}

std::vector<cf::composition::LoopLengthChoice> PerformCommands::loopChoices() {
  const double seconds = sourceSeconds();
  if (seconds <= 0.0) return {};
  if (model_.haveSetup() && !model_.setup().loops.empty()) return cf::composition::loopLengthChoices(model_.setup().loops, seconds);
  return cf::composition::loopLengthChoices(cf::insight::suggestLoop(seconds, usedHostTempo() ? model_.activeBpm() : 0.0), seconds);
}

std::string PerformCommands::loopChoiceKey() {
  const auto choices = loopChoices();
  std::string key = std::to_string(static_cast<long long>(sourceSeconds() * 1000.0));
  for (const auto& c : choices) key += "|" + c.text;
  return key;
}

bool PerformCommands::sourceLooksStretched() {
  bool stretched = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (const auto* map = s.chopMap()) stretched = cf::composition::looksStretched(map->info().name);
  });
  return stretched;
}

bool PerformCommands::hasPattern() {
  bool has = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) { has = s.pattern() != nullptr; });
  return has;
}

void PerformCommands::startStretch(const cf::composition::StretchRequest& request) {
  const cf::Status st = stretch_.start(request);
  if (!st.ok()) model_.setStatus(juce::String::fromUTF8(st.error().message.c_str()));
}

void PerformCommands::cancelStretch() { stretch_.cancel(); }

void PerformCommands::undoStretch() {
  const cf::Status st = stretch_.undo();
  model_.setStatus(st.ok() ? juce::String("Stretch undone.") : juce::String::fromUTF8(st.error().message.c_str()));
  model_.rebuild();
}

void PerformCommands::background() {
  juce::String message;
  pollArrangementJob();
  if (stretch_.running()) {
    const auto outcome = stretch_.poll(message);
    if (outcome == StretchRunner::Outcome::None) model_.setStatus("Stretching... " + juce::String(static_cast<int>(stretch_.progress() * 100.f)) + "%");
    else {
      model_.setStatus(message);
      model_.rebuild();
    }
  }
  drainNotices();
}

// ---------------------------------------------------------------------------------------------
// Performance pads
// ---------------------------------------------------------------------------------------------

cf::ui::PadView PerformCommands::padView(int page, const std::array<std::uint32_t, 4>& sounding) {
  cf::ui::PadUiInput in;
  const auto settings = proc_.padSettings();
  in.baseNote = settings.baseNote;
  in.page = page;
  in.midiChannel = settings.midiChannel;
  in.quantizeDivision = settings.quantizeDivision;
  in.quantizeTriplet = settings.quantizeTriplet;
  in.merge = settings.merge;
  in.recordArmed = proc_.recordArmed();
  in.takeHits = proc_.takeHits();
  in.hostPlaying = proc_.hostPlaying.load();
  in.hostHasPosition = proc_.hostHasPosition.load();
  for (std::size_t i = 0; i < 4; ++i) in.sounding[i] = sounding[i];
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    in.hasSource = s.hasSource();
    in.hasPattern = s.pattern() != nullptr;
    if (const auto chops = s.chops()) {
      in.chopCount = static_cast<int>(chops->chops.size());
      for (const auto& chop : chops->chops) {
        const std::string role = s.roleMap().roleOf(chop.id);
        in.roles.push_back(role);
        const auto it = settings.chokeOverride.find(chop.id.value);
        in.chokeGroups.push_back(it != settings.chokeOverride.end() ? it->second : static_cast<std::uint8_t>(role == "hat" ? 1 : 0));
      }
    }
  });
  return cf::ui::buildPadView(in);
}

void PerformCommands::editPadSettings(const std::function<void(cf::composition::PadSettings&)>& edit) {
  auto settings = proc_.padSettings();
  edit(settings);
  const cf::Status st = proc_.setPadSettings(settings);
  if (!st.ok()) model_.setStatus(juce::String::fromUTF8(st.error().message.c_str()));
}

void PerformCommands::setChokeGroup(int chopIndex, int group) {
  cf::ChopId id;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (const auto chops = s.chops(); chops && chopIndex >= 0 && chopIndex < static_cast<int>(chops->chops.size())) id = chops->chops[static_cast<std::size_t>(chopIndex)].id;
  });
  if (!id.valid()) return;
  const cf::Status st = proc_.setChokeGroup(id, static_cast<std::uint8_t>(group));
  if (!st.ok()) model_.setStatus(juce::String::fromUTF8(st.error().message.c_str()));
}

// ---------------------------------------------------------------------------------------------
// Scenes and A/B morph
// ---------------------------------------------------------------------------------------------

PerformCommands::SceneState PerformCommands::sceneState() {
  SceneState st;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    const auto& list = s.sceneList();
    st.wrap = list.wrap;
    for (const auto& sc : list.scenes) {
      SceneInfo i;
      i.id = sc.id;
      i.name = sc.name;
      i.loops = sc.loops;
      i.next = sc.next;
      i.jumpTo = sc.jumpTo;
      i.seed = sc.seed;
      i.evolveStep = sc.evolveStep;
      i.eventCount = sc.eventCount;
      i.bars = sc.bars;
      i.fits = s.sceneFits(sc.id);
      st.scenes.push_back(std::move(i));
    }
    st.status = s.sceneStatus();
    st.recording = s.recordingScenes();
    st.evolveRunning = s.evolveRunning();
    st.hasPattern = s.pattern() != nullptr;
    st.hasSource = s.hasSource();
    st.hasA = s.hasSnapshot(0);
    st.hasB = s.hasSnapshot(1);
    st.morphActive = s.morphActive();
    st.morphAmount = s.morphAmount();
  });
  return st;
}

cf::ui::SceneView PerformCommands::sceneView(const SceneState& st, int selectedIndex) {
  cf::ui::SceneViewInput in;
  for (std::size_t i = 0; i < st.scenes.size(); ++i) {
    const auto& sc = st.scenes[i];
    cf::ui::SceneRowInput r;
    r.number = static_cast<int>(i) + 1;
    r.name = sc.name;
    r.loops = sc.loops;
    r.next = static_cast<int>(sc.next);
    if (const int j = st.indexOf(sc.jumpTo); j >= 0) r.jumpNumber = j + 1;
    r.playing = st.status.playing == sc.id;
    r.loopIndex = r.playing ? st.status.loopIndex : 0;
    r.armed = st.status.armed == sc.id;
    r.fits = sc.fits;
    in.rows.push_back(std::move(r));
  }
  in.status.running = st.status.running;
  in.status.holding = st.status.holding;
  in.status.recording = st.status.recording || st.recording;
  in.status.waitingForTransport = st.status.waitingForTransport;
  in.status.playingNumber = st.indexOf(st.status.playing) + 1;
  in.status.armedNumber = st.indexOf(st.status.armed) + 1;
  in.status.loopIndex = st.status.loopIndex;
  in.status.loops = st.status.loops;
  in.selected = selectedIndex;
  in.hasPattern = st.hasPattern;
  in.hasSource = st.hasSource;
  in.evolveRunning = st.evolveRunning;
  in.wrap = st.wrap;
  in.capturedCount = static_cast<int>(st.scenes.size());
  return cf::ui::buildSceneView(in);
}

cf::ui::MorphView PerformCommands::morphView(const SceneState& st) {
  cf::ui::MorphInput in;
  in.hasA = st.hasA;
  in.hasB = st.hasB;
  in.active = st.morphActive;
  in.amount = st.morphAmount;
  in.scenesPlaying = st.status.running || st.status.holding;
  return cf::ui::buildMorphView(in);
}

void PerformCommands::captureScene() {
  int number = 0;
  const cf::Status st = cmd_.run([&number](cf::composition::ProjectSession& s) {
    auto r = s.captureScene();
    if (r.ok()) number = static_cast<int>(s.sceneList().scenes.size());
    return r.ok() ? cf::Status{} : cf::Status{r.error()};
  });
  if (st.ok()) model_.setStatus(juce::String::fromUTF8(cf::ui::sceneCapturedMessage(number).c_str()));
}
void PerformCommands::updateScene(cf::scenes::SceneId id) { cmd_.run([id](cf::composition::ProjectSession& s) { return s.updateScene(id); }); }
void PerformCommands::keepScene(cf::scenes::SceneId id) { cmd_.run([id](cf::composition::ProjectSession& s) { return s.keepScene(id); }); }
void PerformCommands::moveScene(cf::scenes::SceneId id, int delta) {
  cmd_.run([id, delta](cf::composition::ProjectSession& s) {
    const int at = cf::scenes::indexOf(s.sceneList(), id);
    return at < 0 ? cf::Status{cf::makeError(cf::ErrorCode::NotFound, "That scene no longer exists.")} : s.moveScene(id, at + delta);
  });
}
void PerformCommands::removeScene(cf::scenes::SceneId id) { cmd_.run([id](cf::composition::ProjectSession& s) { return s.removeScene(id); }); }
void PerformCommands::clearScenes() {
  confirm(juce::String::fromUTF8(cf::ui::sceneClearPromptTitle().c_str()), juce::String::fromUTF8(cf::ui::sceneClearPromptBody().c_str()), "Delete All",
          [this] { cmd_.run([](cf::composition::ProjectSession& s) { return s.clearScenes(); }); });
}
void PerformCommands::editScene(cf::scenes::SceneId id, const std::function<void(cf::scenes::Scene&)>& edit) {
  cmd_.run([id, edit](cf::composition::ProjectSession& s) { return s.editScene(id, edit); });
}
void PerformCommands::setSceneWrap(bool on) { cmd_.run([on](cf::composition::ProjectSession& s) { return s.setSceneWrap(on); }); }
void PerformCommands::setRecordScenes(bool on) { cmd_.run([on](cf::composition::ProjectSession& s) { return s.setRecordScenes(on); }); }
bool PerformCommands::playScenes(cf::scenes::SceneId first) {
  return cmd_.run([first](cf::composition::ProjectSession& s) { return s.playScenes(first, cf::composition::Quantize::LoopBoundary); }).ok();
}
void PerformCommands::stopScenes() { cmd_.run([](cf::composition::ProjectSession& s) { return s.stopScenes(); }); }

cf::Status PerformCommands::exportScenesTo(const std::string& directory, bool wav, bool overwrite) {
  cf::composition::SceneExportOptions o;
  o.wav = wav;
  o.overwrite = overwrite;
  o.bpm = std::max(20.0, std::min(999.0, proc_.activeBpm.load()));
  int files = 0;
  const cf::Status st = cmd_.run([&](cf::composition::ProjectSession& s) {
    auto r = s.exportScenes(directory, o);
    if (!r.ok()) return cf::Status{r.error()};
    files = static_cast<int>(r.value().files.size());
    return cf::Status{};
  });
  if (st.ok()) model_.setStatus(juce::String::fromUTF8(cf::ui::sceneExportedMessage(files, directory).c_str()));
  return st;
}

void PerformCommands::exportScenes(bool wav) {
  chooser_ = std::make_unique<juce::FileChooser>("Choose a folder for the scene files", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, wav, alive = alive_](const juce::FileChooser& fc) {
    if (!*alive) return;
    const juce::File dir = fc.getResult();
    if (dir == juce::File()) return;
    const std::string path = dir.getFullPathName().toStdString();
    const cf::Status st = exportScenesTo(path, wav, false);
    if (!st.ok() && st.error().code == cf::ErrorCode::Conflict)
      confirm("Replace the scene files?", juce::String::fromUTF8(st.error().message.c_str()) + ". Replace them?", "Replace", [this, path, wav] { exportScenesTo(path, wav, true); });
  });
}

bool PerformCommands::beginMorph(double t) {
  const cf::Status st = cmd_.run([t](cf::composition::ProjectSession& s) { return s.beginMorph(t); });
  return st.ok();
}
bool PerformCommands::setMorph(double t) {
  return cmd_.run([t](cf::composition::ProjectSession& s) { return s.setMorph(t); }).ok();
}
void PerformCommands::cancelMorph() {
  cmd_.run([](cf::composition::ProjectSession& s) {
    s.cancelMorph();
    return cf::Status{};
  });
  model_.setStatus(juce::String::fromUTF8(cf::ui::morphCancelledMessage().c_str()));
}
void PerformCommands::commitMorph() {
  if (cmd_.run([](cf::composition::ProjectSession& s) { return s.commitMorph(); }).ok()) model_.setStatus(juce::String::fromUTF8(cf::ui::morphCommittedMessage().c_str()));
}

// ---------------------------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------------------------

PerformCommands::OutputsState PerformCommands::outputsState() {
  OutputsState out;
  cf::ui::OutputsUiInput in;
  for (int i = 0; i < cf::ui::kOutputAuxBuses; ++i)
    if (proc_.auxBusEnabled(i + 1)) in.auxEnabledMask |= 1u << i;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    in.hasSource = s.hasSource();
    in.mode = static_cast<int>(s.outputRouting().mode);
    const auto& routing = s.outputRouting();
    if (const auto chops = s.chops()) {
      int number = 1;
      for (const auto& chop : chops->chops) {
        cf::ui::OutputsChopInput c;
        c.number = number++;
        c.role = s.roleMap().roleOf(chop.id);
        const auto it = routing.chopBus.find(chop.id.value);
        c.explicitBus = it == routing.chopBus.end() ? -1 : it->second;
        in.chops.push_back(c);
        out.chopIds.push_back(chop.id.value);
        if (!c.role.empty() && std::none_of(in.roles.begin(), in.roles.end(), [&](const cf::ui::OutputsRoleInput& r) { return r.name == c.role; })) {
          cf::ui::OutputsRoleInput r;
          r.name = c.role;
          const auto rb = routing.roleBus.find(c.role);
          r.bus = rb == routing.roleBus.end() ? 0 : rb->second;
          in.roles.push_back(r);
        }
      }
    }
  });
  out.view = cf::ui::buildOutputsView(in);
  return out;
}

void PerformCommands::routeMain() { cmd_.run([](cf::composition::ProjectSession& s) { return s.resetOutputRouting(); }); }
void PerformCommands::routeByRole() {
  cmd_.run([](cf::composition::ProjectSession& s) {
    s.autoRouteByRole();
    return cf::Status{};
  });
}
void PerformCommands::routeByChop() {
  cmd_.run([](cf::composition::ProjectSession& s) {
    s.autoRouteByChop();
    return cf::Status{};
  });
}
void PerformCommands::setRoleBus(const std::string& role, int bus) {
  cmd_.run([role, bus](cf::composition::ProjectSession& s) {
    auto r = s.outputRouting();
    r.mode = cf::composition::RoutingMode::Custom;
    r.roleBus[role] = static_cast<std::uint8_t>(bus);
    return s.setOutputRouting(r);
  });
}
void PerformCommands::setChopBus(std::uint64_t chopId, int bus) {
  cmd_.run([chopId, bus](cf::composition::ProjectSession& s) {
    auto r = s.outputRouting();
    r.mode = cf::composition::RoutingMode::Custom;
    if (bus < 0) r.chopBus.erase(chopId);
    else r.chopBus[chopId] = static_cast<std::uint8_t>(bus);
    return s.setOutputRouting(r);
  });
}

// ---------------------------------------------------------------------------------------------
// Arrangement
// ---------------------------------------------------------------------------------------------

PerformCommands::ArrangementState PerformCommands::arrangementState() {
  ArrangementState st;
  st.bpm = std::round(proc_.activeBpm.load());
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    st.arrangement = s.arrangement();
    st.rows = s.arrangementRows();
    st.sceneCount = static_cast<int>(s.sceneList().scenes.size());
    for (const auto& sc : s.sceneList().scenes) st.scenes.emplace_back(sc.id, sc.bars);
    st.hasSource = s.hasSource();
    if (auto* job = s.arrangementJob(); job && !job->done()) {
      st.running = true;
      st.progress = job->progress();
    }
  });
  return st;
}

cf::ui::ArrangementView PerformCommands::arrangementView(const ArrangementState& st, int selectedRow) {
  cf::ui::ArrangementViewInput in;
  for (const auto& r : st.rows) {
    cf::ui::ArrangementRowInput row;
    row.sceneNumber = r.sceneNumber;
    row.sceneName = r.sceneName;
    row.patternBars = r.patternBars;
    row.bars = r.bars;
    row.repeats = r.repeats;
    row.deleted = r.deleted;
    row.fits = r.fits;
    in.rows.push_back(std::move(row));
  }
  in.selected = selectedRow;
  in.totalBars = st.arrangement.totalBars();
  in.bpm = st.bpm;
  in.sceneCount = st.sceneCount;
  in.hasSource = st.hasSource;
  in.running = st.running;
  in.jobSection = st.progress.section;
  in.jobSections = st.progress.sections;
  in.jobFraction = st.progress.fraction;
  in.fadeOut = st.arrangement.fadeOut;
  return cf::ui::buildArrangementView(in);
}

void PerformCommands::editArrangement(const std::function<void(cf::composition::Arrangement&)>& edit) {
  cmd_.run([edit](cf::composition::ProjectSession& s) {
    auto a = s.arrangement();
    edit(a);
    return s.setArrangement(a);
  });
}
void PerformCommands::fillArrangementFromChain() { cmd_.run([](cf::composition::ProjectSession& s) { return s.fillArrangementFromChain(); }); }
void PerformCommands::autoFillArrangement() { cmd_.run([](cf::composition::ProjectSession& s) { return s.autoFillArrangement(); }); }

std::string PerformCommands::projectName() {
  std::string name;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (const auto* map = s.chopMap()) name = map->info().name;
  });
  return name;
}

void PerformCommands::startArrangementExport(const std::string& name) {
  cf::composition::ArrangementExportSettings settings;
  const juce::File root = exportFolder();
  settings.root = root.getFullPathName().toStdString();
  settings.bpm = std::round(proc_.activeBpm.load());
  settings.wavOptions.sampleRate = 48000;
  settings.wavOptions.format = cf::wav::Format::Pcm24;
  settings.levelMatch = proc_.currentParams().get(cf::host::kLevelMatch) >= 0.5;
  settings.matchTrimDb = proc_.currentParams().get(cf::host::kMatchTrimDb);
  cmd_.run([&](cf::composition::ProjectSession& s) { return s.startArrangementExport(s.arrangement(), name, settings); });
}

void PerformCommands::cancelArrangementExport() {
  proc_.withSession([](cf::composition::ProjectSession& s) { s.cancelArrangementExport(); });
}

bool PerformCommands::pollArrangementJob() {
  bool ended = false;
  juce::String message;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    auto* job = s.arrangementJob();
    if (!job || !job->done()) return;
    const auto& result = job->result();
    if (result.status.ok())
      message = juce::String::fromUTF8(cf::ui::arrangementExportedMessage(juce::File(juce::String::fromUTF8(result.wavPath.c_str())).getFileName().toStdString(), result.bars, result.seconds).c_str());
    else if (result.status.error().code == cf::ErrorCode::Cancelled)
      message = juce::String::fromUTF8(cf::ui::arrangementCancelledMessage().c_str());
    else
      message = juce::String::fromUTF8(result.status.error().message.c_str());
    s.releaseArrangementJob();
    ended = true;
  });
  if (ended) model_.setStatus(message);
  return ended;
}
