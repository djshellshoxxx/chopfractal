// End-to-end in a dozen lines: load audio, find chops, generate, render, save, reload.
#include <chopfractal/composition/project_session.hpp>
#include <cmath>
#include <cstdio>

int main() {
  using namespace chopfractal;
  auto audio = std::make_shared<render::SourceData>();
  audio->channels = 1;
  audio->sampleRate = 48000;
  audio->frames = 48000;
  audio->samples.assign(48000, 0.f);
  for (int hit = 0; hit < 4; ++hit)  // four decaying clicks, one per quarter second
    for (int i = 0; i < 3000; ++i) audio->samples[static_cast<std::size_t>(hit * 12000 + i)] = std::exp(-i / 400.f) * std::sin(i * 0.3f);

  composition::ProjectSession session;
  if (!session.loadSource(audio, "demo loop").ok()) return 1;
  composition::DetectOptions detect;
  if (!session.applyChops(detect).ok()) return 1;

  pattern::Settings settings;
  settings.bars = 2;
  settings.seed = 7;
  if (!session.generate(settings).ok()) return 1;

  render::OfflineSettings out;
  auto rendered = session.renderOffline(out);
  auto saved = session.saveState(/*embedSource=*/true);
  if (!rendered.ok() || !saved.ok()) return 1;

  composition::ProjectSession reopened;
  if (!reopened.loadState(saved.value().data(), saved.value().size(), nullptr).ok()) return 1;
  auto again = reopened.renderOffline(out);
  if (!again.ok() || again.value() != rendered.value()) return 1;  // save/reload restores identical audio
  std::printf("%zu chops, %zu events, %zu frames rendered identically after reload\n", session.chops()->chops.size(),
              pattern::eventCount(*session.pattern()), rendered.value()[0].size());
  return 0;
}
