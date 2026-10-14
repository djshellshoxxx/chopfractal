// Evolve scenes: capture, record, sequenced playback with renderer-armed switches, keep and export
// (docs/specs/roadmap/F09-evolve-scenes.md). Message thread only.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "midi_build.hpp"

namespace chopfractal::composition {
namespace {

std::string twoDigits(std::size_t n) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02zu", n);
  return buf;
}

std::string tsvText(std::string s) {
  for (char& c : s)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s;
}

const char* nextText(scenes::Next n) {
  switch (n) {
    case scenes::Next::Advance: return "next";
    case scenes::Next::Repeat: return "repeat";
    case scenes::Next::Stop: return "stop";
    case scenes::Next::Jump: return "jump";
  }
  return "next";
}

}  // namespace

// ---- list editing ----

Result<scenes::SceneId> ProjectSession::captureScene(const std::string& name) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "Scenes need a pattern and a source.");
  const pattern::Pattern& p = patterns_.current();
  scenes::Scene sc;
  sc.pattern = pattern::serialize(p);
  if (!sceneList_.scenes.empty() && sceneList_.scenes.back().pattern == sc.pattern) return sceneList_.scenes.back().id;  // identical capture
  sc.name = name.empty() ? "Scene " + std::to_string(sceneList_.scenes.size() + 1) : name;
  sc.loops = evolve_.running() ? std::clamp(evolve_.settings().everyLoops, 1, scenes::kMaxLoops) : 4;
  sc.seed = p.settings.seed;
  sc.evolveStep = evolve_.running() && evolve_.stepsTaken() > 0 ? evolve_.stepsTaken() - 1 : scenes::Scene::kManual;
  sc.eventCount = static_cast<std::uint32_t>(pattern::eventCount(p));
  sc.bars = p.settings.bars;
  auto id = scenes::add(sceneList_, std::move(sc));
  if (!id.ok()) return id.error();
  notice(Notice::Level::Info, "Captured " + sceneList_.scenes.back().name + ".");
  return id;
}

Status ProjectSession::updateScene(scenes::SceneId id) {
  const int idx = scenes::indexOf(sceneList_, id);
  if (idx < 0) return makeError(ErrorCode::NotFound, "That scene does not exist.");
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "Scenes need a pattern and a source.");
  const pattern::Pattern& p = patterns_.current();
  scenes::Scene& sc = sceneList_.scenes[static_cast<std::size_t>(idx)];
  sc.pattern = pattern::serialize(p);
  if (sc.pattern.size() > scenes::kMaxPatternBytes) return makeError(ErrorCode::LimitExceeded, "A scene pattern is limited to 1 MiB.");
  sc.seed = p.settings.seed;
  sc.eventCount = static_cast<std::uint32_t>(pattern::eventCount(p));
  sc.bars = p.settings.bars;
  return {};
}

Status ProjectSession::editScene(scenes::SceneId id, const std::function<void(scenes::Scene&)>& edit) {
  const int idx = scenes::indexOf(sceneList_, id);
  if (idx < 0) return makeError(ErrorCode::NotFound, "That scene does not exist.");
  scenes::SceneList trial = sceneList_;
  scenes::Scene& sc = trial.scenes[static_cast<std::size_t>(idx)];
  edit(sc);
  sc.id = id;
  Status v = scenes::validate(trial);
  if (!v.ok()) return v;
  sceneList_ = std::move(trial);
  return {};
}

Status ProjectSession::removeScene(scenes::SceneId id) {
  if (sequencer_.running() || startPending_ != 0) return makeError(ErrorCode::Conflict, "Stop scene playback first.");
  return scenes::remove(sceneList_, id);
}

Status ProjectSession::moveScene(scenes::SceneId id, int newIndex) {
  if (sequencer_.running() || startPending_ != 0) return makeError(ErrorCode::Conflict, "Stop scene playback first.");
  return scenes::move(sceneList_, id, newIndex);
}

Status ProjectSession::clearScenes() {
  if (sequencer_.running() || startPending_ != 0) return makeError(ErrorCode::Conflict, "Stop scene playback first.");
  sceneList_.scenes.clear();
  recordingScenes_ = false;
  return {};
}

Status ProjectSession::setSceneWrap(bool wrap) {
  sceneList_.wrap = wrap;
  return {};
}

Status ProjectSession::setRecordScenes(bool on) {
  if (!on) {
    recordingScenes_ = false;
    return {};
  }
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "Scenes need a pattern and a source.");
  const bool differs = sceneList_.scenes.empty() || sceneList_.scenes.back().pattern != pattern::serialize(patterns_.current());
  if (differs) {
    auto id = captureScene("Scene " + std::to_string(sceneList_.scenes.size() + 1) + " (start)");
    if (!id.ok()) return id.error();
  }
  recordingScenes_ = true;
  return {};
}

// ---- playback ----

Result<std::shared_ptr<const render::Playback>> ProjectSession::buildScenePlayback(const scenes::Scene& scene, pattern::Pattern* out) const {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  auto p = pattern::deserialize(scene.pattern.data(), scene.pattern.size());
  if (!p.ok()) return p.error();
  pattern::Pattern fitted = pattern::sanitize(p.value(), *chops_);
  auto pb = buildPlayback(fitted);
  if (!pb.ok()) return pb.error();
  if (!pb.value()) return makeError(ErrorCode::InvalidArgument, "the source audio is missing");
  if (out) *out = std::move(fitted);
  return pb;
}

void ProjectSession::cancelArm() {
  if ((armedScene_ != 0 || armedHold_) && armMailbox_) armMailbox_->publish(std::make_shared<const render::ArmedSwitch>(render::ArmedSwitch{nullptr, ++armToken_}));
  armedScene_ = 0;
  armedHold_.reset();
}

bool ProjectSession::armScene(scenes::SceneId id) {
  const scenes::Scene* sc = scenes::find(sceneList_, id);
  if (!sc) return false;
  auto pb = buildScenePlayback(*sc);
  if (!pb.ok()) {
    notice(Notice::Level::Warning, "Scene " + std::to_string(scenes::indexOf(sceneList_, id) + 1) + " no longer fits the chops and was skipped.");
    return false;
  }
  armedHold_ = pb.value();
  armedScene_ = id;
  ++armToken_;
  if (armMailbox_) armMailbox_->publish(std::make_shared<const render::ArmedSwitch>(render::ArmedSwitch{pb.value(), armToken_}));
  return true;
}

void ProjectSession::installScene(scenes::SceneId id, const std::shared_ptr<const render::Playback>& pb) {
  const scenes::Scene* sc = scenes::find(sceneList_, id);
  if (!sc) return;
  auto p = pattern::deserialize(sc->pattern.data(), sc->pattern.size());
  if (p.ok() && chops_) sceneOverride_ = std::make_shared<const pattern::Pattern>(pattern::sanitize(p.value(), *chops_));
  publish(pb);
}

void ProjectSession::mirrorArmedScene(scenes::SceneId id) {
  const scenes::Scene* sc = scenes::find(sceneList_, id);
  if (!sc) return;
  auto p = pattern::deserialize(sc->pattern.data(), sc->pattern.size());
  if (p.ok() && chops_) sceneOverride_ = std::make_shared<const pattern::Pattern>(pattern::sanitize(p.value(), *chops_));
  publish(armedHold_);  // same pointer as the armed one: the renderer keeps its voices and the arm is not cancelled
  armedHold_.reset();
  armedScene_ = 0;
}

void ProjectSession::stopScenesForEdit(bool announce, bool republishNow) {
  const bool was = sequencer_.running() || startPending_ != 0 || sceneOverride_ != nullptr;
  sequencer_.stop();
  startPending_ = 0;
  cancelArm();
  sceneOverride_.reset();
  if (was && announce) notice(Notice::Level::Info, "Scene playback stopped because you changed the pattern.");
  if (was && republishNow) republish();
}

Status ProjectSession::playScenes(scenes::SceneId first, Quantize when) {
  if (!patterns_.hasPattern() || !sourceData_ || !chops_) return makeError(ErrorCode::InvalidArgument, "Scenes need a pattern and a source.");
  if (sceneList_.scenes.empty()) return makeError(ErrorCode::InvalidArgument, "Capture at least one scene first.");
  if (morphActive_) return makeError(ErrorCode::Conflict, "Cancel the morph before playing scenes.");
  if (first == 0) first = sceneList_.scenes.front().id;
  if (!scenes::find(sceneList_, first)) return makeError(ErrorCode::NotFound, "That scene does not exist.");
  // The first scene that still fits the chops.
  const int count = static_cast<int>(sceneList_.scenes.size());
  scenes::SceneId start = 0;
  std::shared_ptr<const render::Playback> startPb;
  for (int step = 0, idx = scenes::indexOf(sceneList_, first); step < count; ++step, idx = (idx + 1) % count) {
    const scenes::Scene& sc = sceneList_.scenes[static_cast<std::size_t>(idx)];
    auto pb = buildScenePlayback(sc);
    if (pb.ok()) {
      start = sc.id;
      startPb = pb.value();
      break;
    }
    notice(Notice::Level::Warning, "Scene " + std::to_string(idx + 1) + " no longer fits the chops and was skipped.");
  }
  if (!start) return makeError(ErrorCode::Conflict, "No scene fits the current chops; scene playback stopped.");
  stopScenesForEdit(false, false);
  if (evolve_.running()) {
    evolve_.stop();
    notice(Notice::Level::Info, "Evolve was switched off: scenes and Evolve cannot run together.");
  }
  recordingScenes_ = false;
  const int number = scenes::indexOf(sceneList_, start) + 1;
  if (when == Quantize::LoopBoundary && transportPlaying_ && armMailbox_) {
    startPending_ = start;
    armedHold_ = startPb;
    armedScene_ = start;
    ++armToken_;
    armMailbox_->publish(std::make_shared<const render::ArmedSwitch>(render::ArmedSwitch{startPb, armToken_}));
  } else {
    installScene(start, startPb);
    const scenes::Advance a = sequencer_.start(sceneList_, start);
    if (a.arm) armScene(a.arm);
  }
  notice(Notice::Level::Info, "Playing scenes from Scene " + std::to_string(number) + ".");
  return {};
}

Status ProjectSession::stopScenes() {
  if (!sequencer_.running() && startPending_ == 0 && !sceneOverride_) return {};
  stopScenesForEdit(false, true);
  return {};
}

void ProjectSession::onSceneBoundary(std::uint64_t appliedToken) {
  if (!sequencer_.running() && startPending_ == 0) return;
  const bool applied = armedScene_ != 0 && armMailbox_ && appliedToken >= armToken_;
  auto installNow = [&](scenes::SceneId id) {
    cancelArm();  // late arm: install through the main mailbox (up to one timer tick late) and drop the stale arm
    const scenes::Scene* sc = scenes::find(sceneList_, id);
    if (!sc) return false;
    auto pb = buildScenePlayback(*sc);
    if (!pb.ok()) return false;
    installScene(id, pb.value());
    return true;
  };
  if (startPending_ != 0) {
    const scenes::SceneId id = startPending_;
    startPending_ = 0;
    if (applied && armedScene_ == id) mirrorArmedScene(id);
    else if (!installNow(id)) {
      stopScenesForEdit(false, true);
      return;
    }
    const scenes::Advance a = sequencer_.start(sceneList_, id);
    if (a.arm) armScene(a.arm);
    return;
  }
  const scenes::Advance a = sequencer_.onBoundary(sceneList_);
  if (a.switched) {
    if (applied && armedScene_ == a.playing) mirrorArmedScene(a.playing);
    else if (!installNow(a.playing)) notice(Notice::Level::Warning, "Scene " + std::to_string(scenes::indexOf(sceneList_, a.playing) + 1) + " no longer fits the chops and was skipped.");
  }
  if (a.finished) notice(Notice::Level::Info, "Scene sequence finished at Scene " + std::to_string(scenes::indexOf(sceneList_, a.playing) + 1) + "; it keeps looping.");
  if (a.arm) armScene(a.arm);
}

void ProjectSession::onTransportChanged(bool playing) {
  transportPlaying_ = playing;
  if (!sequencer_.running() && startPending_ == 0) return;
  if (!playing) {
    if (startPending_ != 0) {  // nothing will cross a boundary: start the first scene now
      const scenes::SceneId id = startPending_;
      startPending_ = 0;
      cancelArm();
      if (const scenes::Scene* sc = scenes::find(sceneList_, id))
        if (auto pb = buildScenePlayback(*sc); pb.ok()) {
          installScene(id, pb.value());
          const scenes::Advance a = sequencer_.start(sceneList_, id);
          if (a.arm) armScene(a.arm);
        }
      return;
    }
    sequencer_.onTransportStopped();
    cancelArm();
    return;
  }
  const scenes::Advance a = sequencer_.onTransportStarted(sceneList_);
  if (a.arm) armScene(a.arm);
}

SceneStatus ProjectSession::sceneStatus() const {
  SceneStatus st;
  st.running = sequencer_.running() || startPending_ != 0;
  st.playing = sequencer_.running() ? sequencer_.current() : (sceneOverride_ ? sequencer_.current() : 0);
  st.armed = startPending_ != 0 ? startPending_ : sequencer_.armed();
  st.loopIndex = sequencer_.loopIndex();
  if (const scenes::Scene* sc = scenes::find(sceneList_, sequencer_.current())) st.loops = sc->loops;
  st.recording = recordingScenes_;
  st.waitingForTransport = st.running && !transportPlaying_;
  return st;
}

Status ProjectSession::keepScene(scenes::SceneId id, const std::string& label) {
  const int idx = scenes::indexOf(sceneList_, id);
  if (idx < 0) return makeError(ErrorCode::NotFound, "That scene does not exist.");
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  stopScenesForEdit(false, false);
  auto p = pattern::deserialize(sceneList_.scenes[static_cast<std::size_t>(idx)].pattern.data(), sceneList_.scenes[static_cast<std::size_t>(idx)].pattern.size());
  if (!p.ok()) return p.error();
  const std::string name = label.empty() ? "Scene " + std::to_string(idx + 1) : label;
  Status s = finalize(pattern::sanitize(p.value(), *chops_), name.c_str());
  if (!s.ok()) return s;
  notice(Notice::Level::Info, "Kept Scene " + std::to_string(idx + 1) + " as a new variation.");
  return {};
}

// ---- export ----

Result<ExportReport> ProjectSession::exportScenes(const std::string& directory, const SceneExportOptions& o) const {
  namespace fs = std::filesystem;
  if (sceneList_.scenes.empty()) return makeError(ErrorCode::InvalidArgument, "Capture at least one scene first.");
  if (!sourceData_ || !chops_) return makeError(ErrorCode::InvalidArgument, "Scenes need a pattern and a source.");
  if (!(o.bpm >= 20.0 && o.bpm <= 400.0)) return makeError(ErrorCode::OutOfRange, "tempo must be 20 to 400 BPM");
  if (o.baseNote < 0 || o.baseNote > 127) return makeError(ErrorCode::OutOfRange, "base note must be 0 to 127");
  struct Item {
    scenes::Scene scene;
    std::shared_ptr<const render::Playback> pb;
    pattern::Pattern pattern;
  };
  std::vector<Item> items;
  for (const scenes::Scene& sc : sceneList_.scenes) {
    Item it;
    it.scene = sc;
    auto pb = buildScenePlayback(sc, &it.pattern);
    if (!pb.ok()) return makeError(ErrorCode::Conflict, "\"" + sc.name + "\" no longer fits the chops: " + pb.error().message);
    it.pb = pb.value();
    items.push_back(std::move(it));
  }
  struct Out {
    fs::path path;
    std::vector<std::uint8_t> bytes;
  };
  std::vector<Out> outs;
  const fs::path dir = fs::u8path(directory);
  if (o.list) {
    std::string tsv = "number\tname\tloops\tnext\tjump_to\tseed\tevolve_step\tevents\tbars\n";
    for (std::size_t i = 0; i < items.size(); ++i) {
      const scenes::Scene& sc = items[i].scene;
      std::string jump;
      if (sc.next == scenes::Next::Jump) jump = std::to_string(scenes::indexOf(sceneList_, sc.jumpTo) + 1);
      tsv += std::to_string(i + 1) + "\t" + tsvText(sc.name) + "\t" + std::to_string(sc.loops) + "\t" + nextText(sc.next) + "\t" + jump + "\t" + std::to_string(sc.seed) + "\t" +
             (sc.evolveStep == scenes::Scene::kManual ? std::string("-") : std::to_string(sc.evolveStep)) + "\t" + std::to_string(sc.eventCount) + "\t" + std::to_string(sc.bars) + "\n";
    }
    outs.push_back({dir / "scenes.tsv", std::vector<std::uint8_t>(tsv.begin(), tsv.end())});
  }
  if (o.midi)
    for (std::size_t i = 0; i < items.size(); ++i) {
      auto mid = detail::buildPatternMidi(*items[i].pb, *chops_, items[i].pattern.settings.timeSignature, o.bpm, o.baseNote, items[i].scene.loops, items[i].scene.name);
      if (!mid.ok()) return mid.error();
      outs.push_back({dir / ("scene_" + twoDigits(i + 1) + "_" + wav::sanitizeFileName(items[i].scene.name) + ".mid"), std::move(mid.value())});
    }
  ExportReport rep;
  if (o.wav) {
    const double sr = o.wavOptions.sampleRate;
    const double tpf = o.bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * sr);
    std::vector<std::vector<float>> mix(2);
    double offset = 0.0;
    for (const Item& it : items) {
      render::OfflineSettings rs;
      rs.sampleRate = sr;
      rs.bpm = o.bpm;
      rs.cycles = it.scene.loops;
      rs.tailSeconds = 0.5;
      auto audio = render::renderOffline(it.pb, rs);
      if (!audio.ok()) return audio.error();
      const std::size_t at = static_cast<std::size_t>(std::ceil(offset));
      for (int c = 0; c < 2; ++c) {
        const auto& src = audio.value()[static_cast<std::size_t>(c)];
        if (mix[static_cast<std::size_t>(c)].size() < at + src.size()) mix[static_cast<std::size_t>(c)].resize(at + src.size(), 0.f);
        for (std::size_t k = 0; k < src.size(); ++k) mix[static_cast<std::size_t>(c)][at + k] += src[k];
      }
      offset += static_cast<double>(it.scene.loops) * static_cast<double>(it.pb->lengthTicks) / tpf;
    }
    wav::EncodeReport er;
    auto bytes = wav::encode(mix, o.wavOptions, &er);
    if (!bytes.ok()) return bytes.error();
    outs.push_back({dir / "scenes.wav", std::move(bytes.value())});
    rep.frames = mix[0].size();
    rep.peak = er.peak;
    rep.clipped = er.clipped;
  }
  std::error_code ec;
  if (!o.overwrite)
    for (const Out& f : outs)
      if (fs::exists(f.path, ec)) return makeError(ErrorCode::Conflict, "a file already exists at " + f.path.u8string());
  fs::create_directories(dir, ec);
  if (ec) return makeError(ErrorCode::InvalidArgument, "cannot create " + directory);
  std::vector<fs::path> written;
  for (const Out& f : outs) {
    Status w = wav::writeFileAtomic(f.path.u8string(), f.bytes, o.overwrite);
    if (!w.ok()) {
      if (!o.overwrite)
        for (const fs::path& p : written) fs::remove(p, ec);  // no partial export
      return w.error();
    }
    written.push_back(f.path);
    rep.files.push_back(f.path.u8string());
  }
  return rep;
}

}  // namespace chopfractal::composition
