// Minimal consumer: render one event of a ramp source offline through the same path as live playback.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  auto src = std::make_shared<render::SourceData>();
  src->channels = 1;
  src->sampleRate = 48000;
  src->frames = 24000;
  src->samples.assign(24000, 0.5f);

  FlatEvent e;
  e.id = EventId{1};
  e.chop = ChopId{1};
  e.region = {0, 24000};
  e.start = 0;
  e.duration = kTicksPerQuarter;  // one beat

  auto playback = render::makePlayback(src, {e}, kTicksPerQuarter * 4);
  if (!playback.ok()) return 1;
  render::OfflineSettings settings;
  settings.bpm = 120.0;
  auto audio = render::renderOffline(playback.value(), settings);
  if (!audio.ok() || audio.value()[0].size() < 24000) return 1;
  std::printf("rendered %zu frames\n", audio.value()[0].size());
  return 0;
}
