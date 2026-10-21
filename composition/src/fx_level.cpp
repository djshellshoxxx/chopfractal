// Gesture-effect settings and the level-match measurement of ProjectSession (docs/specs/roadmap/E01, E02, F16).
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::composition {

Status ProjectSession::setFx(const pattern::FxPassSettings& pass, const render::PlaybackOptions& options) {
  const bool passChanged = !(pass == fx_);
  const bool optionsChanged = options.echoRate != playbackOptions_.echoRate || options.echoDecay != playbackOptions_.echoDecay ||
                              options.echoTone != playbackOptions_.echoTone || options.tremMode != playbackOptions_.tremMode ||
                              options.tremRate != playbackOptions_.tremRate || options.tremDepth != playbackOptions_.tremDepth;
  if (!passChanged && !optionsChanged) return {};
  const pattern::FxPassSettings oldPass = fx_;
  const render::PlaybackOptions oldOptions = playbackOptions_;
  fx_ = pass;
  playbackOptions_ = options;
  Status s;
  if (!patterns_.hasPattern() || !chops_) {
    s = republish();
  } else if (passChanged) {
    s = finalize(pattern::applyFxPass(patterns_.current(), fx_), nullptr);  // one undo entry
  } else {
    s = installPlayback(patterns_.current());
  }
  if (!s.ok()) {  // all or nothing
    fx_ = oldPass;
    playbackOptions_ = oldOptions;
    republish();
  }
  return s;
}

render::RenderParams ProjectSession::renderParams() const {
  render::RenderParams p = renderParams_;
  p.matchGainDb = matchGainDb_.load(std::memory_order_relaxed);
  return p;
}

Result<levelmatch::MatchResult> ProjectSession::updateLevelMatch(const host::ParamValues& values, double bpm) {
  levelmatch::MatchResult none;
  if (values.get(host::kLevelMatch) < 0.5 || !sourceData_ || !playback_ || playback_->passThrough || !playback_->source || !patterns_.hasPattern()) {
    matchGainDb_.store(0.f, std::memory_order_relaxed);
    return none;
  }
  const render::SourceData& src = *sourceData_;
  const float* planes[2] = {src.channel(0), src.channel(src.channels > 1 ? 1 : 0)};
  const levelmatch::Loudness loop = levelmatch::measure(planes, 2, src.frames, src.sampleRate);

  render::OfflineSettings rs;
  rs.sampleRate = static_cast<double>(src.sampleRate);
  rs.bpm = std::round(std::isfinite(bpm) && bpm >= 20.0 && bpm <= 999.0 ? bpm : 120.0);
  rs.blockSize = 512;
  rs.cycles = 1;
  rs.tailSeconds = 0.0;
  rs.params = renderParams_;  // polish and gesture mask as in playback; no gain, no limiter, no dry
  rs.params.enabled = true;
  rs.params.dryMix = 0.f;
  rs.params.outputGainDb = 0.f;
  rs.params.matchGainDb = 0.f;
  rs.params.limiterEnabled = false;
  auto audio = render::renderOffline(playback_, rs);
  if (!audio.ok()) return audio.error();
  const auto& ch = audio.value();
  const float* pp[2] = {ch[0].data(), ch[ch.size() > 1 ? 1 : 0].data()};
  const levelmatch::Loudness pat = levelmatch::measure(pp, 2, static_cast<std::int64_t>(ch[0].size()), src.sampleRate);

  const bool limiterOn = values.get(host::kLimiterEnable) >= 0.5;
  const levelmatch::MatchResult m = levelmatch::computeMatch(loop, pat, values.get(host::kMatchTrimDb), limiterOn, values.get(host::kLimiterCeilingDb));
  matchGainDb_.store(m.valid ? static_cast<float>(m.gainDb) : 0.f, std::memory_order_relaxed);
  return m;
}

}  // namespace chopfractal::composition
