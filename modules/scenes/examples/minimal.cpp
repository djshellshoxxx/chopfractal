// Minimal consumer: three scenes played A (2 loops), B (1 loop), C (3 loops, then stop).
#include <chopfractal/scenes/scenes.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::scenes;
  SceneList list;
  list.wrap = false;
  const int loops[3] = {2, 1, 3};
  for (int i = 0; i < 3; ++i) {
    Scene s;
    s.name = std::string("Scene ") + char('1' + i);
    s.loops = loops[i];
    s.next = i == 2 ? Next::Stop : Next::Advance;
    s.pattern = {1, 2, 3};
    if (!add(list, s)) return 1;
  }
  Sequencer q;
  q.start(list, list.scenes[0].id);
  int switches = 0;
  for (int boundary = 1; boundary <= 6; ++boundary) {
    const Advance a = q.onBoundary(list);
    if (a.arm) std::printf("boundary %d: arm scene %u\n", boundary, a.arm);
    if (a.switched) {
      std::printf("boundary %d: now playing scene %u\n", boundary, a.playing);
      ++switches;
    }
    if (a.finished) std::printf("boundary %d: finished\n", boundary);
  }
  return switches == 2 && !q.running() ? 0 : 1;
}
