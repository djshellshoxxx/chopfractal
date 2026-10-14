// Minimal consumer: three scenes of two loops each; print which scene plays after each loop boundary.
#include <chopfractal/scenes/scenes.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::scenes;
  SceneList list;
  for (const char* name : {"A", "B", "C"}) {
    Scene s;
    s.name = name;
    s.loops = 2;
    if (!add(list, s).ok()) return 1;
  }
  Sequencer q;
  q.start(list, list.scenes.front().id);
  int switches = 0;
  for (int loop = 0; loop < 6; ++loop) {
    const Advance a = q.onBoundary(list);
    switches += a.switched ? 1 : 0;
    std::printf("boundary %d: playing %u%s\n", loop + 1, a.playing, a.switched ? " (switched)" : "");
  }
  return switches == 3 ? 0 : 1;
}
