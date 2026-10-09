// Minimal consumer: encode one second of a sine to a 24-bit WAV in memory and read it back.
#include <chopfractal/wav_export/wav.hpp>
#include <cmath>
#include <cstdio>

int main() {
  using namespace chopfractal::wav;
  std::vector<std::vector<float>> audio(1, std::vector<float>(48000));
  for (std::size_t i = 0; i < audio[0].size(); ++i) audio[0][i] = 0.5f * std::sin(0.05f * static_cast<float>(i));
  auto bytes = encode(audio, Options{});
  if (!bytes.ok()) return 1;
  auto back = decode(bytes.value().data(), bytes.value().size());
  if (!back.ok() || back.value().planar[0].size() != 48000) return 1;
  std::printf("wav: %zu bytes\n", bytes.value().size());
  return 0;
}
