// Minimal consumer: swing a row of sixteenth hats with the MPC 62 template and print the shifted ticks.
#include <chopfractal/groove/groove.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::groove;
  Groove g = makeTemplate(Template::Mpc62);
  g.enabled = true;
  int moved = 0;
  for (int step = 0; step < 16; ++step) {
    const Shift s = grooveShift(g, RoleClass::Hat, step * 240, step, 3840, 16);
    std::printf("%d:%+d ", step, s.ticks);
    moved += s.ticks != 0;
  }
  std::printf("\n");
  return moved == 8 ? 0 : 1;  // every odd step moves
}
