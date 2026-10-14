// Minimal consumer: record two bars from the next bar line at 120 BPM, then take the audio.
#include <chopfractal/input_capture/capture.hpp>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::capture;
  CaptureEngine e;
  ArmSettings s;
  s.bars = 2;
  if (!e.arm(s)) return 1;
  std::vector<float> block(512, 0.25f);
  const float* in[2] = {block.data(), block.data()};
  CaptureTransport t;
  t.playing = t.positionValid = true;
  for (std::int64_t n = 0; e.status().state != CaptureState::Captured && n < 1000000; n += 512) {
    t.ppq = n * 120.0 / (60.0 * 48000.0);
    e.process(in, 2, 512, t);
  }
  auto audio = e.take();
  if (!audio) return 1;
  std::printf("captured %lld frames\n", static_cast<long long>(audio.value().frames));
  return audio.value().frames > 0 ? 0 : 1;
}
