// Minimal consumer: turn raw host playhead data and automation values into renderer inputs.
#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::host;
  HostTimeInfo hostTime;  // a host that reports nothing at all
  hostTime.isPlaying = true;
  const TimeTranslation t = translateHostTime(hostTime, /*manualBpm=*/128.0);
  if (!t.usedFallbackTempo || t.block.bpm != 128.0) return 1;

  ParamValues values = ParamValues::defaults();
  values.set(kDryMix, 0.25);
  const auto render = toRenderParams(values);
  std::printf("%zu parameters, dry=%.2f, tempo fallback=%d\n", parameterManifest().size(), static_cast<double>(render.dryMix), t.usedFallbackTempo);
  return 0;
}
