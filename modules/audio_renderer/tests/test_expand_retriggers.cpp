// expandRetriggers: the same sub-hits makePlayback prepares, available without a decoded source (MIDI export).
#include <chopfractal/audio_renderer/renderer.hpp>

#include <algorithm>
#include <cstdint>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::render;

namespace {
FlatEvent ev(std::uint64_t id, Ticks start, Ticks dur, int retrigger, float level = 1.f) {
  FlatEvent e;
  e.id = EventId{id};
  e.chop = ChopId{id % 3 + 1};
  e.region = {1000, 3000};
  e.start = start;
  e.duration = dur;
  e.tx.retrigger = retrigger;
  e.tx.level = level;
  return e;
}
std::shared_ptr<SourceData> src() {
  auto s = std::make_shared<SourceData>();
  s->channels = 1;
  s->sampleRate = 48000;
  s->frames = 8000;
  s->samples.assign(8000, 0.5f);
  return s;
}
}  // namespace

CHOP_TEST(expandRetriggers_matches_makePlayback_events) {
  const FlatEventList in = {ev(1, 0, 960, 1), ev(2, 960, 960, 3, 0.9f), ev(3, 1920, 961, 8), ev(4, 3840 - 480, 960, 4, 1.2f)};
  const Ticks length = 3840;
  const FlatEventList ex = expandRetriggers(in, length);
  auto pb = makePlayback(src(), in, length);
  CHECK(pb.ok());
  CHECK(ex.size() == pb.value()->events.size());
  for (std::size_t i = 0; i < std::min(ex.size(), pb.value()->events.size()); ++i) {
    const FlatEvent& a = ex[i];
    const FlatEvent& b = pb.value()->events[i];
    CHECK(a.id == b.id && a.chop == b.chop && a.start == b.start && a.duration == b.duration && a.tx.level == b.tx.level && a.tx.retrigger == 1);
  }
}

CHOP_TEST(expandRetriggers_levels_decay_and_durations_sum_to_the_original) {
  const FlatEventList ex = expandRetriggers({ev(7, 100, 1000, 8, 1.f)}, 0);
  CHECK(ex.size() == 8);
  Ticks total = 0;
  float level = 1.f;
  for (std::size_t k = 0; k < ex.size(); ++k) {
    CHECK(ex[k].start == 100 + 1000 * static_cast<Ticks>(k) / 8);
    CHECK(ex[k].tx.level == level);
    level *= 0.8f;
    total += ex[k].duration;
  }
  CHECK(total == 1000);
  const FlatEventList three = expandRetriggers({ev(8, 0, 960, 3)}, 0);
  CHECK(three.size() == 3 && three[1].start == 320 && three[2].start == 640);
}

CHOP_TEST(expandRetriggers_wraps_only_with_a_loop_length) {
  const FlatEventList e = {ev(9, 3600, 960, 2)};
  CHECK(expandRetriggers(e, 3840)[0].start == 240);   // the second sub-hit (4080) wraps; sorted first
  CHECK(expandRetriggers(e, 0).back().start == 4080);
}
