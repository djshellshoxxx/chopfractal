// Minimal consumer: stretch one second of a 440 Hz sine to 1.25 seconds and report the exact length.
#include <chopfractal/time_stretch/stretch.hpp>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::stretch;
  std::vector<float> x(48000);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(i) / 48000.0));
  const float* ch[1] = {x.data()};
  Input in;
  in.channels = ch;
  in.numChannels = 1;
  in.frames = static_cast<std::int64_t>(x.size());
  in.sampleRate = 48000;
  auto plan = makePlan(in.frames, 60000, {});
  if (!plan.ok()) return 1;
  auto out = chopfractal::stretch::stretch(in, plan.value(), Quality::Draft);
  if (!out.ok()) return 1;
  std::printf("%zu frames\n", out.value().size());
  return out.value().size() == 60000 ? 0 : 1;
}
