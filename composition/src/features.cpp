// Fractal Rhythm, Evolve, Smart Setup and export: the composition-level glue for the feature modules.
#include <chopfractal/composition/engine_roles.hpp>
#include <chopfractal/composition/export_job.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/midi_export/midi.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace chopfractal::composition {
namespace {

}  // namespace

// ---------------------------------------------------------------------------------------------
// Fractal Rhythm
// ---------------------------------------------------------------------------------------------

Status ProjectSession::applyFractal(const fractal::Settings& fs, const std::vector<int>& bars, const std::string& label) {
  if (!patterns_.hasPattern() || !chops_) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  const int barCount = static_cast<int>(p.bars.size());
  std::vector<int> targets;
  if (bars.empty()) {
    for (int b = 0; b < barCount; ++b) {
      if (!pattern::barHasLock(p, b)) targets.push_back(b);  // a bar holding any lock (including a locked zoom) is left alone
    }
    if (targets.empty()) return makeError(ErrorCode::Blocked, "every bar is locked; unlock a bar first");
  } else {
    targets = bars;
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    for (int b : targets)
      if (b < 0 || b >= barCount) return makeError(ErrorCode::OutOfRange, "bar index out of range");
  }
  // Event budget: what remains of the cap after the bars that stay as they are, shared between the targets.
  pattern::Pattern rest = p;
  for (int b : targets) {
    auto cleared = pattern::setBarEvents(rest, b, {}, *chops_);
    if (!cleared.ok()) return cleared.error();
    rest = std::move(cleared.value());
  }
  auto kept = pattern::flatten(rest, *chops_);
  if (!kept.ok()) return kept.error();
  const std::size_t cap = static_cast<std::size_t>(p.settings.maxEvents);
  if (kept.value().size() >= cap) return makeError(ErrorCode::LimitExceeded, "the event cap leaves no room for a fractal groove", static_cast<std::int64_t>(cap));
  const std::size_t budget = (cap - kept.value().size()) / targets.size();

  auto policy = makePolicy();
  fractal::Context ctx;
  ctx.chops = chops_.get();
  ctx.barCount = barCount;
  ctx.barTicks = pattern::barTicks(p.settings);
  ctx.beatsPerBar = p.settings.timeSignature.numerator;
  ctx.policy = policy.get();
  ctx.eventBudget = budget;
  pattern::Pattern next = p;
  for (int b : targets) {
    ctx.bar = b;
    auto events = fractal::generateBar(ctx, fs);
    if (!events.ok()) return events.error();
    std::vector<Event> bar = std::move(events.value());
    if (p.engineVersion >= pattern::kEngineVersionBreak) bar = overlayAnchors(std::move(bar), *annotatedChops(), ctx.barTicks, fs.seed, b);  // the backbeat survives the motif
    auto q = pattern::setBarEvents(next, b, std::move(bar), *chops_);
    if (!q.ok()) return q.error();
    next = std::move(q.value());
  }
  return finalize(std::move(next), (label.empty() ? std::string("Fractal rhythm") : label).c_str());
}

// ---------------------------------------------------------------------------------------------
// Evolve
// ---------------------------------------------------------------------------------------------

Status ProjectSession::setEvolve(const evolve::Settings& settings) {
  if (!evolve::valid(settings)) return makeError(ErrorCode::OutOfRange, "invalid Evolve settings");
  if (settings.enabled) {
    if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
    evolve_.start(settings);
  } else {
    evolve_.start(settings);  // stores the settings, stays stopped
  }
  return {};
}

Result<bool> ProjectSession::evolveStep(double chaos) {
  if (!evolve_.running() || !patterns_.hasPattern() || hasPendingActivation()) return false;
  auto step = evolve_.onLoopMidpoint();
  if (!step) return false;
  pattern::MutateOptions mo;
  mo.seed = step->seed;
  mo.amount = step->amount;
  auto policy = makePolicy();
  pattern::GenerationReport report;
  // Chaos widens what this step may change; the stored settings are restored afterwards so they never drift.
  pattern::Pattern base = patterns_.current();
  const pattern::Settings stored = base.settings;
  if (chaos > 0.0) base.settings = applyChaos(stored, chaos);
  const ChopSnapshotPtr view = base.engineVersion >= pattern::kEngineVersionBreak ? annotatedChops() : chops_;
  auto r = pattern::mutate(base, *view, mo, policy.get(), &report);
  if (r.ok()) {
    pattern::Pattern next = std::move(r.value());
    const std::uint64_t stepSeed = next.settings.seed;  // the mutation's own seed stays on the result
    next.settings = stored;
    next.settings.seed = stepSeed;
    r = std::move(next);
  }
  // A step that changed nothing (every bar locked or protected) is a failed step: it must not fill the undo stack,
  // and after three of them Evolve stops, as specified.
  if (r.ok()) {
    pattern::Pattern a = r.value(), b = patterns_.current();
    a.settings.seed = b.settings.seed = 0;
    if (pattern::serialize(a) == pattern::serialize(b)) {
      evolve_.reportFailure();
      if (!evolve_.running()) notice(Notice::Level::Warning, "Evolve stopped: nothing could change (is everything locked?)");
      return false;
    }
  }
  Status st = finalize(std::move(r), nullptr, &report);  // undoable, but not a family-tree node
  if (!st.ok()) {
    evolve_.reportFailure();
    if (!evolve_.running()) notice(Notice::Level::Warning, "Evolve stopped after 3 failed steps: " + st.error().message);
    return st.error();
  }
  evolve_.reportSuccess();
  return true;
}

Status ProjectSession::keepEvolved(const std::string& label) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "there is no pattern to keep");
  recordHistory(label.empty() ? "Evolve keeper" : label);
  return {};
}

// ---------------------------------------------------------------------------------------------
// Smart Setup
// ---------------------------------------------------------------------------------------------

Result<SetupSuggestion> ProjectSession::suggestSetup(double hostBpm) const {
  if (!sourceData_ || !chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  const float* ch[2] = {sourceData_->channel(0), sourceData_->channels > 1 ? sourceData_->channel(1) : nullptr};
  insight::SourceView view{ch, sourceData_->channels, sourceData_->frames, sourceData_->sampleRate};
  SetupSuggestion out;
  out.roles = insight::suggestRoles(view, *chops_);
  out.loops = insight::suggestLoop(static_cast<double>(sourceData_->frames) / sourceData_->sampleRate, hostBpm);
  return out;
}

Result<int> ProjectSession::acceptRoleSuggestions(const std::vector<insight::RoleSuggestion>& suggestions, double minConfidence) {
  if (!chopMap_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  int applied = 0;
  for (const insight::RoleSuggestion& s : suggestions) {
    if (s.role == insight::Role::Other || s.confidence < minConfidence) continue;
    if (!roles_.roleOf(s.chop).empty()) continue;  // never overwrite what the user chose
    Status st = assignRole(s.chop, insight::roleName(s.role));
    if (st.ok()) ++applied;
  }
  return applied;
}

Result<SetupSuggestion> ProjectSession::smartSetup(double hostBpm) {
  if (!sourceData_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  {  // detect every time, merging with whatever the user already placed (their markers are never replaced)
    DetectOptions d;
    d.merge = source::MergeMode::Merge;  // keeps any marker the user already placed
    Status st = applyChops(d);
    if (!st.ok()) return st.error();
  }
  return suggestSetup(hostBpm);
}

// ---------------------------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------------------------

Result<ExportReport> ProjectSession::exportWav(const std::string& path, const ExportWavOptions& o) const {
  auto job = ExportJob::planWav(*this, path, o);
  if (!job.ok()) return job.error();
  ExportProgress progress;  // synchronous callers do not watch or cancel
  return job.value().run(progress);
}

Result<std::vector<std::uint8_t>> ProjectSession::renderMidi(const KitOptions& o) const {
  if (!sourceData_ || !chops_ || chops_->chops.empty()) return makeError(ErrorCode::InvalidArgument, "load a source with chops first");
  if (!patterns_.hasPattern() || !playback_ || playback_->passThrough) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  const std::size_t n = chops_->chops.size();
  if (n > 256) return makeError(ErrorCode::LimitExceeded, "too many chops for a kit", 256);
  if (o.baseNote < 0 || o.baseNote > 128 - static_cast<int>(n))
    return makeError(ErrorCode::OutOfRange, "the chops do not fit into MIDI notes 0 to 127 from this base note", 127 - static_cast<int>(n) + 1);
  midi::FileSpec spec;
  spec.bpm = o.bpm;
  spec.numerator = patterns_.current().settings.timeSignature.numerator;
  spec.denominator = patterns_.current().settings.timeSignature.denominator;
  spec.trackName = o.name.substr(0, 60);
  auto midiEv = midiEvents();  // honours the groove's bake flag
  if (!midiEv.ok()) return midiEv.error();
  for (int loop = 0; loop < o.loops; ++loop)
    for (const FlatEvent& e : midiEv.value()) {
      std::size_t idx = n;
      for (std::size_t i = 0; i < n; ++i)
        if (chops_->chops[i].id == e.chop) idx = i;
      if (idx == n) continue;
      midi::Note note;
      note.note = o.baseNote + static_cast<int>(idx);
      note.velocity = std::min(127, std::max(1, static_cast<int>(std::lround(static_cast<double>(e.tx.level) * 127.0))));
      note.start = static_cast<Ticks>(loop) * playback_->lengthTicks + e.start;
      note.duration = e.duration;
      spec.notes.push_back(note);
    }
  return midi::encode(spec);

}

Result<std::vector<std::uint8_t>> ProjectSession::renderWavBytes(const ExportWavOptions& o) const {
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  if (!(o.tailSeconds >= 0.0 && o.tailSeconds <= 30.0)) return makeError(ErrorCode::OutOfRange, "tail must be 0 to 30 seconds");
  if (!playback_ || playback_->passThrough || !playback_->source) return makeError(ErrorCode::InvalidArgument, "there is no pattern to render");
  render::OfflineSettings rs;
  rs.sampleRate = o.wav.sampleRate;
  rs.bpm = o.bpm;
  rs.cycles = o.loops;
  rs.tailSeconds = o.tailSeconds;
  auto audio = render::renderOffline(playback_, rs);
  if (!audio.ok()) return audio.error();
  return wav::encode(audio.value(), o.wav);
}

Result<ExportReport> ProjectSession::exportKit(const std::string& directory, const KitOptions& o) const {
  auto job = ExportJob::planKit(*this, directory, o);
  if (!job.ok()) return job.error();
  ExportProgress progress;
  return job.value().run(progress);
}

}  // namespace chopfractal::composition
