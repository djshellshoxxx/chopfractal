// Minimal consumer: compose two module payloads into a project blob and read it back.
#include <chopfractal/state_codec/codec.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::codec;
  ProjectState state;
  state.modules["my_module"] = {1, {1, 2, 3}};
  auto blob = encode(state);
  if (!blob.ok()) return 1;
  auto back = decode(blob.value().data(), blob.value().size());
  if (!back.ok() || back.value().modules.at("my_module").bytes.size() != 3) return 1;
  std::printf("project state: %zu bytes\n", blob.value().size());
  return 0;
}
