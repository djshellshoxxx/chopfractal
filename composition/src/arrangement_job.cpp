// F18 arrangement render and worker job (see arrangement_job.hpp and docs/specs/roadmap/F18-arrangement-export.md).
//
// Every section is rendered with render::renderOffline (the same Renderer::process path as playback) into its own buffer and
// the buffers are overlap-added at integer bar-frame offsets. Nothing here touches a live session object.
#include <chopfractal/composition/arrangement_job.hpp>

#include <chopfractal/audio_renderer/limiter.hpp>
#include <chopfractal/level_match/level_match.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace chopfractal::composition {
namespace {

constexpr int kBlock = 512;
constexpr double kPi = 3.14159265358979323846;

double tpfFor(double bpm, double sr) { return bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * sr); }

std::int64_t tailFramesOf(const ArrangementPlan& p) {
  if (!p.options.tail || p.options.fadeOut) return 0;
  return static_cast<std::int64_t>(std::ceil(kArrangementTailSeconds * static_cast<double>(p.wav.sampleRate)));
}

std::int64_t sectionFrames(const ArrangementPlan& p, const PlannedSection& s) {
  return static_cast<std::int64_t>(s.bars) * static_cast<std::int64_t>(s.repeats) * barFrames(p.bpm, p.wav.sampleRate);
}

std::string cleanLabel(const std::string& s) {
  std::string out;
  for (char c : s) out.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7F ? ' ' : c);
  return out;
}

// The playback the section renders: the scene's own when whole pattern lengths fill a bar group, otherwise a copy that
// holds exactly `bars` bars of the pattern looped (a shorter pattern repeats, a longer one is cut at the section end).
std::shared_ptr<const render::Playback> effectivePlayback(const PlannedSection& s, std::int64_t& cycles) {
  const Ticks groupTicks = static_cast<Ticks>(s.bars) * kTicksPerWhole;
  const Ticks len = s.playback->lengthTicks;
  if (len > 0 && groupTicks % len == 0) {
    cycles = static_cast<std::int64_t>(s.repeats) * (groupTicks / len);
    return s.playback;
  }
  cycles = s.repeats;
  auto pb = std::make_shared<render::Playback>();
  pb->source = s.playback->source;
  pb->lengthTicks = groupTicks;
  const render::Playback& src = *s.playback;
  if (len <= 0 || src.events.empty()) return pb;
  const bool hasSlices = src.slices.size() == src.events.size();
  struct Item {
    Ticks start;
    std::size_t index;
    Ticks offset;
  };
  std::vector<Item> items;
  for (Ticks off = 0; off < groupTicks; off += len)
    for (std::size_t i = 0; i < src.events.size(); ++i)
      if (src.events[i].start + off < groupTicks) items.push_back({src.events[i].start + off, i, off});
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.start < b.start; });
  for (const Item& it : items) {
    FlatEvent e = src.events[it.index];
    e.start = it.start;
    pb->events.push_back(std::move(e));
    if (hasSlices) pb->slices.push_back(src.slices[it.index]);
  }
  return pb;
}

// Progress bookkeeping: work units are frames; the last 5 % is assemble, encode and write.
struct Meter {
  const ArrangementPlan& plan;
  ArrangementControl& control;
  double total = 1.0, done = 0.0;
  void set(double units) {
    const float f = static_cast<float>(0.95 * std::min(1.0, std::max(0.0, (done + units) / total)));
    if (f > control.fraction.load(std::memory_order_relaxed)) control.fraction.store(f, std::memory_order_relaxed);
    if (plan.observer) plan.observer(control.fraction.load(std::memory_order_relaxed));
  }
  void finish(double units) { done += units; set(0.0); }
};

}  // namespace

std::int64_t barFrames(double bpm, double sampleRate) {
  return static_cast<std::int64_t>(std::llround(static_cast<double>(kTicksPerWhole) / tpfFor(bpm, sampleRate)));
}

std::int64_t crossfadeFrames(Transition t, double sampleRate) {
  switch (t) {
    case Transition::Crossfade10: return static_cast<std::int64_t>(std::llround(0.010 * sampleRate));
    case Transition::Crossfade50: return static_cast<std::int64_t>(std::llround(0.050 * sampleRate));
    default: return 0;
  }
}

std::int64_t mainFrames(const ArrangementPlan& p) {
  return static_cast<std::int64_t>(p.options.totalBars()) * barFrames(p.bpm, p.wav.sampleRate);
}

std::int64_t totalFrames(const ArrangementPlan& p) { return mainFrames(p) + tailFramesOf(p); }

Result<ArrangementAudio> renderArrangement(const ArrangementPlan& plan, ArrangementControl& control) {
  const double sr = plan.wav.sampleRate;
  if (plan.sections.empty() || plan.sections.size() != plan.options.sections.size())
    return makeError(ErrorCode::InvalidArgument, "The arrangement has no sections.");
  if (!plan.options.valid()) return makeError(ErrorCode::OutOfRange, "Arrangement needs 8 to 32 bars (now " + std::to_string(plan.options.totalBars()) + ").");
  if (!(sr >= 8000.0 && sr <= 384000.0)) return makeError(ErrorCode::OutOfRange, "invalid sample rate");
  if (!(plan.bpm >= 20.0 && plan.bpm <= 999.0)) return makeError(ErrorCode::OutOfRange, "the tempo must be 20 to 999 BPM");
  const std::int64_t bar = barFrames(plan.bpm, sr);
  const std::int64_t main = mainFrames(plan);
  const std::int64_t tail = tailFramesOf(plan);
  const std::int64_t total = main + tail;
  if (bar < 1 || total > kMaxArrangementFrames) return makeError(ErrorCode::LimitExceeded, "Arrangement is too long for this sample rate.", kMaxArrangementFrames);
  for (const PlannedSection& s : plan.sections)
    if (!s.playback || !s.playback->source || s.playback->passThrough) return makeError(ErrorCode::InvalidArgument, "A section has no pattern to render.");

  const std::size_t n = plan.sections.size();
  const std::int64_t N = crossfadeFrames(plan.options.transition, sr);
  const bool limiterOn = plan.params.limiterEnabled;
  const std::int64_t lat = limiterOn ? render::Limiter::latencyFor(sr) : 0;
  control.sections.store(static_cast<int>(n), std::memory_order_relaxed);

  // ---- work units ----
  Meter meter{plan, control};
  meter.total = 0.0;
  std::map<const render::Playback*, float> matchDb;  // one measurement per distinct scene
  std::map<const render::Playback*, double> matchUnits;
  if (plan.levelMatch)
    for (const PlannedSection& s : plan.sections)
      if (matchUnits.emplace(s.playback.get(), static_cast<double>(s.playback->lengthTicks) / tpfFor(plan.bpm, sr)).second) meter.total += matchUnits[s.playback.get()];
  for (std::size_t k = 0; k < n; ++k) meter.total += static_cast<double>(sectionFrames(plan, plan.sections[k]) + (k + 1 == n ? tail : N));
  meter.total = std::max(1.0, meter.total);

  render::RenderParams base = plan.params;
  base.enabled = true;
  base.dryMix = 0.f;
  base.limiterEnabled = false;  // the limiter runs once over the final mix

  ArrangementAudio out;
  out.matchGainDb.assign(n, 0.f);

  // ---- per-scene level match (F16 functions, recomputed here so each section is leveled to the loop) ----
  if (plan.levelMatch) {
    for (std::size_t k = 0; k < n; ++k) {
      const render::Playback* key = plan.sections[k].playback.get();
      auto it = matchDb.find(key);
      if (it == matchDb.end()) {
        if (control.cancel.load()) return makeError(ErrorCode::Cancelled, "Export cancelled. No files were written.");
        const render::SourceData& src = *key->source;
        const float* planes[2] = {src.channel(0), src.channel(src.channels > 1 ? 1 : 0)};
        const levelmatch::Loudness loop = levelmatch::measure(planes, 2, src.frames, src.sampleRate);
        render::OfflineSettings rs;
        rs.sampleRate = sr;
        rs.bpm = plan.bpm;
        rs.blockSize = kBlock;
        rs.cycles = 1;
        rs.tailSeconds = 0.0;
        rs.params = base;
        rs.params.outputGainDb = 0.f;
        rs.params.matchGainDb = 0.f;
        const double units = matchUnits[key];
        auto audio = render::renderOffline(plan.sections[k].playback, rs, [&](double f) {
          meter.set(f * units);
          return !control.cancel.load();
        });
        if (!audio.ok()) return audio.error();
        meter.finish(units);
        const auto& ch = audio.value();
        const float* pp[2] = {ch[0].data(), ch[ch.size() > 1 ? 1 : 0].data()};
        const levelmatch::Loudness pat = levelmatch::measure(pp, 2, static_cast<std::int64_t>(ch[0].size()), static_cast<int>(sr));
        const levelmatch::MatchResult m = levelmatch::computeMatch(loop, pat, plan.matchTrimDb, plan.params.limiterEnabled, plan.params.limiterCeilingDb);
        it = matchDb.emplace(key, m.valid ? static_cast<float>(m.gainDb) : 0.f).first;
      }
      out.matchGainDb[k] = it->second;
    }
  }

  // ---- render and assemble ----
  std::vector<std::vector<float>> mix(2, std::vector<float>(static_cast<std::size_t>(total + lat), 0.f));
  std::vector<float> gIn(static_cast<std::size_t>(N));
  for (std::int64_t i = 0; i < N; ++i) gIn[static_cast<std::size_t>(i)] = crossfadeGainIn(i, N);

  std::int64_t start = 0;
  for (std::size_t k = 0; k < n; ++k) {
    if (control.cancel.load()) return makeError(ErrorCode::Cancelled, "Export cancelled. No files were written.");
    control.section.store(static_cast<int>(k + 1), std::memory_order_relaxed);
    const PlannedSection& s = plan.sections[k];
    const std::int64_t F = sectionFrames(plan, s);
    const bool last = k + 1 == n;
    const std::int64_t extra = last ? tail : N;
    out.sectionStarts.push_back(start);

    std::int64_t cycles = 1;
    std::shared_ptr<const render::Playback> pb = effectivePlayback(s, cycles);
    if (cycles < 1 || cycles > 64) return makeError(ErrorCode::OutOfRange, "A section is too long to render.");
    render::OfflineSettings rs;
    rs.sampleRate = sr;
    rs.bpm = plan.bpm;
    rs.blockSize = kBlock;
    rs.cycles = static_cast<int>(cycles);
    rs.tailSeconds = static_cast<double>(extra + 64) / sr;  // ring-out past the section end; the 64 frames absorb rounding of the loop length
    rs.params = base;
    rs.params.matchGainDb = out.matchGainDb[k];
    const double units = static_cast<double>(F + extra);
    auto audio = render::renderOffline(pb, rs, [&](double f) {
      meter.set(f * units);
      return !control.cancel.load();
    });
    if (!audio.ok()) return audio.error();
    meter.finish(units);
    const auto& seg = audio.value();
    const std::int64_t have = static_cast<std::int64_t>(seg[0].size());
    auto at = [&](std::size_t ch, std::int64_t i) -> float { return i < have ? seg[ch][static_cast<std::size_t>(i)] : 0.f; };

    for (std::size_t ch = 0; ch < 2; ++ch) {
      float* dst = mix[ch].data() + start;
      for (std::int64_t i = 0; i < F; ++i) {
        const float g = (k > 0 && i < N) ? gIn[static_cast<std::size_t>(i)] : 1.f;
        dst[i] += at(ch, i) * g;
      }
      if (last) {
        const std::int64_t fade = std::min<std::int64_t>(tail, static_cast<std::int64_t>(std::llround(kArrangementTailFadeSeconds * sr)));
        for (std::int64_t i = 0; i < tail; ++i) {
          float g = 1.f;
          if (i >= tail - fade) g = static_cast<float>(tail - 1 - i) / static_cast<float>(fade);
          dst[F + i] += at(ch, F + i) * g;
        }
      } else {
        for (std::int64_t i = 0; i < N; ++i) dst[F + i] += at(ch, F + i) * (1.f - gIn[static_cast<std::size_t>(i)]);
      }
    }
    start += F;
  }

  // ---- fade out (replaces the tail), then the limiter once over everything ----
  if (plan.options.fadeOut) {
    for (std::size_t ch = 0; ch < 2; ++ch)
      for (std::int64_t i = 0; i < bar && i < main; ++i) {
        const double g = 0.5 * (1.0 + std::cos(kPi * static_cast<double>(i + 1) / static_cast<double>(bar)));
        float& v = mix[ch][static_cast<std::size_t>(main - bar + i)];
        v = static_cast<float>(static_cast<double>(v) * g);
      }
  }
  if (limiterOn) {
    render::Limiter lim;
    lim.prepare(sr, kBlock);
    lim.prime();
    const render::LimiterParams lp{true, plan.params.limiterCeilingDb};
    const std::int64_t len = total + lat;
    for (std::int64_t pos = 0; pos < len; pos += kBlock) {
      const int cnt = static_cast<int>(std::min<std::int64_t>(kBlock, len - pos));
      lim.process(lp, mix[0].data() + pos, mix[1].data() + pos, cnt);
    }
    for (auto& ch : mix) {
      ch.erase(ch.begin(), ch.begin() + static_cast<std::ptrdiff_t>(lat));
      ch.resize(static_cast<std::size_t>(total));
    }
  }
  out.planar = std::move(mix);
  return out;
}

namespace {

// The notes of every section placed on the arrangement's tick grid (960 per quarter), clipped to their span.
std::vector<midi::Note> placeNotes(const ArrangementPlan& plan) {
  std::vector<midi::Note> notes;
  Ticks base = 0;
  for (const PlannedSection& s : plan.sections) {
    const Ticks group = static_cast<Ticks>(s.bars) * kTicksPerWhole;
    const Ticks len = s.lengthTicks > 0 ? s.lengthTicks : group;
    const bool whole = group % len == 0;
    const Ticks span = whole ? group * s.repeats : group;  // one clipping span
    const int spans = whole ? 1 : s.repeats;
    for (int r = 0; r < spans; ++r) {
      const Ticks spanStart = base + static_cast<Ticks>(r) * span;
      for (Ticks off = 0; off < span; off += len)
        for (const midi::Note& n : s.notes) {
          const Ticks at = off + n.start;
          if (at >= span) continue;
          midi::Note m = n;
          m.start = spanStart + at;
          m.duration = std::min<Ticks>(n.duration, span - at);
          if (m.duration > 0) notes.push_back(m);
        }
    }
    base += group * s.repeats;
  }
  std::stable_sort(notes.begin(), notes.end(), [](const midi::Note& a, const midi::Note& b) { return a.start < b.start; });
  return notes;
}

struct OutFile {
  fs::path finalPath, partial;
  std::vector<std::uint8_t> bytes;
};

ArrangementResult fail(Error e) {
  ArrangementResult r;
  r.status = std::move(e);
  return r;
}

Error cancelledError() { return Error{ErrorCode::Cancelled, "Export cancelled. No files were written.", 0}; }

}  // namespace

ArrangementResult runArrangement(const ArrangementPlan& plan, ArrangementControl& control) {
  control.fraction.store(0.f, std::memory_order_relaxed);
  if (control.cancel.load()) return fail(cancelledError());
  if (plan.root.empty()) return fail(Error{ErrorCode::InvalidArgument, "Choose a folder for the arrangement.", 0});

  std::vector<midi::Note> notes;
  if (plan.options.writeMidi) {
    notes = placeNotes(plan);
    if (notes.size() > midi::kMaxNotes) return fail(Error{ErrorCode::LimitExceeded, "The arrangement has too many notes for one MIDI file.", static_cast<std::int64_t>(midi::kMaxNotes)});
  }

  auto audio = renderArrangement(plan, control);
  if (!audio.ok()) return fail(audio.error().code == ErrorCode::Cancelled ? cancelledError() : audio.error());
  if (control.cancel.load()) return fail(cancelledError());

  const double sr = plan.wav.sampleRate;
  const std::int64_t total = totalFrames(plan);
  const bool markers = plan.options.writeMarkers;

  std::vector<wav::CuePoint> cues;
  std::string cueText;
  midi::FileSpec spec;
  {
    std::int64_t bars = 0;
    for (std::size_t k = 0; k < plan.sections.size(); ++k) {
      const std::string label = cleanLabel(plan.sections[k].label);
      cues.push_back({static_cast<std::uint64_t>(audio.value().sectionStarts[k]), label});
      char line[64];
      std::snprintf(line, sizeof line, "%d,%.3f,", static_cast<int>(bars + 1), static_cast<double>(audio.value().sectionStarts[k]) / sr);
      cueText += line + label + "\n";
      spec.markers.push_back({bars * kTicksPerWhole, label});
      bars += static_cast<std::int64_t>(plan.sections[k].bars) * plan.sections[k].repeats;
    }
  }
  spec.bpm = plan.bpm;
  spec.numerator = 4;
  spec.denominator = 4;
  spec.trackName = plan.trackName.substr(0, 60);
  spec.notes = std::move(notes);
  spec.endTick = static_cast<Ticks>(plan.options.totalBars()) * kTicksPerWhole;
  if (!markers) spec.markers.clear();

  wav::EncodeReport rep;
  auto wavBytes = wav::encodeWithCues(audio.value().planar, plan.wav, markers ? cues : std::vector<wav::CuePoint>{}, &rep);
  if (!wavBytes.ok()) return fail(wavBytes.error());
  std::vector<std::uint8_t> midiBytes;
  if (plan.options.writeMidi) {
    auto m = midi::encode(spec);
    if (!m.ok()) return fail(m.error());
    midiBytes = std::move(m.value());
  }
  control.fraction.store(std::max(control.fraction.load(), 0.97f), std::memory_order_relaxed);
  if (plan.observer) plan.observer(control.fraction.load());
  if (control.cancel.load()) return fail(cancelledError());

  // ---- files: WAV, MIDI, cue list; written as .partial, then renamed together ----
  std::error_code ec;
  const fs::path root = fs::u8path(plan.root);
  fs::create_directories(root, ec);
  if (ec) return fail(Error{ErrorCode::Blocked, "Could not write " + plan.root + ": " + ec.message(), 0});
  std::string stem;
  for (int i = 1; i < 10000; ++i) {
    stem = plan.name + "_arrangement" + (i > 1 ? "_" + std::to_string(i) : std::string());
    bool clash = fs::exists(root / (stem + ".wav"), ec);
    if (plan.options.writeMidi) clash = clash || fs::exists(root / (stem + ".mid"), ec);
    if (markers) clash = clash || fs::exists(root / (stem + "_cues.txt"), ec);
    if (!clash) break;
  }
  std::vector<OutFile> files;
  files.push_back({root / (stem + ".wav"), {}, std::move(wavBytes.value())});
  if (plan.options.writeMidi) files.push_back({root / (stem + ".mid"), {}, std::move(midiBytes)});
  if (markers) files.push_back({root / (stem + "_cues.txt"), {}, std::vector<std::uint8_t>(cueText.begin(), cueText.end())});
  auto removePartials = [&]() {
    for (const OutFile& f : files)
      if (!f.partial.empty()) fs::remove(f.partial, ec);
  };
  for (OutFile& f : files) {
    f.partial = f.finalPath;
    f.partial += ".partial";
    std::ofstream os(f.partial, std::ios::binary | std::ios::trunc);
    if (os) os.write(reinterpret_cast<const char*>(f.bytes.data()), static_cast<std::streamsize>(f.bytes.size()));
    if (os) os.flush();
    if (!os) {
      const std::string why = errno != 0 ? std::strerror(errno) : "write failed";
      os.close();
      removePartials();
      return fail(Error{ErrorCode::Blocked, "Could not write " + f.finalPath.u8string() + ": " + why, 0});
    }
  }
  if (control.cancel.load()) {
    removePartials();
    return fail(cancelledError());
  }
  std::vector<fs::path> renamed;  // the rename phase is not cancellable
  for (OutFile& f : files) {
    fs::rename(f.partial, f.finalPath, ec);
    if (ec) {
      const std::string why = ec.message();
      for (const fs::path& p : renamed) fs::remove(p, ec);
      removePartials();
      return fail(Error{ErrorCode::Blocked, "Could not write " + f.finalPath.u8string() + ": " + why, 0});
    }
    f.partial.clear();
    renamed.push_back(f.finalPath);
  }

  ArrangementResult r;
  r.wavPath = files[0].finalPath.u8string();
  std::size_t idx = 1;
  if (plan.options.writeMidi) r.midiPath = files[idx++].finalPath.u8string();
  if (markers) r.cuePath = files[idx++].finalPath.u8string();
  r.bars = plan.options.totalBars();
  r.frames = total;
  r.seconds = static_cast<double>(total) / sr;
  r.peak = rep.peak;
  r.clipped = rep.clipped;
  control.fraction.store(1.f, std::memory_order_relaxed);
  if (plan.observer) plan.observer(1.f);
  return r;
}

// ---------------------------------------------------------------------------------------------
// the worker
// ---------------------------------------------------------------------------------------------

ArrangementJob::ArrangementJob(ArrangementPlan plan) : plan_(std::move(plan)) {
  control_.sections.store(static_cast<int>(plan_.sections.size()), std::memory_order_relaxed);
}

std::unique_ptr<ArrangementJob> ArrangementJob::start(ArrangementPlan plan) {
  std::unique_ptr<ArrangementJob> job(new ArrangementJob(std::move(plan)));
  ArrangementJob* self = job.get();
  self->thread_ = std::thread([self] {
    self->result_ = runArrangement(self->plan_, self->control_);
    self->done_.store(true, std::memory_order_release);
  });
  return job;
}

ArrangementJob::~ArrangementJob() {
  control_.cancel.store(true);
  if (thread_.joinable()) thread_.join();
}

ArrangementProgress ArrangementJob::progress() const {
  ArrangementProgress p;
  p.section = control_.section.load(std::memory_order_relaxed);
  p.sections = control_.sections.load(std::memory_order_relaxed);
  p.fraction = static_cast<double>(control_.fraction.load(std::memory_order_relaxed));
  return p;
}

bool ArrangementJob::done() const { return done_.load(std::memory_order_acquire); }

const ArrangementResult& ArrangementJob::result() {
  if (thread_.joinable()) thread_.join();
  return result_;
}

void ArrangementJob::cancel() { control_.cancel.store(true); }

}  // namespace chopfractal::composition
