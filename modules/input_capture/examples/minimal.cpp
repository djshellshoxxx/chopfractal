// Records two bars of a test tone at 120 BPM (4/4, 48 kHz), starting on a bar line, and prints what was captured.
#include <chopfractal/input_capture/capture.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::capture;
  Capture cap;
  Options o;
  o.channels = 1;
  o.sampleRate = 48000.0;
  o.targetFrames = framesForBars(2, 4.0, 120.0, 48000.0);  // 192000 frames
  o.waitForBar = true;
  if (!cap.arm(o).ok()) return 1;

  std::vector<float> block(512);
  double ppq = 3.9;  // the host is just before a bar line
  for (int b = 0; b < 400 && cap.state() != State::Done; ++b) {
    for (int i = 0; i < 512; ++i) block[static_cast<std::size_t>(i)] = 0.5f * std::sin(0.05f * static_cast<float>(b * 512 + i));
    const float* in[1] = {block.data()};
    Transport t;
    t.playing = true;
    t.positionValid = true;
    t.ppq = ppq;
    cap.process(in, 1, 512, t);
    ppq += 512.0 * 120.0 / (60.0 * 48000.0);
  }
  auto r = cap.take();
  if (!r.ok()) return 1;
  std::printf("captured %lld frames (%d channel, %d Hz)\n", static_cast<long long>(r.value().frames), r.value().channels, r.value().sampleRate);
  return r.value().frames == o.targetFrames ? 0 : 1;
}
