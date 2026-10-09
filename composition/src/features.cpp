// Fractal Rhythm, Evolve, Smart Setup and export: the composition-level glue for the feature modules.
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/midi_export/midi.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace chopfractal::composition {
namespace {

std::string twoDigits(std::size_t n) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02zu", n);
  return buf;
}

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
      const pattern::Bar& bar = p.bars[static_cast<std::size_t>(b)];
      bool anyLock = p.phraseLocked || bar.locked;
      for (const pattern::Beat& bt : bar.beats) {
        anyLock = anyLock || bt.locked;
        for (const Event& e : bt.events) anyLock = anyLock || e.locked;
      }
      if (!anyLock) targets.push_back(b);  // a bar holding any lock is left alone
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
    auto q = pattern::setBarEvents(next, b, std::move(events.value()), *chops_);
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

Result<bool> ProjectSession::evolveStep() {
  if (!evolve_.running() || !patterns_.hasPattern() || hasPendingActivation()) return false;
  auto step = evolve_.onLoopMidpoint();
  if (!step) return false;
  pattern::MutateOptions mo;
  mo.seed = step->seed;
  mo.amount = step->amount;
  auto policy = makePolicy();
  pattern::GenerationReport report;
  auto r = pattern::mutate(patterns_.current(), *chops_, mo, policy.get(), &report);
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
  if (chopMap_->markers().empty()) {  // nothing placed yet: detect (a user's markers are never replaced)
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
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  if (!(o.tailSeconds >= 0.0 && o.tailSeconds <= 30.0)) return makeError(ErrorCode::OutOfRange, "tail must be 0 to 30 seconds");
  if (!playback_ || playback_->passThrough || !playback_->source) return makeError(ErrorCode::InvalidArgument, "there is no pattern to render");
  if (!o.overwrite && std::filesystem::exists(path)) return makeError(ErrorCode::Conflict, "a file already exists at " + path);
  render::OfflineSettings rs;
  rs.sampleRate = o.wav.sampleRate;
  rs.bpm = o.bpm;
  rs.cycles = o.loops;
  rs.tailSeconds = o.tailSeconds;
  auto audio = render::renderOffline(playback_, rs);
  if (!audio.ok()) return audio.error();
  wav::EncodeReport rep;
  auto bytes = wav::encode(audio.value(), o.wav, &rep);
  if (!bytes.ok()) return bytes.error();
  Status w = wav::writeFileAtomic(path, bytes.value(), o.overwrite);
  if (!w.ok()) return w.error();
  ExportReport out;
  out.frames = audio.value()[0].size();
  out.peak = rep.peak;
  out.clipped = rep.clipped;
  out.files.push_back(path);
  return out;
}

Result<ExportReport> ProjectSession::exportKit(const std::string& directory, const KitOptions& o) const {
  namespace fs = std::filesystem;
  if (!sourceData_ || !chops_ || chops_->chops.empty()) return makeError(ErrorCode::InvalidArgument, "load a source with chops first");
  if (!patterns_.hasPattern() || !playback_ || playback_->passThrough) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  const std::size_t n = chops_->chops.size();
  if (n > 256) return makeError(ErrorCode::LimitExceeded, "too many chops for a kit", 256);
  if (o.baseNote < 0 || o.baseNote + static_cast<int>(n) - 1 > 127)
    return makeError(ErrorCode::OutOfRange, "the chops do not fit into MIDI notes 0 to 127 from this base note", 127 - static_cast<int>(n) + 1);
  const std::string stem = wav::sanitizeFileName(o.name);

  // Plan every file name first, then refuse (before writing anything) if any already exists.
  std::vector<std::string> sliceNames;
  for (std::size_t i = 0; i < n; ++i) {
    std::string role = roles_.roleOf(chops_->chops[i].id);
    sliceNames.push_back("slice_" + twoDigits(i + 1) + "_" + (role.empty() ? std::string("chop") : wav::sanitizeFileName(role)) + ".wav");
  }
  const fs::path dir(directory);
  std::vector<fs::path> targets;
  for (const std::string& s : sliceNames) targets.push_back(dir / s);
  targets.push_back(dir / (stem + ".mid"));
  targets.push_back(dir / "kit.txt");
  std::error_code ec;
  if (!o.overwrite)
    for (const fs::path& t : targets)
      if (fs::exists(t, ec)) return makeError(ErrorCode::Conflict, "a file already exists at " + t.string());

  // MIDI first (it can fail on limits) so that nothing is written for a request that cannot complete.
  midi::FileSpec spec;
  spec.bpm = o.bpm;
  spec.numerator = patterns_.current().settings.timeSignature.numerator;
  spec.denominator = patterns_.current().settings.timeSignature.denominator;
  spec.trackName = o.name.substr(0, 60);
  for (int loop = 0; loop < o.loops; ++loop)
    for (const FlatEvent& e : playback_->events) {
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
  auto mid = midi::encode(spec);
  if (!mid.ok()) return mid.error();

  std::vector<std::vector<std::uint8_t>> sliceBytes;
  for (std::size_t i = 0; i < n; ++i) {
    const ChopInfo& c = chops_->chops[i];
    const std::int64_t len = c.range.length();
    std::vector<std::vector<float>> audio(static_cast<std::size_t>(sourceData_->channels));
    const std::int64_t fin = std::min<std::int64_t>(c.fadeInFrames ? c.fadeInFrames : 32, len / 2);
    const std::int64_t fout = std::min<std::int64_t>(c.fadeOutFrames ? c.fadeOutFrames : 64, len / 2);
    for (int ch = 0; ch < sourceData_->channels; ++ch) {
      const float* src = sourceData_->channel(ch) + c.range.start;
      auto& dst = audio[static_cast<std::size_t>(ch)];
      dst.assign(src, src + len);
      for (std::int64_t k = 0; k < fin; ++k) dst[static_cast<std::size_t>(k)] *= static_cast<float>(k) / static_cast<float>(fin);
      for (std::int64_t k = 0; k < fout; ++k) dst[static_cast<std::size_t>(len - 1 - k)] *= static_cast<float>(k) / static_cast<float>(fout);
    }
    wav::Options wo;
    wo.sampleRate = sourceData_->sampleRate;
    wo.format = wav::Format::Pcm24;
    wo.dither = false;
    auto b = wav::encode(audio, wo);
    if (!b.ok()) return b.error();
    sliceBytes.push_back(std::move(b.value()));
  }
  std::string map = "ChopFractal Producer Kit\nnote\tfile\trole\tsource_frames\n";
  for (std::size_t i = 0; i < n; ++i) {
    const std::string role = roles_.roleOf(chops_->chops[i].id);
    map += std::to_string(o.baseNote + static_cast<int>(i)) + "\t" + sliceNames[i] + "\t" + (role.empty() ? "-" : role) + "\t" +
           std::to_string(chops_->chops[i].range.start) + "-" + std::to_string(chops_->chops[i].range.end) + "\n";
  }

  fs::create_directories(dir, ec);
  if (ec) return makeError(ErrorCode::InvalidArgument, "cannot create " + directory);
  ExportReport rep;
  std::vector<fs::path> written;
  auto fail = [&](const Error& e) -> Result<ExportReport> {
    if (!o.overwrite)
      for (const fs::path& w : written) fs::remove(w, ec);  // no partial kit (with overwrite the old files are already replaced)
    return e;
  };
  for (std::size_t i = 0; i < n; ++i) {
    Status w = wav::writeFileAtomic(targets[i].string(), sliceBytes[i], o.overwrite);
    if (!w.ok()) return fail(w.error());
    written.push_back(targets[i]);
    rep.files.push_back(targets[i].string());
  }
  Status wm = wav::writeFileAtomic(targets[n].string(), mid.value(), o.overwrite);
  if (!wm.ok()) return fail(wm.error());
  written.push_back(targets[n]);
  rep.files.push_back(targets[n].string());
  Status wk = wav::writeFileAtomic(targets[n + 1].string(), std::vector<std::uint8_t>(map.begin(), map.end()), o.overwrite);
  if (!wk.ok()) return fail(wk.error());
  rep.files.push_back(targets[n + 1].string());
  rep.notes = spec.notes.size();
  return rep;
}

}  // namespace chopfractal::composition
