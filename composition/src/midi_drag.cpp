// F13: MIDI drag-out and per-variation export. The note-building loop of the Producer Kit lives here so that the kit
// file, the drag file and the variation file are produced by one function.
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "midi_build.hpp"

namespace chopfractal::composition {

int midiVelocity(float level, VelocityCurve curve, int fixedVelocity) {
  if (curve == VelocityCurve::Fixed) return std::min(127, std::max(1, fixedVelocity));
  const double l = static_cast<double>(level);
  const double lc = std::min(l, 1.0);
  double v = l * 127.0;  // Linear: exactly the pre-F13 rule
  if (curve == VelocityCurve::Soft) v = 127.0 * lc * lc;
  if (curve == VelocityCurve::Hard) v = 127.0 * std::sqrt(std::max(0.0, lc));
  return std::min(127, std::max(1, static_cast<int>(std::lround(v))));
}

namespace midibuild {

Status validate(const MidiExportOptions& o, std::size_t n) {
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  if (n > 256) return makeError(ErrorCode::LimitExceeded, "too many chops for a kit", 256);
  if (o.baseNote < 0 || o.baseNote > 128 - static_cast<int>(n))
    return makeError(ErrorCode::OutOfRange, "the chops do not fit into MIDI notes 0 to 127 from this base note", 127 - static_cast<int>(n) + 1);
  if (o.channel < 0 || o.channel > 15) return makeError(ErrorCode::OutOfRange, "the MIDI channel must be 1 to 16");
  if (static_cast<std::uint8_t>(o.curve) > 3) return makeError(ErrorCode::OutOfRange, "unknown velocity curve");
  if (o.curve == VelocityCurve::Fixed && (o.fixedVelocity < 1 || o.fixedVelocity > 127)) return makeError(ErrorCode::OutOfRange, "the fixed velocity must be 1 to 127");
  return {};
}

MidiExportOptions fromKit(const KitOptions& k) {
  MidiExportOptions o;
  o.baseNote = k.baseNote;
  o.bpm = k.bpm;
  o.loops = k.loops;
  o.curve = k.curve;
  o.fixedVelocity = k.fixedVelocity;
  o.channel = k.channel;
  o.trackName = k.name;
  return o;
}

Result<midi::FileSpec> buildSpec(const FlatEventList& events, Ticks lengthTicks, TimeSignature ts, const ChopSnapshot& chops, const MidiExportOptions& o,
                                 int* skipped) {
  Status v = validate(o, chops.chops.size());
  if (!v.ok()) return v.error();
  std::map<ChopId, int> index;
  for (std::size_t i = 0; i < chops.chops.size(); ++i) index[chops.chops[i].id] = static_cast<int>(i);
  midi::FileSpec spec;
  spec.bpm = o.bpm;
  spec.numerator = ts.numerator;
  spec.denominator = ts.denominator;
  spec.trackName = o.trackName.substr(0, 60);
  int skippedCount = 0;
  for (int loop = 0; loop < o.loops; ++loop)
    for (const FlatEvent& e : events) {
      auto it = index.find(e.chop);
      if (it == index.end()) {
        if (loop == 0) ++skippedCount;
        continue;
      }
      midi::Note note;
      note.note = o.baseNote + it->second;
      note.velocity = midiVelocity(e.tx.level, o.curve, o.fixedVelocity);
      note.start = static_cast<Ticks>(loop) * lengthTicks + e.start;
      note.duration = e.duration;
      note.channel = o.channel;
      spec.notes.push_back(note);
    }
  if (skipped) *skipped = skippedCount;
  return spec;
}

std::vector<std::string> sliceFileNames(const roles::RoleMap& roles, const ChopSnapshot& chops) {
  std::vector<std::string> names;
  for (std::size_t i = 0; i < chops.chops.size(); ++i) {
    char num[32];
    std::snprintf(num, sizeof num, "%02zu", i + 1);
    const std::string role = roles.roleOf(chops.chops[i].id);
    names.push_back(std::string("slice_") + num + "_" + (role.empty() ? std::string("chop") : wav::sanitizeFileName(role)) + ".wav");
  }
  return names;
}

Result<std::vector<std::uint8_t>> encodeSlice(const render::SourceData& source, const ChopInfo& c) {
  const std::int64_t len = c.range.length();
  std::vector<std::vector<float>> audio(static_cast<std::size_t>(source.channels));
  const std::int64_t fin = std::min<std::int64_t>(c.fadeInFrames ? c.fadeInFrames : 32, len / 2);
  const std::int64_t fout = std::min<std::int64_t>(c.fadeOutFrames ? c.fadeOutFrames : 64, len / 2);
  for (int ch = 0; ch < source.channels; ++ch) {
    const float* src = source.channel(ch) + c.range.start;
    auto& dst = audio[static_cast<std::size_t>(ch)];
    dst.assign(src, src + len);
    for (std::int64_t k = 0; k < fin; ++k) dst[static_cast<std::size_t>(k)] *= static_cast<float>(k) / static_cast<float>(fin);
    for (std::int64_t k = 0; k < fout; ++k) dst[static_cast<std::size_t>(len - 1 - k)] *= static_cast<float>(k) / static_cast<float>(fout);
  }
  wav::Options wo;
  wo.sampleRate = source.sampleRate;
  wo.format = wav::Format::Pcm24;
  wo.dither = false;
  return wav::encode(audio, wo);
}

Result<std::vector<std::uint8_t>> renderWavFromPlayback(const std::shared_ptr<const render::Playback>& playback, const ExportWavOptions& o,
                                                        const render::RenderParams& params, wav::EncodeReport* report, std::size_t* frames) {
  if (o.loops < 1 || o.loops > 16) return makeError(ErrorCode::OutOfRange, "loops must be 1 to 16");
  if (!(o.tailSeconds >= 0.0 && o.tailSeconds <= 30.0)) return makeError(ErrorCode::OutOfRange, "tail must be 0 to 30 seconds");
  if (!playback || playback->passThrough || !playback->source) return makeError(ErrorCode::InvalidArgument, "there is no pattern to render");
  render::OfflineSettings rs;
  rs.sampleRate = o.wav.sampleRate;
  rs.bpm = o.bpm;
  rs.cycles = o.loops;
  rs.tailSeconds = o.tailSeconds;
  rs.params = params;
  rs.params.enabled = true;
  rs.params.dryMix = 0.f;
  auto audio = render::renderOffline(playback, rs);
  if (!audio.ok()) return audio.error();
  if (frames) *frames = audio.value()[0].size();
  return wav::encode(audio.value(), o.wav, report);
}

}  // namespace midibuild

namespace {

// The pattern a node stands for: the live pattern for the active node, else its stored snapshot repaired against today's chops.
Result<pattern::Pattern> nodePattern(const history::VariationTree& history, const pattern::PatternSession& patterns, const ChopSnapshot& chops,
                                     history::NodeId node, int* skipped) {
  pattern::Pattern raw;
  if (node == history.active() && patterns.hasPattern()) {
    raw = patterns.current();
  } else {
    auto snap = history.getSnapshot(node);
    if (!snap.ok()) return Error{static_cast<ErrorCode>(static_cast<int>(snap.error().code)), snap.error().message, snap.error().hint};
    auto p = pattern::deserialize(snap.value().payload->data(), snap.value().payload->size());
    if (!p.ok()) return makeError(ErrorCode::Corrupt, "snapshot " + std::to_string(node) + " could not be read: " + p.error().message, static_cast<std::int64_t>(node));
    raw = std::move(p.value());
  }
  pattern::Pattern fixed = pattern::sanitize(raw, chops);
  if (skipped) {
    const std::size_t before = pattern::eventCount(raw), after = pattern::eventCount(fixed);
    *skipped = before > after ? static_cast<int>(before - after) : 0;
  }
  return fixed;
}

}  // namespace

Result<std::vector<std::uint8_t>> ProjectSession::renderCurrentMidi(const MidiExportOptions& o) const {
  if (!chops_ || chops_->chops.empty()) return makeError(ErrorCode::InvalidArgument, "load a source with chops first");
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  FlatEventList events;
  Ticks length = pattern::lengthTicks(p);
  if (sourceData_) {
    if (!playback_ || playback_->passThrough) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
    auto midiEv = midiEvents();  // honours the groove's bake flag
    if (!midiEv.ok()) return midiEv.error();
    events = std::move(midiEv.value());
    length = playback_->lengthTicks;
  } else {  // the audio is missing: the chop map and the pattern are enough for notes
    auto flat = pattern::flatten(p, *chops_);
    if (!flat.ok()) return flat.error();
    events = render::expandRetriggers(flat.value(), length);
  }
  auto spec = midibuild::buildSpec(events, length, p.settings.timeSignature, *chops_, o);
  if (!spec.ok()) return spec.error();
  return midi::encode(spec.value());
}

Result<std::vector<std::uint8_t>> ProjectSession::renderVariationMidi(history::NodeId node, const MidiExportOptions& o, int* skipped) const {
  if (skipped) *skipped = 0;
  if (!chops_ || chops_->chops.empty()) return makeError(ErrorCode::InvalidArgument, "load a source with chops first");
  if (!history_.contains(node)) return makeError(ErrorCode::NotFound, "there is no variation " + std::to_string(node), static_cast<std::int64_t>(node));
  auto p = nodePattern(history_, patterns_, *chops_, node, skipped);
  if (!p.ok()) return p.error();
  FlatEventList events;
  const Ticks length = pattern::lengthTicks(p.value());
  if (sourceData_) {
    auto pb = buildPlayback(p.value(), groove_.bakeIntoMidi);  // the same expansion (gesture effects, groove) as playback
    if (!pb.ok()) return pb.error();
    events = pb.value()->events;
  } else {
    auto flat = pattern::flatten(p.value(), *chops_);
    if (!flat.ok()) return flat.error();
    events = render::expandRetriggers(flat.value(), length);
  }
  auto spec = midibuild::buildSpec(events, length, p.value().settings.timeSignature, *chops_, o);
  if (!spec.ok()) return spec.error();
  return midi::encode(spec.value());
}

Result<std::vector<std::uint8_t>> ProjectSession::renderVariationWav(history::NodeId node, const ExportWavOptions& o) const {
  if (!sourceData_ || !chops_) return makeError(ErrorCode::InvalidArgument, "the audio for this project is missing");
  if (!history_.contains(node)) return makeError(ErrorCode::NotFound, "there is no variation " + std::to_string(node), static_cast<std::int64_t>(node));
  auto p = nodePattern(history_, patterns_, *chops_, node, nullptr);
  if (!p.ok()) return p.error();
  auto pb = buildPlayback(p.value(), true);
  if (!pb.ok()) return pb.error();
  return midibuild::renderWavFromPlayback(pb.value(), o, renderParams());
}

Result<ExportReport> ProjectSession::exportVariationMidi(history::NodeId node, const std::string& path, const MidiExportOptions& o, bool overwrite) const {
  auto bytes = renderVariationMidi(node, o);
  if (!bytes.ok()) return bytes.error();
  Status w = wav::writeFileAtomic(path, bytes.value(), overwrite);  // refuses an existing file unless `overwrite`
  if (!w.ok()) return w.error();
  ExportReport rep;
  rep.files.push_back(path);
  if (auto dec = midi::decode(bytes.value().data(), bytes.value().size()); dec.ok()) rep.notes = dec.value().notes.size();
  return rep;
}

Result<ExportReport> ProjectSession::exportVariationWav(history::NodeId node, const std::string& path, const ExportWavOptions& o) const {
  if (!sourceData_ || !chops_) return makeError(ErrorCode::InvalidArgument, "the audio for this project is missing");
  if (!history_.contains(node)) return makeError(ErrorCode::NotFound, "there is no variation " + std::to_string(node), static_cast<std::int64_t>(node));
  auto p = nodePattern(history_, patterns_, *chops_, node, nullptr);
  if (!p.ok()) return p.error();
  auto pb = buildPlayback(p.value(), true);
  if (!pb.ok()) return pb.error();
  wav::EncodeReport enc;
  std::size_t frames = 0;
  auto bytes = midibuild::renderWavFromPlayback(pb.value(), o, renderParams(), &enc, &frames);
  if (!bytes.ok()) return bytes.error();
  Status w = wav::writeFileAtomic(path, bytes.value(), o.overwrite);
  if (!w.ok()) return w.error();
  ExportReport rep;
  rep.frames = frames;
  rep.peak = enc.peak;
  rep.clipped = enc.clipped;
  rep.files.push_back(path);
  return rep;
}

Result<std::vector<KitFile>> ProjectSession::renderKitFiles(const KitOptions& o) const {
  if (!sourceData_ || !chops_ || chops_->chops.empty()) return makeError(ErrorCode::InvalidArgument, "load a source with chops first");
  if (chops_->chops.size() + 1 > kMaxDragFiles) return makeError(ErrorCode::LimitExceeded, "Too many chops to drag (64 max). Use Export Kit.", static_cast<std::int64_t>(kMaxDragFiles - 1));
  auto mid = renderMidi(o);  // validates the options and the pattern before any audio is encoded
  if (!mid.ok()) return mid.error();
  const std::vector<std::string> names = midibuild::sliceFileNames(roles_, *chops_);
  std::vector<KitFile> out;
  for (std::size_t i = 0; i < chops_->chops.size(); ++i) {
    auto b = midibuild::encodeSlice(*sourceData_, chops_->chops[i]);
    if (!b.ok()) return b.error();
    out.push_back(KitFile{names[i], std::move(b.value())});
  }
  out.push_back(KitFile{wav::sanitizeFileName(o.name) + ".mid", std::move(mid.value())});
  return out;
}

}  // namespace chopfractal::composition
