// probabilitySeedFor / probabilityPasses: the shared draw behind flatten's probability test (used by the A/B morph).
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

#include "chop_test.hpp"

using namespace chopfractal;

CHOP_TEST(probability_helpers_match_flatten_behavior) {
  ChopSnapshot chops;
  chops.source = SourceId{1};
  chops.sourceFrames = 4000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 1000}, 0, 0, 0});
  const std::uint64_t patternSeed = 9;
  CHECK(probabilitySeedFor(patternSeed) == hashCombine(patternSeed, 0x50524F42ull));
  for (float p : {0.3f, 0.7f, 1.0f}) {
    std::vector<Event> events;
    for (std::uint64_t id = 1; id <= 1000; ++id) {
      Event e;
      e.id = EventId{id};
      e.chop = ChopId{1};
      e.start = static_cast<Ticks>((id % 8) * 400);
      e.duration = 200;
      e.probability = p;
      events.push_back(e);
    }
    FlatEventList out;
    CHECK(flattenInto(out, events, 0, 3840, {}, chops, FlattenLimits{2000, 3}, probabilitySeedFor(patternSeed)).ok());
    std::size_t expected = 0;
    for (const Event& e : events) expected += probabilityPasses(probabilitySeedFor(patternSeed), e.id, p) ? 1 : 0;
    CHECK(out.size() == expected);
    if (p >= 1.f) CHECK(out.size() == 1000);
    else CHECK(out.size() > 0 && out.size() < 1000);
  }
}

CHOP_TEST(probability_passes_is_monotone_in_the_probability) {
  const std::uint64_t seed = probabilitySeedFor(5);
  for (std::uint64_t id = 1; id <= 500; ++id) {
    bool seen = false;
    for (int k = 0; k <= 20; ++k) {
      const bool pass = probabilityPasses(seed, EventId{id}, static_cast<float>(k) / 20.f);
      CHECK(!seen || pass);  // once an event passes, a higher probability keeps it
      seen = seen || pass;
    }
    CHECK(seen);
  }
}
