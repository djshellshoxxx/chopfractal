// Gesture effects, level match and the output stage through ProjectSession.
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

std::shared_ptr<const render::SourceData> loop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k) {
    const int kind = k % 4 == 0 ? 0 : (k % 4 == 2 ? 1 : 2);
    for (int i = 0; i < 5000; ++i) {
      const float t = static_cast<float>(i);
      float v = 0.f;
      if (kind == 0) v = std::sin(t * 0.0105f) * std::exp(-t / 1500.f);
      if (kind == 1) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-t / 900.f);
      if (kind == 2) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-t / 250.f) * 0.6f;
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = v * 0.8f;
    }
  }
  return d;
}

bool ready(ProjectSession& s) {
  CHECK(s.loadSource(loop(), "loop").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  pattern::Settings st;
  st.bars = 2;
  st.density = 0.7;
  st.seed = 42;
  CHECK(s.generate(st).ok());
  return s.pattern() != nullptr;
}

std::size_t blocks(const pattern::Pattern& p) {
  std::size_t n = 0;
  for (const auto& b : p.bars)
    for (const auto& bt : b.beats)
      for (const Event& e : bt.events) n += e.echo.has_value() + e.tremPan.has_value() + e.stutter.has_value() + e.roll.has_value();
  return n;
}

std::vector<std::vector<float>> audio(const ProjectSession& s, const render::OfflineSettings& o = {}) {
  auto r = s.renderOffline(o);
  CHECK(r.ok());
  return r.ok() ? r.value() : std::vector<std::vector<float>>(2);
}

}  // namespace

CHOP_TEST(fx_settings_decorate_the_pattern_undo_cleanly_and_follow_new_generations) {
  ProjectSession s;
  render::Renderer r;
  r.prepare(render::Config{});
  s.attachMailbox(&r.mailbox());
  CHECK(ready(s));
  const auto plain = audio(s);
  CHECK_EQ(blocks(*s.pattern()), 0u);

  pattern::FxPassSettings fx;
  fx.echoAmount = fx.tremPanAmount = fx.stutterAmount = fx.rollAmount = 0.7;
  CHECK(s.setFx(fx, render::PlaybackOptions{}).ok());
  CHECK(blocks(*s.pattern()) > 0);
  CHECK(s.setFx(fx, render::PlaybackOptions{}).ok());  // no change: no-op
  const std::size_t withFx = blocks(*s.pattern());

  render::OfflineSettings o;
  o.params.fxMask = render::fxbit::kAll;
  const auto fxAudio = audio(s, o);
  CHECK(fxAudio[0] != plain[0]);
  // Bypassing the effects at render time returns the plain sound's events (same trigger times, no gesture voices).
  render::OfflineSettings off = o;
  off.params.fxMask = 0;
  CHECK(audio(s, off)[0] != fxAudio[0]);

  // Options change rebuilds the playback only.
  render::PlaybackOptions opt;
  opt.echoDecay = 80;
  CHECK(s.setFx(fx, opt).ok());
  CHECK_EQ(blocks(*s.pattern()), withFx);
  CHECK_EQ(s.playbackOptions().echoDecay, 80);

  // A fresh generation keeps the effects.
  pattern::Settings st = s.pattern()->settings;
  st.seed = 77;
  CHECK(s.generate(st).ok());
  CHECK(blocks(*s.pattern()) > 0);

  // Clearing removes the pass's blocks and undo steps back.
  CHECK(s.setFx(pattern::FxPassSettings{}, opt).ok());
  CHECK_EQ(blocks(*s.pattern()), 0u);
  CHECK(s.undo());
  CHECK(blocks(*s.pattern()) > 0);
}

CHOP_TEST(fx_settings_from_params_respect_the_allow_switches) {
  host::ParamValues v = host::ParamValues::defaults();
  CHECK(!fxSettingsFromParams(v).any());
  v.set(host::kAllowStutter, 1.0);
  v.set(host::kStutterAmount, 50.0);
  v.set(host::kStutterDecayDb, -3.0);
  const pattern::FxPassSettings f = fxSettingsFromParams(v, 5);
  CHECK(f.any() && f.stutterAmount == 0.5 && f.stutterDecay == -30 && f.echoAmount == 0.0 && f.seed == 5);
}

CHOP_TEST(level_match_measures_the_pattern_and_the_export_applies_it) {
  ProjectSession s;
  CHECK(ready(s));
  host::ParamValues v = host::ParamValues::defaults();
  auto off = s.updateLevelMatch(v, 120.0);
  CHECK(off.ok() && !off.value().valid);
  CHECK(s.matchGainDb() == 0.f);

  v.set(host::kLevelMatch, 1.0);
  auto on = s.updateLevelMatch(v, 120.0);
  CHECK(on.ok());
  if (on.ok()) {
    CHECK(on.value().valid);
    CHECK(std::fabs(on.value().gainDb) <= 12.0);
    CHECK(s.matchGainDb() == static_cast<float>(on.value().gainDb));
  }
  const auto again = s.updateLevelMatch(v, 120.0);  // deterministic
  CHECK(again.ok() && on.ok() && again.value().gainDb == on.value().gainDb);
  CHECK(s.renderParams().matchGainDb == s.matchGainDb());

  host::ParamValues lim = v;
  lim.set(host::kLimiterEnable, 1.0);
  lim.set(host::kLimiterCeilingDb, -3.0);
  s.setRenderParams(host::toRenderParams(lim));
  const auto a = audio(s, [&] {
    render::OfflineSettings o;
    o.params = s.renderParams();
    return o;
  }());
  float peak = 0.f;
  for (float x : a[0]) {
    CHECK(std::isfinite(x));
    peak = std::max(peak, std::fabs(x));
  }
  CHECK(peak <= std::pow(10.f, -3.f / 20.f) * 1.0001f);
  CHECK_EQ(a[0].size(), audio(s)[0].size());  // export length is unchanged by the limiter's latency
}
