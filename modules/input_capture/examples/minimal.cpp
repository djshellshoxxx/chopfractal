// Arms a free-running capture, feeds it one second of a ramp as a host would, stops it and takes the audio.
#include <chopfractal/input_capture/capture.hpp>

#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::capture;
  CaptureEngine engine;
  ArmSettings settings;
  settings.mode = CaptureMode::Free;
  settings.channels = 1;
  settings.maxSeconds = 10.0;
  if (!engine.arm(settings).ok()) return 1;
  (void)engine.startNow();
  std::vector<float> block(512);
  const float* in[1] = {block.data()};
  CaptureTransport transport;
  for (int b = 0; b < 94; ++b) {
    for (std::size_t i = 0; i < block.size(); ++i) block[i] = static_cast<float>(b * 512 + static_cast<int>(i)) / 48128.f;
    engine.process(in, 1, 512, transport);
  }
  engine.requestStop();
  engine.process(in, 1, 512, transport);
  auto audio = engine.take();
  if (!audio.ok()) return 1;
  std::printf("captured %lld frames at %d Hz\n", static_cast<long long>(audio.value().frames), audio.value().sampleRate);
  return audio.value().frames == 94 * 512 ? 0 : 1;
}
