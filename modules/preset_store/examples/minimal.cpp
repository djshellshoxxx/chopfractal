// Minimal consumer: save the first factory preset to bytes and read it back.
#include <chopfractal/preset_store/presets.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::presets;
  const Preset& p = factoryPresets().front();
  auto bytes = serialize(p);
  auto back = deserialize(bytes.data(), bytes.size());
  if (!back.ok() || back.value().name != p.name) return 1;
  std::printf("%s -> %s (%zu bytes)\n", p.name.c_str(), fileNameFor(p.name).c_str(), bytes.size());
  return 0;
}
