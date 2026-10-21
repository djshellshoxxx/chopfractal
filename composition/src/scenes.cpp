// F09 Evolve scenes at the session level: the scene list, recording from Evolve, sequenced playback through the armed
// loop-boundary switch, state section 11 and export. The portable list and sequencer live in the `scenes` module.
//
// Playback is an override. The real pattern (patterns_, history, A/B, undo, saved state) is never touched; the scene that
// plays is `sceneOverride_` and its Playback goes to the audio side. The next scene is ARMED (ArmedSwitch mailbox) so the
// renderer flips on the exact loop-boundary frame; the message thread then publishes the same Playback to the main
// mailbox, which the renderer recognises as already current.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

#include "midi_build.hpp"

namespace fs = std::filesystem;

namespace chopfractal::composition {
namespace {
constexpr char kFullText[] = "The scene list is full (32). Delete a scene to capture more.";
constexpr char kNeedPatternText[] = "Scenes need a pattern and a source.";
constexpr char kStoppedByEditText[] = "Scene playback stopped because you changed the pattern.";
constexpr char kStopFirstText[] = "Stop scene playback first.";
constexpr char kEvolveOffText[] = "Evolve was switched off: scenes and Evolve cannot run together.";
constexpr char kMorphText[] = "Cancel the morph before playing scenes.";
constexpr char kNoneFitText[] = "No scene fits the current chops; scene playback stopped.";
constexpr std::int64_t kMaxExportSeconds = 1200;

std::string nameOf(const std::string& base, std::size_t n) { return base + " " + std::to_string(n); }

int numberOf(const scenes::SceneList& l, scenes::SceneId id) { return scenes::indexOf(l, id) + 1; }

std::string cleanCell(std::string s) {
  for (char& c : s)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s;
}
}  // namespace

// ---------------------------------------------------------------------------------------------
// audio-side plumbing
// ---------------------------------------------------------------------------------------------

void ProjectSession::attachArmMailbox(render::Mailbox<render::ArmedSwitch>* mailbox) { armMailbox_ = mailbox; }

void ProjectSession::cancelArm() {
  if (armedToken_ != 0 && armMailbox_) armMailbox_->publish(std::make_shared<const render::ArmedSwitch>(render::ArmedSwitch{nullptr, ++armCounter_}));
  armedToken_ = 0;
  armedScene_ = 0;
  armedBuilt_ = BuiltScene{};
}

Status ProjectSession::armScene(scenes::SceneId id) {
  const scenes::Scene* sc = scenes::find(scenes_, id);
  if (!sc) return makeError(ErrorCode::NotFound, "No such scene.");
  auto built = buildScene(*sc);
  if (!built.ok()) return built.error();
  if (armedToken_ != 0) cancelArm();
  armedBuilt_ = std::move(built.value());
  armedScene_ = id;
  armedToken_ = ++armCounter_;
  if (armMailbox_) armMailbox_->publish(std::make_shared<const render::ArmedSwitch>(render::ArmedSwitch{armedBuilt_.playback, armedToken_}));
  return {};
}

void ProjectSession::installScene(scenes::SceneId id, const BuiltScene& built) {
  sceneOverride_ = built.pattern;
  playingScene_ = id;
  publish(built.playback);
}

// The armed scene has begun (its boundary was observed). When the renderer applied the switch itself the same Playback is
// handed to the main mailbox (the renderer sees an unchanged current, no voice is cut). When it did not (the arm arrived
// after the boundary, or there is no audio side) the scene is installed now and the arm is cancelled.
bool ProjectSession::adoptArmed(std::uint64_t appliedToken) {
  if (armedScene_ == 0 || !armedBuilt_.playback) return false;
  const scenes::SceneId id = armedScene_;
  const BuiltScene built = armedBuilt_;
  const bool audioApplied = armMailbox_ && armedToken_ != 0 && appliedToken >= armedToken_;
  if (audioApplied) {
    armedToken_ = 0;  // consumed: nothing left to cancel
    armedScene_ = 0;
    armedBuilt_ = BuiltScene{};
  } else {
    cancelArm();  // before the main publish, so the renderer can never apply a stale arm after adopting this one
  }
  installScene(id, built);
  return true;
}

void ProjectSession::stopScenesForEdit() { endScenes(kStoppedByEditText, false); }

bool ProjectSession::endScenes(const char* noticeText, bool restore) {
  if (scenePhase_ == ScenePhase::Off) return false;
  scenePhase_ = ScenePhase::Off;
  sequencer_.stop();
  playlist_ = scenes::SceneList{};
  sceneOverride_.reset();
  playingScene_ = 0;
  cancelArm();
  if (noticeText) notice(Notice::Level::Info, noticeText);
  if (restore) republish();
  return true;
}

Status ProjectSession::stopScenes() {
  endScenes(nullptr, true);
  return {};
}

Result<ProjectSession::BuiltScene> ProjectSession::buildScene(const scenes::Scene& s) const {
  if (!chops_ || !sourceData_) return makeError(ErrorCode::InvalidArgument, kNeedPatternText);
  auto p = pattern::deserialize(s.pattern.data(), s.pattern.size());
  if (!p.ok()) return makeError(ErrorCode::Corrupt, "Scene \"" + s.name + "\" could not be read.");
  if (!pattern::flatten(p.value(), *chops_).ok()) return makeError(ErrorCode::NotFound, "Scene \"" + s.name + "\" no longer fits the chops.");
  BuiltScene out;
  out.pattern = std::make_shared<const pattern::Pattern>(pattern::sanitize(p.value(), *chops_));
  auto pb = buildPlayback(*out.pattern, true);  // groove and engine hints as for any pattern; stored effect blocks stay as captured
  if (!pb.ok()) return pb.error();
  out.playback = pb.value();
  return out;
}

bool ProjectSession::sceneFits(scenes::SceneId id) const {
  if (!chops_) return false;
  if (fitChops_ != chops_) {
    fitCache_.clear();
    fitChops_ = chops_;
  }
  auto it = fitCache_.find(id);
  if (it != fitCache_.end()) return it->second;
  const scenes::Scene* sc = scenes::find(scenes_, id);
  bool fits = false;
  if (sc) {
    auto p = pattern::deserialize(sc->pattern.data(), sc->pattern.size());
    fits = p.ok() && pattern::flatten(p.value(), *chops_).ok();
  }
  fitCache_[id] = fits;
  return fits;
}

// The sequencer plays the scenes that fit; a Jump to a scene that does not fit falls back to Advance.
void ProjectSession::rebuildPlaylist(bool warn) {
  scenes::SceneList list;
  list.wrap = scenes_.wrap;
  list.nextId = scenes_.nextId;
  std::set<scenes::SceneId> kept;
  for (const scenes::Scene& s : scenes_.scenes) {
    if (!sceneFits(s.id)) {
      if (warn) notice(Notice::Level::Warning, "Scene " + std::to_string(numberOf(scenes_, s.id)) + " no longer fits the chops and was skipped.");
      continue;
    }
    scenes::Scene meta;
    meta.id = s.id;
    meta.name = s.name;
    meta.loops = s.loops;
    meta.next = s.next;
    meta.jumpTo = s.jumpTo;
    list.scenes.push_back(std::move(meta));
    kept.insert(s.id);
  }
  for (scenes::Scene& s : list.scenes)
    if (s.next == scenes::Next::Jump && !kept.count(s.jumpTo)) {
      s.next = scenes::Next::Advance;
      s.jumpTo = 0;
    }
  playlist_ = std::move(list);
}

// Groove, effect or marker changes under a running scene: rebuild what plays (and what is armed) from the stored bytes.
Status ProjectSession::refreshScenes() {
  const scenes::SceneId playing = playingScene_;
  const scenes::SceneId armed = scenePhase_ == ScenePhase::Running ? sequencer_.armed() : armedScene_;
  rebuildPlaylist(false);
  if (playing != 0) {
    const scenes::Scene* sc = scenes::find(scenes_, playing);
    if (!sc || !sceneFits(playing)) return makeError(ErrorCode::NotFound, "The scene that was playing no longer fits the chops.");
    auto built = buildScene(*sc);
    if (!built.ok()) return built.error();
    installScene(playing, built.value());  // publish() cancels the stale arm
  }
  if (armed != 0 && scenes::find(playlist_, armed)) {
    if (!armScene(armed).ok() && scenePhase_ == ScenePhase::Running) sequencer_.stop();
  }
  return {};
}

// ---------------------------------------------------------------------------------------------
// the list
// ---------------------------------------------------------------------------------------------

Status ProjectSession::storeScenesSection() {
  StateSection sec;
  sec.tag = kTagScenes;
  sec.schema = static_cast<std::uint16_t>(scenes::kSchemaVersion);
  if (!scenes_.scenes.empty()) sec.bytes = scenes::serialize(scenes_);
  return setSection(std::move(sec));
}

Status ProjectSession::decodeScenes(const StateSection& s) {
  if (s.schema != scenes::kSchemaVersion) return makeError(ErrorCode::Corrupt, "unknown scenes section schema");
  auto r = scenes::deserialize(s.bytes.data(), s.bytes.size());
  if (!r.ok()) return r.error();
  for (const scenes::Scene& sc : r.value().scenes)
    if (!pattern::deserialize(sc.pattern.data(), sc.pattern.size()).ok()) return makeError(ErrorCode::Corrupt, "a scene pattern is not valid");
  scenes_ = std::move(r.value());
  fitCache_.clear();
  return {};
}

void ProjectSession::clearSceneState() {
  scenes_ = scenes::SceneList{};
  recordScenes_ = false;
  fitCache_.clear();
  fitChops_.reset();
}

Result<scenes::SceneId> ProjectSession::captureImpl(const std::string& nameIn, std::uint64_t evolveStep, int loops) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "There is no pattern to capture.");
  const pattern::Pattern& p = patterns_.current();
  std::vector<std::uint8_t> bytes = pattern::serialize(p);
  if (bytes.size() > scenes::kMaxPatternBytes)
    return makeError(ErrorCode::LimitExceeded, "This pattern is too large to store as a scene.", static_cast<std::int64_t>(scenes::kMaxPatternBytes));
  if (!scenes_.scenes.empty() && scenes_.scenes.back().pattern == bytes) return scenes_.scenes.back().id;  // identical to the last scene
  if (scenes_.scenes.size() >= scenes::kMaxScenes) return makeError(ErrorCode::LimitExceeded, kFullText, static_cast<std::int64_t>(scenes::kMaxScenes));
  scenes::Scene s;
  s.name = nameIn.empty() ? nameOf("Scene", scenes_.scenes.size() + 1) : nameIn.substr(0, scenes::kMaxNameLength);
  s.loops = std::min(scenes::kMaxLoops, std::max(1, loops));
  s.seed = p.settings.seed;
  s.evolveStep = evolveStep;
  s.eventCount = static_cast<std::uint32_t>(pattern::eventCount(p));
  s.bars = static_cast<std::int32_t>(p.bars.size());
  s.pattern = std::move(bytes);
  const scenes::SceneId nextBefore = scenes_.nextId;
  auto id = scenes::add(scenes_, std::move(s));
  if (!id.ok()) return id.error();
  Status st = storeScenesSection();
  if (!st.ok()) {
    scenes_.scenes.pop_back();
    scenes_.nextId = nextBefore;
    return st.error();
  }
  if (scenePhase_ == ScenePhase::Running || scenePhase_ == ScenePhase::Pending) rebuildPlaylist(false);
  return id.value();
}

Result<scenes::SceneId> ProjectSession::captureScene(const std::string& name) {
  auto r = captureImpl(name, scenes::Scene::kManual, evolve_.running() ? evolve_.settings().everyLoops : scenes::kDefaultLoops);
  if (r.ok()) notice(Notice::Level::Info, "Captured Scene " + std::to_string(numberOf(scenes_, r.value())) + ".");
  return r;
}

Status ProjectSession::updateScene(scenes::SceneId id) {
  scenes::Scene* sc = scenes::find(scenes_, id);
  if (!sc) return makeError(ErrorCode::NotFound, "No such scene.");
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "There is no pattern to capture.");
  endScenes(kStoppedByEditText, true);  // the scene being replaced may be the one that plays
  sc = scenes::find(scenes_, id);
  const pattern::Pattern& p = patterns_.current();
  std::vector<std::uint8_t> bytes = pattern::serialize(p);
  if (bytes.size() > scenes::kMaxPatternBytes)
    return makeError(ErrorCode::LimitExceeded, "This pattern is too large to store as a scene.", static_cast<std::int64_t>(scenes::kMaxPatternBytes));
  scenes::Scene backup = *sc;
  sc->pattern = std::move(bytes);
  sc->seed = p.settings.seed;
  sc->eventCount = static_cast<std::uint32_t>(pattern::eventCount(p));
  sc->bars = static_cast<std::int32_t>(p.bars.size());
  Status st = storeScenesSection();
  if (!st.ok()) {
    *sc = std::move(backup);
    return st;
  }
  fitCache_.erase(id);
  return {};
}

Status ProjectSession::editScene(scenes::SceneId id, const std::function<void(scenes::Scene&)>& edit) {
  scenes::Scene* sc = scenes::find(scenes_, id);
  if (!sc) return makeError(ErrorCode::NotFound, "No such scene.");
  scenes::Scene next = *sc;
  if (edit) edit(next);
  // Only the user-facing fields can change here; the stored pattern and its facts belong to capture / update.
  next.id = sc->id;
  next.pattern = sc->pattern;
  next.seed = sc->seed;
  next.evolveStep = sc->evolveStep;
  next.eventCount = sc->eventCount;
  next.bars = sc->bars;
  if (next.next != scenes::Next::Jump) next.jumpTo = 0;
  if (Status v = scenes::validateScene(next); !v.ok()) return v;
  if (next.next == scenes::Next::Jump && !scenes::find(scenes_, next.jumpTo)) return makeError(ErrorCode::NotFound, "The jump target does not exist.");
  std::swap(*sc, next);  // `next` now holds the previous scene
  Status st = storeScenesSection();
  if (!st.ok()) {
    std::swap(*scenes::find(scenes_, id), next);
    return st;
  }
  if (scenePhase_ == ScenePhase::Running || scenePhase_ == ScenePhase::Pending) {
    rebuildPlaylist(false);  // loops and sequencing apply to the running sequence
    if (scenePhase_ == ScenePhase::Running && sequencer_.armed() == 0 && sequencer_.running()) {
      // The decision for the following scene is taken once per scene; an edit can change it, so re-arm if needed.
      const scenes::Advance a = sequencer_.onTransportStarted(playlist_);
      if (a.arm != 0 && !armScene(a.arm).ok()) sequencer_.stop();
    }
  }
  return {};
}

Status ProjectSession::removeScene(scenes::SceneId id) {
  if (scenePhase_ == ScenePhase::Running || scenePhase_ == ScenePhase::Pending) return makeError(ErrorCode::Blocked, kStopFirstText);
  const scenes::SceneList backup = scenes_;
  Status st = scenes::remove(scenes_, id);
  if (!st.ok()) return st;
  Status stored = storeScenesSection();
  if (!stored.ok()) {
    scenes_ = backup;
    return stored;
  }
  fitCache_.erase(id);
  return {};
}

Status ProjectSession::moveScene(scenes::SceneId id, int newIndex) {
  if (scenePhase_ == ScenePhase::Running || scenePhase_ == ScenePhase::Pending) return makeError(ErrorCode::Blocked, kStopFirstText);
  Status st = scenes::move(scenes_, id, newIndex);
  if (!st.ok()) return st;
  return storeScenesSection();
}

Status ProjectSession::clearScenes() {
  if (scenePhase_ == ScenePhase::Running || scenePhase_ == ScenePhase::Pending) return makeError(ErrorCode::Blocked, kStopFirstText);
  endScenes(nullptr, true);  // a held last scene goes away with the list
  recordScenes_ = false;
  scenes_.scenes.clear();
  fitCache_.clear();
  return storeScenesSection();  // an empty list removes the section
}

Status ProjectSession::setSceneWrap(bool wrap) {
  scenes_.wrap = wrap;
  playlist_.wrap = wrap;
  return storeScenesSection();
}

Status ProjectSession::setRecordScenes(bool on) {
  if (!on) {
    recordScenes_ = false;
    return {};
  }
  if (recordScenes_) return {};
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "Generate a pattern first.");
  if (scenePhase_ != ScenePhase::Off) return makeError(ErrorCode::Blocked, kStopFirstText);
  const int loops = evolve_.running() ? evolve_.settings().everyLoops : scenes::kDefaultLoops;
  const bool differsFromLast = scenes_.scenes.empty() || scenes_.scenes.back().pattern != pattern::serialize(patterns_.current());
  if (differsFromLast) {
    auto r = captureImpl(nameOf("Scene", scenes_.scenes.size() + 1) + " (start)", scenes::Scene::kManual, loops);
    if (!r.ok()) return r.error();
  }
  recordScenes_ = true;
  if (scenes_.scenes.size() >= scenes::kMaxScenes) {
    recordScenes_ = false;
    notice(Notice::Level::Info, "Scene recording stopped: the list is full (32).");
  }
  return {};
}

void ProjectSession::recordEvolveScene(std::uint64_t stepIndex) {
  if (!recordScenes_) return;
  auto r = captureImpl(nameOf("Evolve step", static_cast<std::size_t>(stepIndex) + 1), stepIndex, evolve_.settings().everyLoops);
  if (!r.ok()) {
    recordScenes_ = false;
    notice(Notice::Level::Info, "Scene recording stopped: the list is full (32).");
    return;
  }
  if (scenes_.scenes.size() >= scenes::kMaxScenes) {
    recordScenes_ = false;
    notice(Notice::Level::Info, "Scene recording stopped: the list is full (32).");
  }
}

// ---------------------------------------------------------------------------------------------
// playback
// ---------------------------------------------------------------------------------------------

Status ProjectSession::playScenes(scenes::SceneId first, Quantize when) {
  if (!patterns_.hasPattern() || !sourceData_ || !chops_) return makeError(ErrorCode::InvalidArgument, kNeedPatternText);
  if (scenes_.scenes.empty()) return makeError(ErrorCode::InvalidArgument, "Capture at least one scene first.");
  if (morphActive_) return makeError(ErrorCode::Blocked, kMorphText);
  if (first != 0 && !scenes::find(scenes_, first)) return makeError(ErrorCode::NotFound, "No such scene.");
  if (scenePhase_ != ScenePhase::Off) endScenes(nullptr, false);  // restarting; the new scene is published below
  rebuildPlaylist(true);
  if (playlist_.scenes.empty()) return makeError(ErrorCode::NotFound, kNoneFitText);
  scenes::SceneId start = first != 0 ? first : playlist_.scenes.front().id;
  if (!scenes::find(playlist_, start)) {
    if (first != 0) {
      playlist_ = scenes::SceneList{};
      return makeError(ErrorCode::NotFound, "Scene " + std::to_string(numberOf(scenes_, first)) + " no longer fits the chops.");
    }
    start = playlist_.scenes.front().id;
  }
  if (evolve_.running()) {
    evolve_.stop();
    notice(Notice::Level::Info, kEvolveOffText);
  }
  recordScenes_ = false;
  const scenes::Scene* sc = scenes::find(scenes_, start);
  auto built = buildScene(*sc);
  if (!built.ok()) {
    playlist_ = scenes::SceneList{};
    return built.error();
  }
  const int number = numberOf(scenes_, start);
  if (when == Quantize::LoopBoundary && hostPlaying_) {
    // Begins at the next loop boundary like every later scene; the real pattern keeps playing until then.
    scenePhase_ = ScenePhase::Pending;
    armedBuilt_ = std::move(built.value());
    armedScene_ = start;
    armedToken_ = ++armCounter_;
    if (armMailbox_) armMailbox_->publish(std::make_shared<const render::ArmedSwitch>(render::ArmedSwitch{armedBuilt_.playback, armedToken_}));
  } else {
    scenePhase_ = ScenePhase::Running;
    installScene(start, built.value());
    const scenes::Advance adv = sequencer_.start(playlist_, start);
    if (adv.arm != 0 && !armScene(adv.arm).ok()) sequencer_.stop();
  }
  notice(Notice::Level::Info, "Playing scenes from Scene " + std::to_string(number) + ".");
  return {};
}

void ProjectSession::onSceneBoundary(std::uint64_t appliedToken) {
  if (scenePhase_ == ScenePhase::Pending) {
    if (!adoptArmed(appliedToken)) return;
    scenePhase_ = ScenePhase::Running;
    const scenes::Advance adv = sequencer_.start(playlist_, playingScene_);
    if (adv.arm != 0 && !armScene(adv.arm).ok()) {
      sequencer_.stop();
      scenePhase_ = ScenePhase::Held;
    }
    return;
  }
  if (scenePhase_ != ScenePhase::Running) return;
  const scenes::Advance adv = sequencer_.onBoundary(playlist_);
  if (adv.switched && !adoptArmed(appliedToken)) {
    // The sequencer moved on to a scene that was never armed (its build failed): keep the current one.
    sequencer_.stop();
    scenePhase_ = ScenePhase::Held;
    notice(Notice::Level::Warning, "A scene could not be played; the current scene keeps looping.");
    return;
  }
  if (adv.arm != 0 && !armScene(adv.arm).ok()) {
    sequencer_.stop();
    scenePhase_ = ScenePhase::Held;
    notice(Notice::Level::Warning, "A scene could not be played; the current scene keeps looping.");
    return;
  }
  if (adv.finished) {
    scenePhase_ = ScenePhase::Held;
    cancelArm();
    notice(Notice::Level::Info, "Scene sequence finished at Scene " + std::to_string(numberOf(scenes_, playingScene_)) + "; it keeps looping.");
  }
}

void ProjectSession::onTransportChanged(bool playing, std::uint64_t appliedToken) {
  if (hostPlaying_ == playing) return;
  hostPlaying_ = playing;
  if (scenePhase_ == ScenePhase::Off || scenePhase_ == ScenePhase::Held) return;
  if (!playing) {
    if (armedToken_ != 0 && armMailbox_ && appliedToken >= armedToken_) onSceneBoundary(appliedToken);  // the renderer already switched
    if (scenePhase_ == ScenePhase::Pending) {
      // Nothing will reach the boundary while the host is stopped: begin the first scene now.
      if (armedScene_ != 0 && armedBuilt_.playback) {
        const scenes::SceneId id = armedScene_;
        const BuiltScene built = armedBuilt_;
        cancelArm();
        installScene(id, built);
        scenePhase_ = ScenePhase::Running;
        const scenes::Advance adv = sequencer_.start(playlist_, id);
        if (adv.arm != 0 && !armScene(adv.arm).ok()) {
          sequencer_.stop();
          scenePhase_ = ScenePhase::Held;
        }
      }
      return;
    }
    sequencer_.onTransportStopped();
    cancelArm();
    return;
  }
  if (scenePhase_ == ScenePhase::Running) {
    const scenes::Advance adv = sequencer_.onTransportStarted(playlist_);
    if (adv.arm != 0 && !armScene(adv.arm).ok()) {
      sequencer_.stop();
      scenePhase_ = ScenePhase::Held;
    }
  }
}

SceneStatus ProjectSession::sceneStatus() const {
  SceneStatus st;
  st.recording = recordScenes_;
  st.running = scenePhase_ == ScenePhase::Pending || scenePhase_ == ScenePhase::Running;
  st.holding = scenePhase_ == ScenePhase::Held;
  st.playing = playingScene_;
  st.armed = scenePhase_ == ScenePhase::Running ? sequencer_.armed() : armedScene_;
  if (scenePhase_ == ScenePhase::Running) st.loopIndex = sequencer_.loopIndex();
  if (const scenes::Scene* sc = scenes::find(scenes_, playingScene_)) st.loops = sc->loops;
  st.waitingForTransport = st.running && !hostPlaying_;
  return st;
}

Status ProjectSession::keepScene(scenes::SceneId id, const std::string& label) {
  const scenes::Scene* sc = scenes::find(scenes_, id);
  if (!sc) return makeError(ErrorCode::NotFound, "No such scene.");
  auto p = pattern::deserialize(sc->pattern.data(), sc->pattern.size());
  if (!p.ok()) return makeError(ErrorCode::Corrupt, "Scene \"" + sc->name + "\" could not be read.");
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  pattern::Pattern fixed = pattern::sanitize(p.value(), *chops_);
  const int number = numberOf(scenes_, id);
  const std::string text = label.empty() ? nameOf("Scene", static_cast<std::size_t>(number)) : label;
  const bool wasActive = scenePhase_ != ScenePhase::Off;
  if (wasActive) endScenes(nullptr, false);  // finalize publishes the kept pattern
  Status st = finalize(std::move(fixed), text.c_str());
  if (!st.ok()) {
    if (wasActive) republish();
    return st;
  }
  notice(Notice::Level::Info, "Kept Scene " + std::to_string(number) + " as a new variation.");
  return {};
}

// ---------------------------------------------------------------------------------------------
// export
// ---------------------------------------------------------------------------------------------

Result<ExportReport> ProjectSession::exportScenes(const std::string& directory, const SceneExportOptions& o) const {
  if (scenes_.scenes.empty()) return makeError(ErrorCode::InvalidArgument, "Capture at least one scene first.");
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  if (!o.list && !o.midi && !o.wav) return makeError(ErrorCode::InvalidArgument, "Choose at least one file to export.");
  if (!(o.bpm >= 20.0 && o.bpm <= 999.0)) return makeError(ErrorCode::OutOfRange, "the tempo must be 20 to 999 BPM");
  if (o.baseNote < 0 || o.baseNote > 127) return makeError(ErrorCode::OutOfRange, "the base note must be 0 to 127");
  if (directory.empty()) return makeError(ErrorCode::InvalidArgument, "choose a folder");
  if ((o.midi || o.wav) && !chops_->chops.empty() && o.midi && o.baseNote > 128 - static_cast<int>(chops_->chops.size()))
    return makeError(ErrorCode::OutOfRange, "the chops do not fit into MIDI notes 0 to 127 from this base note", 127 - static_cast<int>(chops_->chops.size()) + 1);
  if (o.wav && !sourceData_) return makeError(ErrorCode::InvalidArgument, "the audio for this project is missing");

  struct Out {
    std::string name;
    std::vector<std::uint8_t> bytes;
  };
  std::vector<Out> files;
  ExportReport rep;

  if (o.list) {
    static const char* const kNext[] = {"next", "repeat", "stop", "jump"};
    std::string tsv = "number\tname\tloops\tnext\tjump_to\tseed\tevolve_step\tevents\tbars\n";
    for (std::size_t i = 0; i < scenes_.scenes.size(); ++i) {
      const scenes::Scene& s = scenes_.scenes[i];
      tsv += std::to_string(i + 1) + "\t" + cleanCell(s.name) + "\t" + std::to_string(s.loops) + "\t" + kNext[static_cast<int>(s.next)] + "\t";
      if (s.next == scenes::Next::Jump) tsv += std::to_string(numberOf(scenes_, s.jumpTo));
      tsv += "\t" + std::to_string(s.seed) + "\t" + (s.evolveStep == scenes::Scene::kManual ? std::string("-") : std::to_string(s.evolveStep)) + "\t" +
             std::to_string(s.eventCount) + "\t" + std::to_string(s.bars) + "\n";
    }
    files.push_back({"scenes.tsv", std::vector<std::uint8_t>(tsv.begin(), tsv.end())});
  }

  struct Rendered {
    std::shared_ptr<const render::Playback> playback;
    int loops = 1;
  };
  std::vector<Rendered> renders;
  for (std::size_t i = 0; i < scenes_.scenes.size(); ++i) {
    const scenes::Scene& s = scenes_.scenes[i];
    auto built = buildScene(s);
    if (!built.ok()) continue;  // a scene that no longer fits is left out of the audio and MIDI files
    if (o.midi) {
      MidiExportOptions mo;
      mo.baseNote = o.baseNote;
      mo.bpm = o.bpm;
      mo.loops = std::min(16, s.loops);
      mo.trackName = s.name;
      auto bytes = midiFromPattern(*built.value().pattern, mo);
      if (!bytes.ok()) return bytes.error();
      char num[32];
      std::snprintf(num, sizeof num, "%02zu", i + 1);
      files.push_back({std::string("scene_") + num + "_" + wav::sanitizeFileName(s.name) + ".mid", std::move(bytes.value())});
      if (auto dec = midi::decode(files.back().bytes.data(), files.back().bytes.size()); dec.ok()) rep.notes += dec.value().notes.size();
    }
    if (o.wav) renders.push_back({built.value().playback, s.loops});
  }

  if (o.wav) {
    if (renders.empty()) return makeError(ErrorCode::NotFound, "No scene fits the current chops.");
    const double sr = o.wavOptions.sampleRate;
    if (!(sr >= 8000.0 && sr <= 384000.0)) return makeError(ErrorCode::OutOfRange, "invalid sample rate");
    const double tpf = o.bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * sr);
    constexpr double kTail = 0.5;
    std::vector<std::int64_t> offsets;
    std::int64_t cursor = 0, total = 0;
    for (const Rendered& r : renders) {
      offsets.push_back(cursor);
      const std::int64_t mainFrames = static_cast<std::int64_t>(std::ceil(static_cast<double>(r.loops) * static_cast<double>(r.playback->lengthTicks) / tpf - 1e-6));
      total = std::max(total, cursor + mainFrames + static_cast<std::int64_t>(std::ceil(kTail * sr)));
      cursor += mainFrames;
    }
    if (total > static_cast<std::int64_t>(sr) * kMaxExportSeconds) return makeError(ErrorCode::LimitExceeded, "The scene export would be longer than 20 minutes.");
    std::vector<std::vector<float>> mix(2, std::vector<float>(static_cast<std::size_t>(total), 0.f));
    for (std::size_t i = 0; i < renders.size(); ++i) {
      render::OfflineSettings rs;
      rs.sampleRate = sr;
      rs.bpm = o.bpm;
      rs.cycles = renders[i].loops;
      rs.tailSeconds = kTail;
      rs.params = renderParams();
      rs.params.enabled = true;
      rs.params.dryMix = 0.f;
      auto audio = render::renderOffline(renders[i].playback, rs);
      if (!audio.ok()) return audio.error();
      for (std::size_t ch = 0; ch < 2; ++ch) {
        const std::vector<float>& src = audio.value()[ch];
        for (std::size_t k = 0; k < src.size(); ++k) {
          const std::size_t at = static_cast<std::size_t>(offsets[i]) + k;
          if (at < mix[ch].size()) mix[ch][at] += src[k];
        }
      }
    }
    wav::EncodeReport enc;
    auto bytes = wav::encode(mix, o.wavOptions, &enc);
    if (!bytes.ok()) return bytes.error();
    rep.frames = static_cast<std::uint64_t>(total);
    rep.peak = enc.peak;
    rep.clipped = enc.clipped;
    files.push_back({"scenes.wav", std::move(bytes.value())});
  }

  std::error_code ec;
  fs::create_directories(fs::path(directory), ec);
  if (ec) return makeError(ErrorCode::Blocked, "could not create the folder: " + ec.message());
  if (!o.overwrite)
    for (const Out& f : files)
      if (fs::exists(fs::path(directory) / f.name, ec)) return makeError(ErrorCode::Conflict, f.name + " already exists in this folder");
  for (const Out& f : files) {
    const std::string path = (fs::path(directory) / f.name).string();
    Status w = wav::writeFileAtomic(path, f.bytes, o.overwrite);
    if (!w.ok()) return w.error();
    rep.files.push_back(path);
  }
  return rep;
}

}  // namespace chopfractal::composition
