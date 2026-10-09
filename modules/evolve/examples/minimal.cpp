// Minimal consumer: step every 2 loops and print the seeds a host would pass to its mutate function.
#include <chopfractal/evolve/evolve.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::evolve;
  Settings s;
  s.enabled = true;
  s.everyLoops = 2;
  Controller c;
  if (!c.start(s)) return 1;
  int steps = 0;
  for (int loop = 0; loop < 6; ++loop)
    if (auto st = c.onLoopMidpoint()) {
      std::printf("step %llu seed %llu amount %.2f\n", static_cast<unsigned long long>(st->index), static_cast<unsigned long long>(st->seed), st->amount);
      ++steps;
    }
  return steps == 3 ? 0 : 1;
}
