// Tempo matching: plan on the message thread, stretch on a worker, apply atomically
// (docs/specs/roadmap/F14-tempo-match-stretch.md).
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::composition {
namespace {

std::string bpmText(double bpm) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", bpm);
  return buf;
}

const char* qualityName(stretch::Quality q) {
  switch (q) {
    case stretch::Quality::Draft: return "Draft";
    case stretch::Quality::High: return "High";
    case stretch::Quality::Standard: break;
  }
  return "Standard";
}

}  // namespace

Result<StretchPlan> ProjectSession::planStretch(const StretchRequest& r) const {
  if (!chopMap_ || !chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  if (!sourceData_) return makeError(ErrorCode::InvalidArgument, "the source audio is missing; relink the file first");
  if (!(r.sourceBpm >= 20.0 && r.sourceBpm <= 999.0 && r.targetBpm >= 20.0 && r.targetBpm <= 999.0))
    return makeError(ErrorCode::OutOfRange, "tempos must be between 20 and 999 BPM");
  if (std::fabs(r.sourceBpm - r.targetBpm) < 0.05) return makeError(ErrorCode::InvalidArgument, "Already at the target tempo.");
  const double ratio = stretch::lengthRatio(r.sourceBpm, r.targetBpm);
  if (Status s = stretch::checkRatio(ratio); !s.ok()) return s.error();
  const std::int64_t in = sourceData_->frames;
  const std::int64_t out = std::llround(static_cast<double>(in) * ratio);
  const std::int64_t maxFrames = std::min<std::int64_t>({static_cast<std::int64_t>(60.0 * sourceData_->sampleRate), source::kMaxSourceFrames,
                                                         static_cast<std::int64_t>(100ll * 1024 * 1024 / (static_cast<std::int64_t>(sourceData_->channels) * 4))});
  if (out > maxFrames) {
    char msg[120];
    std::snprintf(msg, sizeof msg, "The stretched audio would be %.1f s; the maximum is 60 s.", static_cast<double>(out) / sourceData_->sampleRate);
    return makeError(ErrorCode::LimitExceeded, msg, maxFrames);
  }
  std::vector<std::int64_t> anchors;
  for (const source::Marker& m : chopMap_->markers())
    if (m.enabled) anchors.push_back(m.position);
  auto plan = stretch::makePlan(in, out, anchors);
  if (!plan.ok()) return plan.error();
  StretchPlan sp;
  sp.plan = std::move(plan.value());
  sp.input = sourceData_;
  sp.inputId = sourceIdOf(*sourceData_);
  sp.request = r;
  return sp;
}

Status ProjectSession::applyStretched(const StretchPlan& sp, std::shared_ptr<const render::SourceData> stretched) {
  if (!sourceData_ || !chopMap_ || !chops_ || sourceIdOf(*sourceData_) != sp.inputId)
    return makeError(ErrorCode::Conflict, "The source changed while stretching; nothing was applied.");
  if (!stretched || stretched->channels != sourceData_->channels || stretched->sampleRate != sourceData_->sampleRate || stretched->frames != sp.plan.outFrames ||
      stretched->samples.size() != static_cast<std::size_t>(stretched->channels) * static_cast<std::size_t>(stretched->frames))
    return makeError(ErrorCode::InvalidArgument, "the stretched audio does not match the plan");
  const std::int64_t in = sp.plan.inFrames, out = sp.plan.outFrames;

  // ---- build everything in temporaries; the live session is untouched until all of it validates ----
  source::SourceInfo info = chopMap_->info();
  info.id = sourceIdOf(*stretched);
  info.frames = out;
  info.path.clear();  // a stretched source has no file
  info.name = chopMap_->info().name.substr(0, 200) + " @" + bpmText(sp.request.targetBpm) + " BPM";
  auto map = source::ChopMap::rescaled(*chopMap_, info, in, out);
  if (!map.ok()) {
    if (map.error().code == ErrorCode::Conflict)
      return makeError(ErrorCode::Conflict,
                       "Stretching to " + bpmText(sp.request.targetBpm) + " BPM would make " + std::to_string(map.error().hint) +
                           " chops shorter than 64 frames. Remove or merge tiny chops first, or use a smaller change.",
                       map.error().hint);
    return map.error();
  }
  ChopSnapshotPtr newChops = map.value().snapshot();
  std::optional<pattern::Pattern> newPattern;
  std::shared_ptr<const render::Playback> newPlayback;
  if (patterns_.hasPattern()) {
    pattern::Pattern mapped = pattern::mapRegions(patterns_.current(), [&](SampleRange r) {
      return SampleRange{stretch::mapPosition(r.start, in, out), stretch::mapPosition(r.end, in, out)};
    });
    mapped = pattern::sanitize(mapped, *newChops);
    if (Status v = pattern::validate(mapped); !v.ok()) return v.error();
    auto flat = pattern::flatten(mapped, *newChops);
    if (!flat.ok()) return flat.error();
    auto pb = render::makePlayback(stretched, flat.value(), pattern::lengthTicks(mapped));
    if (!pb.ok()) return pb.error();
    newPlayback = pb.value();
    newPattern = std::move(mapped);
  } else {
    auto pt = render::makePassThroughPlayback(stretched);
    if (!pt.ok()) return pt.error();
    newPlayback = pt.value();
  }

  // ---- commit ----
  stopScenesForEdit(false, false);
  for (scenes::Scene& sc : sceneList_.scenes) {  // scenes keep their rhythm too: scale their sub-regions with the source
    auto p = pattern::deserialize(sc.pattern.data(), sc.pattern.size());
    if (!p.ok()) continue;
    pattern::Pattern mapped = pattern::mapRegions(p.value(), [&](SampleRange r) {
      return SampleRange{stretch::mapPosition(r.start, in, out), stretch::mapPosition(r.end, in, out)};
    });
    sc.pattern = pattern::serialize(pattern::sanitize(mapped, *newChops));
  }
  recordingScenes_ = false;
  endMorph(true);
  evolve_.stop();
  const bool hadPattern = newPattern.has_value();
  sourceData_ = std::move(stretched);
  chopMap_ = std::move(map.value());
  chops_ = std::move(newChops);
  history_ = history::VariationTree(history_.config());
  pendingActivation_ = history::kNoNode;
  patterns_.reset();
  if (newPattern) patterns_.restore(std::move(*newPattern));
  publish(newPlayback);
  if (hadPattern) recordHistory("Stretched to " + bpmText(sp.request.targetBpm) + " BPM");
  notice(Notice::Level::Info, "Stretched to " + bpmText(sp.request.targetBpm) + " BPM (" + qualityName(sp.request.quality) + ").");
  return {};
}

}  // namespace chopfractal::composition
