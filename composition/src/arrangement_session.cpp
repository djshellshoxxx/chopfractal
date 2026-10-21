// F18 Arrangement export at the session level: the list (project state, section tag 16), the row view, and the start of the
// export job over an immutable snapshot. The model is in arrangement.cpp, the render and the worker in arrangement_job.cpp.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace chopfractal::composition {
namespace {

constexpr char kRunningText[] = "An arrangement export is already running.";

// "Scene 3 Verse"; a scene already named "Scene 3 ..." (the default names) is not prefixed twice.
std::string sectionLabel(int number, const std::string& name) {
  const std::string prefix = "Scene " + std::to_string(number);
  if (name.compare(0, prefix.size(), prefix) == 0 && (name.size() == prefix.size() || name[prefix.size()] == ' ')) return name;
  return prefix + " " + name;
}

}  // namespace

Status ProjectSession::storeArrangementSection() {
  StateSection sec;
  sec.tag = kTagArrangement;
  sec.schema = kArrangementSchema;
  sec.bytes = serialize(arrangement_);  // empty for the default: the section is then absent
  return setSection(std::move(sec));
}

Status ProjectSession::decodeArrangement(const StateSection& s) {
  if (s.schema != kArrangementSchema) return makeError(ErrorCode::Corrupt, "unknown arrangement section schema");
  auto r = deserialize(s.bytes.data(), s.bytes.size());
  if (!r.ok()) return r.error();
  arrangement_ = std::move(r.value());
  return {};
}

Status ProjectSession::setArrangement(const Arrangement& a) {
  Status v = validateStored(a);
  if (!v.ok()) return v;
  const Arrangement before = arrangement_;
  arrangement_ = a;
  Status st = storeArrangementSection();
  if (!st.ok()) arrangement_ = before;
  return st;
}

Status ProjectSession::fillArrangementFromChain() {
  if (scenes_.scenes.empty()) return makeError(ErrorCode::InvalidArgument, "Record at least one scene first (Evolve scenes).");
  return setArrangement(fromSceneChain(scenes_, arrangement_));
}

Status ProjectSession::autoFillArrangement() {
  if (scenes_.scenes.empty()) return makeError(ErrorCode::InvalidArgument, "Record at least one scene first (Evolve scenes).");
  return setArrangement(autoFill16(scenes_, arrangement_));
}

std::vector<ArrangementRow> ProjectSession::arrangementRows() const {
  std::vector<ArrangementRow> rows;
  for (const ArrangementSection& s : arrangement_.sections) {
    ArrangementRow r;
    r.sceneId = s.sceneId;
    r.bars = s.bars;
    r.repeats = s.repeats;
    const scenes::Scene* sc = scenes::find(scenes_, s.sceneId);
    if (!sc) {
      r.deleted = true;
      r.fits = false;
    } else {
      r.sceneNumber = scenes::indexOf(scenes_, s.sceneId) + 1;
      r.sceneName = sc->name;
      r.patternBars = sc->bars;
      r.fits = sceneFits(s.sceneId);
    }
    rows.push_back(std::move(r));
  }
  return rows;
}

void ProjectSession::cancelArrangementExport() {
  if (arrangementJob_) arrangementJob_->cancel();
}

void ProjectSession::releaseArrangementJob() { arrangementJob_.reset(); }

Status ProjectSession::startArrangementExport(const Arrangement& a, const std::string& nameIn, const ArrangementExportSettings& st) {
  if (arrangementJob_ && !arrangementJob_->done()) return makeError(ErrorCode::Conflict, kRunningText);
  auto plan = planArrangementExport(a, nameIn, st);
  if (!plan.ok()) return plan.error();
  arrangementJob_ = ArrangementJob::start(std::move(plan.value()));
  return {};
}

Result<ArrangementPlan> ProjectSession::planArrangementExport(const Arrangement& a, const std::string& nameIn, const ArrangementExportSettings& st) const {
  Status v = validateStored(a);
  if (!v.ok()) return v.error();
  if (!a.valid())
    return makeError(ErrorCode::OutOfRange, "Arrangement needs 8 to 32 bars (now " + std::to_string(a.totalBars()) + ").", a.totalBars());
  if (!sourceData_ || !chops_) return makeError(ErrorCode::InvalidArgument, "the audio for this project is missing");
  if (!(st.bpm >= 20.0 && st.bpm <= 999.0)) return makeError(ErrorCode::OutOfRange, "the tempo must be 20 to 999 BPM");
  const double sr = st.wavOptions.sampleRate;
  if (!(sr >= 8000.0 && sr <= 384000.0)) return makeError(ErrorCode::OutOfRange, "invalid sample rate");
  if (st.root.empty()) return makeError(ErrorCode::InvalidArgument, "choose a folder");

  ArrangementPlan plan;
  plan.options = a;
  plan.bpm = std::round(st.bpm);
  plan.wav = st.wavOptions;
  plan.params = renderParams();
  plan.params.matchGainDb = 0.f;  // the job recomputes a gain per scene when levelMatch is on
  plan.levelMatch = st.levelMatch;
  plan.matchTrimDb = st.matchTrimDb;
  plan.root = st.root;
  plan.observer = st.observer;
  plan.name = wav::sanitizeFileName(nameIn.empty() ? std::string("chopfractal") : nameIn);
  plan.trackName = nameIn.empty() ? std::string("chopfractal") : nameIn;
  if (totalFrames(plan) > kMaxArrangementFrames) return makeError(ErrorCode::LimitExceeded, "Arrangement is too long for this sample rate.", kMaxArrangementFrames);

  MidiExportOptions mo;
  mo.baseNote = extras_.exportUi.midi.baseNote;
  mo.curve = extras_.exportUi.midi.curve;
  mo.fixedVelocity = extras_.exportUi.midi.fixedVelocity;
  mo.channel = extras_.exportUi.midi.channel;
  mo.bpm = plan.bpm;
  mo.loops = 1;

  struct Built {
    BuiltScene scene;
    std::vector<midi::Note> notes;
  };
  std::map<scenes::SceneId, Built> built;
  for (std::size_t i = 0; i < a.sections.size(); ++i) {
    const ArrangementSection& s = a.sections[i];
    const scenes::Scene* sc = scenes::find(scenes_, s.sceneId);
    if (!sc) return makeError(ErrorCode::NotFound, "Section " + std::to_string(i + 1) + " uses a scene that was deleted.", static_cast<std::int64_t>(i + 1));
    const int number = scenes::indexOf(scenes_, s.sceneId) + 1;
    auto it = built.find(s.sceneId);
    if (it == built.end()) {
      auto b = buildScene(*sc);
      if (!b.ok()) return makeError(ErrorCode::NotFound, "Scene " + std::to_string(number) + " no longer fits the current chops.", number);
      Built entry;
      entry.scene = b.value();
      if (a.writeMidi) {
        mo.trackName = sc->name;
        auto spec = midiSpecFromPattern(*entry.scene.pattern, mo);
        if (!spec.ok()) return spec.error();
        entry.notes = std::move(spec.value().notes);
      }
      it = built.emplace(s.sceneId, std::move(entry)).first;
    }
    PlannedSection ps;
    ps.label = sectionLabel(number, sc->name);
    ps.bars = s.bars;
    ps.repeats = s.repeats;
    ps.patternBars = std::max(1, sc->bars);
    ps.playback = it->second.scene.playback;
    ps.lengthTicks = ps.playback->lengthTicks;
    ps.notes = it->second.notes;
    plan.sections.push_back(std::move(ps));
  }
  return plan;
}

}  // namespace chopfractal::composition
