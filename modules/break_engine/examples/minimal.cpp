// Minimal consumer: plan one 4-bar phrase for the Jungle profile and print the hits of the last bar.
#include <chopfractal/break_engine/break_engine.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::breakeng;
  PlanSettings s;
  s.options.profile = Profile::Jungle;
  s.seed = 7;
  const auto plans = planPattern(s);
  if (plans.size() != 4) return 1;
  int snareAnchors = 0;
  for (const Hit& h : plans[3].hits) {
    std::printf("step %2d tick %4lld role %d vel %3d%s\n", h.step, static_cast<long long>(h.start), static_cast<int>(h.role), h.velocity, h.anchor ? " anchor" : "");
    if (h.anchor && h.role == chopfractal::DrumRole::Snare) ++snareAnchors;
  }
  return snareAnchors >= 1 ? 0 : 1;
}
