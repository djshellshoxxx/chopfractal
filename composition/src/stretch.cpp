// Tempo-match stretch: planning, applying and the pure helpers behind the "Tempo match" panel. The heavy DSP lives
// in the time_stretch module; this file only wires it to the session (ProjectSession members declared in
// project_session.hpp).
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace chopfractal::composition {
namespace {

std::string fmt1(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}

SourceId idOfData(const render::SourceData& d) {
  std::vector<const float*> ptrs;
  for (int c = 0; c < d.channels; ++c) ptrs.push_back(d.channel(c));
  return source::computeSourceId(ptrs.data(), d.channels, d.frames, d.sampleRate);
}

constexpr char kSuffixMarker[] = " @";
constexpr char kSuffixTail[] = " BPM";

// Strips a trailing " @<number> BPM" so repeated stretches do not pile up suffixes.
std::string stripStretchSuffix(const std::string& name) {
  if (!looksStretched(name)) return name;
  return name.substr(0, name.rfind(kSuffixMarker));
}

std::string stretchedName(const std::string& oldName, double targetBpm) {
  std::string base = stripStretchSuffix(oldName);
  const std::string suffix = kSuffixMarker + fmt1(targetBpm) + kSuffixTail;
  if (base.size() + suffix.size() > 256) base.resize(256 - suffix.size());
  return base + suffix;
}

Error tooCloseError(const Error& e, double targetBpm, std::int64_t minRegion) {
  return makeError(ErrorCode::Conflict,
                   "Stretching to " + fmt1(targetBpm) + " BPM would make " + std::to_string(e.hint) + " chops shorter than " + std::to_string(minRegion) +
                       " frames. Remove or merge tiny chops first, or use a smaller change.",
                   e.hint);
}

std::string seconds1(std::int64_t frames, int sr) { return fmt1(static_cast<double>(frames) / sr) + " s"; }

}  // namespace

std::int64_t maxStretchFrames(int sampleRate, int channels) {
  const std::int64_t byTime = kMaxStretchSeconds * sampleRate;
  const std::int64_t byBytes = static_cast<std::int64_t>(kMaxStretchBytes / (static_cast<std::size_t>(std::max(channels, 1)) * sizeof(float)));
  return std::min({byTime, source::kMaxSourceFrames, byBytes});
}

bool looksStretched(const std::string& name) {
  const std::size_t tail = sizeof(kSuffixTail) - 1;
  if (name.size() < tail + 4 || name.compare(name.size() - tail, tail, kSuffixTail) != 0) return false;
  const std::size_t at = name.rfind(kSuffixMarker);
  if (at == std::string::npos) return false;
  const std::string num = name.substr(at + 2, name.size() - tail - at - 2);
  if (num.empty()) return false;
  bool dot = false;
  for (char ch : num) {
    if (ch == '.' && !dot) dot = true;
    else if (ch < '0' || ch > '9') return false;
  }
  return true;
}

StretchPreview previewStretch(double sourceBpm, double targetBpm, std::int64_t inFrames, int sampleRate, int channels) {
  StretchPreview p;
  p.level = StretchPreview::Level::Error;
  if (!std::isfinite(sourceBpm) || !std::isfinite(targetBpm) || sourceBpm < kMinStretchBpm || sourceBpm > kMaxStretchBpm || targetBpm < kMinStretchBpm ||
      targetBpm > kMaxStretchBpm || inFrames <= 0 || sampleRate <= 0) {
    p.text = "Tempo must be between 20 and 999 BPM.";
    return p;
  }
  if (std::fabs(sourceBpm - targetBpm) < kSameTempoEpsilon) {
    p.level = StretchPreview::Level::Hidden;
    p.text = "Already at the target tempo.";
    p.ratio = 1.0;
    p.outFrames = inFrames;
    return p;
  }
  p.ratio = stretch::lengthRatio(sourceBpm, targetBpm);
  p.outFrames = std::llround(static_cast<double>(inFrames) * p.ratio);
  if (!stretch::checkRatio(p.ratio).ok()) {
    p.text = "Out of range: the tempo change must stay between 0.5x and 2x.";
    return p;
  }
  const std::int64_t limit = maxStretchFrames(sampleRate, channels);
  if (p.outFrames > limit) {
    const double lim = static_cast<double>(limit) / sampleRate;
    char buf[32];
    std::snprintf(buf, sizeof buf, lim == std::floor(lim) ? "%.0f" : "%.1f", lim);
    p.text = "The stretched audio would be " + seconds1(p.outFrames, sampleRate) + "; the maximum is " + buf + " s.";
    return p;
  }
  char speed[32];
  std::snprintf(speed, sizeof speed, "%.0f%%", targetBpm / sourceBpm * 100.0);
  char ratio[32];
  std::snprintf(ratio, sizeof ratio, "%.3f", p.ratio);
  p.text = fmt1(sourceBpm) + " -> " + fmt1(targetBpm) + " BPM: " + speed + " speed, length x" + ratio + " (" + seconds1(inFrames, sampleRate) + " -> " +
           seconds1(p.outFrames, sampleRate) + ")";
  p.canApply = true;
  p.level = StretchPreview::Level::Ok;
  if (p.ratio < 0.75 || p.ratio > 1.5) {
    p.level = StretchPreview::Level::Warning;
    p.advice = "Large change: High quality is recommended.";
  }
  return p;
}

double sourceBpmForBars(int bars, double seconds) { return seconds > 0 ? bars * 4.0 * 60.0 / seconds : 0.0; }

std::vector<LoopLengthChoice> loopLengthChoices(const std::vector<insight::LoopSuggestion>& suggestions, double seconds) {
  std::vector<LoopLengthChoice> out;
  for (std::size_t i = 0; i < suggestions.size() && i < 3; ++i) {
    LoopLengthChoice c;
    c.kind = LoopLengthChoice::Kind::Suggestion;
    c.text = suggestions[i].note;
    c.bars = suggestions[i].bars;
    c.bpm = suggestions[i].bpm;
    out.push_back(std::move(c));
  }
  for (int bars : {1, 2, 4, 8, 16}) {
    LoopLengthChoice c;
    c.kind = LoopLengthChoice::Kind::Bars;
    c.text = std::to_string(bars) + (bars == 1 ? " bar" : " bars");
    c.bars = bars;
    c.bpm = sourceBpmForBars(bars, seconds);
    out.push_back(std::move(c));
  }
  LoopLengthChoice custom;
  custom.text = "Custom BPM";
  out.push_back(std::move(custom));
  return out;
}

Result<std::shared_ptr<const render::SourceData>> runStretch(const StretchPlan& plan, const stretch::Progress& progress) {
  if (!plan.input) return makeError(ErrorCode::InvalidArgument, "the stretch plan has no input");
  const render::SourceData& in = *plan.input;
  if (in.frames != plan.plan.inFrames || in.channels < 1 || in.channels > 2)
    return makeError(ErrorCode::InvalidArgument, "the stretch plan does not match its input");
  const float* ptrs[2] = {in.channel(0), in.channels > 1 ? in.channel(1) : nullptr};
  auto out = stretch::stretch(stretch::Input{ptrs, in.channels, in.frames, in.sampleRate}, plan.plan, plan.request.quality, progress);
  if (!out.ok()) return out.error();
  auto data = std::make_shared<render::SourceData>();
  data->channels = in.channels;
  data->sampleRate = in.sampleRate;
  data->frames = plan.plan.outFrames;
  data->samples = std::move(out.value());
  return std::shared_ptr<const render::SourceData>(std::move(data));
}

bool ProjectSession::sourceNeedsEmbed() const { return hasSource() && chopMap_->info().path.empty(); }

Result<StretchPlan> ProjectSession::planStretch(const StretchRequest& r) const {
  if (!chopMap_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  if (!sourceData_) return makeError(ErrorCode::NotFound, "the source audio is missing, so it cannot be stretched");
  if (!std::isfinite(r.sourceBpm) || !std::isfinite(r.targetBpm) || r.sourceBpm < kMinStretchBpm || r.sourceBpm > kMaxStretchBpm ||
      r.targetBpm < kMinStretchBpm || r.targetBpm > kMaxStretchBpm)
    return makeError(ErrorCode::OutOfRange, "Tempo must be between 20 and 999 BPM.");
  if (std::fabs(r.sourceBpm - r.targetBpm) < kSameTempoEpsilon) return makeError(ErrorCode::InvalidArgument, "Already at the target tempo.");
  const Status ratioOk = stretch::checkRatio(stretch::lengthRatio(r.sourceBpm, r.targetBpm));
  if (!ratioOk.ok()) return ratioOk.error();

  const source::SourceInfo& info = chopMap_->info();
  const std::int64_t inFrames = sourceData_->frames;
  const std::int64_t outFrames = std::llround(static_cast<double>(inFrames) * r.sourceBpm / r.targetBpm);
  const std::int64_t limit = maxStretchFrames(info.sampleRate, info.channels);
  if (outFrames > limit) {
    const double lim = static_cast<double>(limit) / info.sampleRate;
    char buf[32];
    std::snprintf(buf, sizeof buf, lim == std::floor(lim) ? "%.0f" : "%.1f", lim);
    return makeError(ErrorCode::LimitExceeded, "The stretched audio would be " + seconds1(outFrames, info.sampleRate) + "; the maximum is " + buf + " s.", limit);
  }

  std::vector<std::int64_t> anchors;
  for (const source::Marker& m : chopMap_->markers())
    if (m.enabled) anchors.push_back(m.position);
  auto plan = stretch::makePlan(inFrames, outFrames, anchors);
  if (!plan.ok()) return plan.error();

  // Dry run of the marker rescale, so collisions are reported before any audio work.
  source::SourceInfo probe = info;
  probe.frames = outFrames;
  auto mapped = source::ChopMap::rescaled(*chopMap_, probe, inFrames, outFrames);
  if (!mapped.ok()) return mapped.error().code == ErrorCode::Conflict ? tooCloseError(mapped.error(), r.targetBpm, chopMap_->minRegionFrames()) : mapped.error();

  StretchPlan sp;
  sp.plan = std::move(plan.value());
  sp.input = sourceData_;
  sp.inputId = idOfData(*sourceData_);
  sp.request = r;
  return sp;
}

Status ProjectSession::applyStretched(const StretchPlan& sp, std::shared_ptr<const render::SourceData> stretched) {
  if (!chopMap_ || !sourceData_ || !sp.input) return makeError(ErrorCode::Conflict, "The source changed while stretching; nothing was applied.");
  if (sourceData_ != sp.input && idOfData(*sourceData_) != sp.inputId)
    return makeError(ErrorCode::Conflict, "The source changed while stretching; nothing was applied.");
  const std::int64_t inFrames = sp.plan.inFrames, outFrames = sp.plan.outFrames;
  if (sourceData_->frames != inFrames || chopMap_->info().frames != inFrames)
    return makeError(ErrorCode::Conflict, "The source changed while stretching; nothing was applied.");
  if (!stretched || stretched->channels != sourceData_->channels || stretched->sampleRate != sourceData_->sampleRate || stretched->frames != outFrames ||
      stretched->samples.size() != static_cast<std::size_t>(stretched->channels) * static_cast<std::size_t>(outFrames))
    return makeError(ErrorCode::InvalidArgument, "the stretched audio does not match the plan");

  // ---- build everything in temporaries; the live session is untouched until all of it validates ----
  source::SourceInfo newInfo = chopMap_->info();
  newInfo.frames = outFrames;
  newInfo.name = stretchedName(newInfo.name, sp.request.targetBpm);
  newInfo.path.clear();
  newInfo.id = idOfData(*stretched);
  auto newMap = source::ChopMap::rescaled(*chopMap_, newInfo, inFrames, outFrames);
  if (!newMap.ok())
    return newMap.error().code == ErrorCode::Conflict ? tooCloseError(newMap.error(), sp.request.targetBpm, chopMap_->minRegionFrames()) : newMap.error();
  ChopSnapshotPtr newChops = newMap.value().snapshot();

  std::optional<pattern::Pattern> newPattern;
  std::shared_ptr<const render::Playback> newPlayback;
  if (patterns_.hasPattern()) {
    auto mapped = pattern::mapRegions(patterns_.current(), [&](SampleRange r) {
      return SampleRange{stretch::mapPosition(r.start, inFrames, outFrames), stretch::mapPosition(r.end, inFrames, outFrames)};
    });
    if (!mapped.ok()) return mapped.error();
    pattern::Pattern clean = pattern::sanitize(mapped.value(), *newChops);
    Status valid = pattern::validate(clean);
    if (!valid.ok()) return valid.error();
    auto flat = pattern::flatten(clean, *newChops);
    if (!flat.ok()) return flat.error();
    const render::BusRouting bus = busRouting();  // chop ids are preserved by the rescale
    auto pb = render::makePlayback(stretched, flat.value(), pattern::lengthTicks(clean), playbackOptions_, &bus);
    if (!pb.ok()) return pb.error();
    newPlayback = pb.value();
    newPattern = std::move(clean);
  } else {
    auto pt = render::makePassThroughPlayback(stretched);
    if (!pt.ok()) return pt.error();
    newPlayback = pt.value();
  }

  // ---- commit ----
  const std::string bpm = fmt1(sp.request.targetBpm);
  evolve_.stop();
  endMorph(true);
  stopScenesForEdit();
  sourceData_ = std::move(stretched);
  chopMap_ = std::move(newMap.value());
  chops_ = std::move(newChops);
  // Roles and rules are kept: chop ids are preserved by the rescale.
  if (newPattern) patterns_.restore(std::move(*newPattern));
  history_ = history::VariationTree(history_.config());
  recordHistory("Stretched to " + bpm + " BPM");
  pendingActivation_ = history::kNoNode;
  publish(std::move(newPlayback));
  notice(Notice::Level::Info, "Stretched to " + bpm + " BPM (" + (sp.request.quality == stretch::Quality::Draft ? "Draft" : sp.request.quality == stretch::Quality::High ? "High" : "Standard") + ").");
  return {};
}

}  // namespace chopfractal::composition
