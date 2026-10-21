#include <chopfractal/composition/export_job.hpp>
#include <chopfractal/midi_export/midi.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace chopfractal::composition {
namespace {

namespace fs = std::filesystem;

std::string twoDigits(std::size_t n) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02zu", n);
  return buf;
}

void report(ExportProgress& progress, float fraction) {
  progress.fraction.store(fraction);
  if (progress.observer) progress.observer(fraction);
}

Error cancelled() { return makeError(ErrorCode::Cancelled, "export cancelled"); }

std::string leafName(const std::string& path) {
  fs::path p = fs::u8path(path);
  if (p.filename().empty()) p = p.parent_path();  // "dir/" -> "dir"
  return p.filename().u8string();
}

}  // namespace

Result<ExportJob> ExportJob::planWav(const ProjectSession& session, std::string path, const ExportWavOptions& o) {
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  if (!(o.tailSeconds >= 0.0 && o.tailSeconds <= 30.0)) return makeError(ErrorCode::OutOfRange, "tail must be 0 to 30 seconds");
  const auto playback = session.playback();
  if (!playback || playback->passThrough || !playback->source) return makeError(ErrorCode::InvalidArgument, "there is no pattern to render");
  std::error_code existsEc;
  if (!o.overwrite && fs::exists(fs::u8path(path), existsEc)) return makeError(ErrorCode::Conflict, "a file already exists at " + path);
  ExportJob job;
  job.kind_ = Kind::Wav;
  job.name_ = leafName(path);
  job.target_ = std::move(path);
  job.wavOptions_ = o;
  job.playback_ = playback;
  job.renderParams_ = session.renderParams();
  job.renderParams_.enabled = true;
  job.renderParams_.dryMix = 0.f;
  return job;
}

Result<ExportJob> ExportJob::planKit(const ProjectSession& session, std::string directory, const KitOptions& o) {
  const auto source = session.sourceData();
  const ChopSnapshotPtr chops = session.chops();
  const auto playback = session.playback();
  if (!source || !chops || chops->chops.empty()) return makeError(ErrorCode::InvalidArgument, "load a source with chops first");
  if (!session.pattern() || !playback || playback->passThrough) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  const std::size_t n = chops->chops.size();
  if (n > 256) return makeError(ErrorCode::LimitExceeded, "too many chops for a kit", 256);
  if (o.baseNote < 0 || o.baseNote > 128 - static_cast<int>(n))
    return makeError(ErrorCode::OutOfRange, "the chops do not fit into MIDI notes 0 to 127 from this base note", 127 - static_cast<int>(n) + 1);
  const std::string stem = wav::sanitizeFileName(o.name);

  // Plan every file name first, then refuse (before anything is written) if any already exists.
  std::vector<std::string> sliceNames;
  for (std::size_t i = 0; i < n; ++i) {
    const std::string role = session.roleMap().roleOf(chops->chops[i].id);
    sliceNames.push_back("slice_" + twoDigits(i + 1) + "_" + (role.empty() ? std::string("chop") : wav::sanitizeFileName(role)) + ".wav");
  }
  const fs::path dir = fs::u8path(directory);
  std::vector<fs::path> targets;
  for (const std::string& s : sliceNames) targets.push_back(dir / s);
  targets.push_back(dir / (stem + ".mid"));
  targets.push_back(dir / "kit.txt");
  std::error_code ec;
  if (!o.overwrite)
    for (const fs::path& t : targets)
      if (fs::exists(t, ec)) return makeError(ErrorCode::Conflict, "a file already exists at " + t.u8string());

  // MIDI first (it can fail on limits) so that nothing is written for a request that cannot complete.
  auto mid = session.renderMidi(o);
  if (!mid.ok()) return mid.error();

  ExportJob job;
  job.kind_ = Kind::Kit;
  job.name_ = leafName(directory);
  job.target_ = std::move(directory);
  job.kitOptions_ = o;
  job.source_ = source;
  job.chops_ = chops;
  for (const fs::path& t : targets) job.targets_.push_back(t.u8string());
  job.midi_ = std::move(mid.value());
  job.map_ = "ChopFractal Producer Kit\nnote\tfile\trole\tsource_frames\n";
  for (std::size_t i = 0; i < n; ++i) {
    const std::string role = session.roleMap().roleOf(chops->chops[i].id);
    job.map_ += std::to_string(o.baseNote + static_cast<int>(i)) + "\t" + sliceNames[i] + "\t" + (role.empty() ? "-" : role) + "\t" +
                std::to_string(chops->chops[i].range.start) + "-" + std::to_string(chops->chops[i].range.end) + "\n";
  }
  return job;
}

Result<ExportReport> ExportJob::run(ExportProgress& progress) const {
  progress.fraction.store(0.f);
  if (progress.cancel.load()) return cancelled();
  return kind_ == Kind::Wav ? runWav(progress) : runKit(progress);
}

Result<ExportReport> ExportJob::runWav(ExportProgress& progress) const {
  const ExportWavOptions& o = wavOptions_;
  render::OfflineSettings rs;
  rs.sampleRate = o.wav.sampleRate;
  rs.bpm = o.bpm;
  rs.cycles = o.loops;
  rs.tailSeconds = o.tailSeconds;
  rs.params = renderParams_;
  auto audio = render::renderOffline(playback_, rs, [&progress](double f) {
    report(progress, static_cast<float>(f * 0.9));
    return !progress.cancel.load();
  });
  if (!audio.ok()) return audio.error();
  if (progress.cancel.load()) return cancelled();
  wav::EncodeReport rep;
  auto bytes = wav::encode(audio.value(), o.wav, &rep);
  if (!bytes.ok()) return bytes.error();
  report(progress, 0.95f);
  if (progress.cancel.load()) return cancelled();  // the last point before the file is created
  Status w = wav::writeFileAtomic(target_, bytes.value(), o.overwrite);  // re-checks that the file does not exist unless overwriting
  if (!w.ok()) return w.error();
  report(progress, 1.f);
  ExportReport out;
  out.frames = audio.value()[0].size();
  out.peak = rep.peak;
  out.clipped = rep.clipped;
  out.files.push_back(target_);
  return out;
}

Result<ExportReport> ExportJob::runKit(ExportProgress& progress) const {
  const KitOptions& o = kitOptions_;
  const std::size_t n = chops_->chops.size();
  std::vector<std::vector<std::uint8_t>> sliceBytes;
  for (std::size_t i = 0; i < n; ++i) {
    if (progress.cancel.load()) return cancelled();
    const ChopInfo& c = chops_->chops[i];
    const std::int64_t len = c.range.length();
    std::vector<std::vector<float>> audio(static_cast<std::size_t>(source_->channels));
    const std::int64_t fin = std::min<std::int64_t>(c.fadeInFrames ? c.fadeInFrames : 32, len / 2);
    const std::int64_t fout = std::min<std::int64_t>(c.fadeOutFrames ? c.fadeOutFrames : 64, len / 2);
    for (int ch = 0; ch < source_->channels; ++ch) {
      const float* src = source_->channel(ch) + c.range.start;
      auto& dst = audio[static_cast<std::size_t>(ch)];
      dst.assign(src, src + len);
      for (std::int64_t k = 0; k < fin; ++k) dst[static_cast<std::size_t>(k)] *= static_cast<float>(k) / static_cast<float>(fin);
      for (std::int64_t k = 0; k < fout; ++k) dst[static_cast<std::size_t>(len - 1 - k)] *= static_cast<float>(k) / static_cast<float>(fout);
    }
    wav::Options wo;
    wo.sampleRate = source_->sampleRate;
    wo.format = wav::Format::Pcm24;
    wo.dither = false;
    auto b = wav::encode(audio, wo);
    if (!b.ok()) return b.error();
    sliceBytes.push_back(std::move(b.value()));
  }

  std::error_code ec;
  fs::create_directories(fs::u8path(target_), ec);
  if (ec) return makeError(ErrorCode::InvalidArgument, "cannot create " + target_);
  ExportReport rep;
  std::vector<fs::path> written;
  std::vector<fs::path> created;  // written by this run and not there before
  const double steps = static_cast<double>(n + 2);
  auto cleanup = [&](bool cancelledRun) {
    if (cancelledRun) {
      for (const fs::path& p : created) fs::remove(p, ec);  // overwritten files stay: their old content is gone already
    } else if (!o.overwrite) {
      for (const fs::path& p : written) fs::remove(p, ec);  // no partial kit (with overwrite the old files are already replaced)
    }
  };
  for (std::size_t i = 0; i < n + 2; ++i) {
    if (progress.cancel.load()) {
      cleanup(true);
      return cancelled();
    }
    const fs::path path = fs::u8path(targets_[i]);
    const bool existed = fs::exists(path, ec);
    const std::vector<std::uint8_t>* payload = nullptr;
    std::vector<std::uint8_t> mapBytes;
    if (i < n) {
      payload = &sliceBytes[i];
    } else if (i == n) {
      payload = &midi_;
    } else {
      mapBytes.assign(map_.begin(), map_.end());
      payload = &mapBytes;
    }
    Status w = wav::writeFileAtomic(targets_[i], *payload, o.overwrite);
    if (!w.ok()) {
      cleanup(false);
      return w.error();
    }
    written.push_back(path);
    if (!existed) created.push_back(path);
    rep.files.push_back(targets_[i]);
    report(progress, static_cast<float>(static_cast<double>(i + 1) / steps));
  }
  if (auto dec = midi::decode(midi_.data(), midi_.size()); dec.ok()) rep.notes = dec.value().notes.size();
  report(progress, 1.f);
  return rep;
}

}  // namespace chopfractal::composition
