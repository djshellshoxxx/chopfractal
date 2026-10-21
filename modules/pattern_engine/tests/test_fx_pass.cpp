#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/pattern_engine/fx_pass.hpp>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::pattern;

namespace {

ChopSnapshot chopsFor() {
  ChopSnapshot s;
  s.source = SourceId{42};
  s.sourceFrames = 96000;
  s.sampleRate = 48000;
  for (int i = 0; i < 8; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 12000ll, (i + 1) * 12000ll}, 1u, 0, 0});
  return s;
}

Pattern make(std::uint64_t seed = 3) {
  Settings s;
  s.bars = 4;
  s.density = 0.8;
  s.seed = seed;
  auto r = generate(chopsFor(), s);
  CHECK(r.ok());
  return r.ok() ? r.value() : Pattern{};
}

std::vector<std::uint8_t> bytesOf(const Pattern& p) { return serialize(p); }

template <class F>
void forEvents(const Pattern& p, F f) {
  for (const Bar& b : p.bars)
    for (const Beat& bt : b.beats)
      for (const Event& e : bt.events) f(e);
}

}  // namespace

CHOP_TEST(an_all_zero_pass_leaves_the_pattern_byte_identical) {
  const Pattern p = make();
  CHECK(bytesOf(applyFxPass(p, FxPassSettings{})) == bytesOf(p));
}

CHOP_TEST(the_pass_only_adds_effect_blocks_and_is_deterministic) {
  const Pattern p = make();
  FxPassSettings s;
  s.echoAmount = s.tremPanAmount = s.stutterAmount = s.rollAmount = 0.6;
  s.seed = 9;
  const Pattern a = applyFxPass(p, s);
  const Pattern b = applyFxPass(p, s);
  CHECK(bytesOf(a) == bytesOf(b));
  std::size_t echo = 0, trem = 0, stut = 0, roll = 0, events = 0;
  forEvents(a, [&](const Event& e) {
    ++events;
    echo += e.echo.has_value();
    trem += e.tremPan.has_value();
    stut += e.stutter.has_value();
    roll += e.roll.has_value();
    CHECK(!(e.stutter && e.roll));
    if (e.stutter || e.roll) CHECK(e.duration >= 480);
    CHECK(validateEvent(e).ok());
  });
  CHECK(events == eventCount(p));
  CHECK(echo > 0 && trem > 0 && stut + roll > 0);
  // Timing and chops are untouched: flattening agrees on everything but the blocks.
  auto fa = flatten(a, chopsFor());
  auto fp = flatten(p, chopsFor());
  CHECK(fa.ok() && fp.ok());
  if (fa.ok() && fp.ok()) {
    CHECK(fa.value().size() == fp.value().size());
    for (std::size_t i = 0; i < fa.value().size() && i < fp.value().size(); ++i) {
      CHECK(fa.value()[i].start == fp.value()[i].start);
      CHECK(fa.value()[i].chop == fp.value()[i].chop);
      CHECK(fa.value()[i].tx == fp.value()[i].tx);
    }
  }
  FxPassSettings other = s;
  other.seed = 10;
  CHECK(bytesOf(applyFxPass(p, other)) != bytesOf(a));
}

CHOP_TEST(rerunning_replaces_the_pass_blocks_and_zero_clears_them_but_forced_hits_stay) {
  Pattern p = make();
  // Force an Echo on the first event and own another one.
  bool forced = false, owned = false;
  for (Bar& b : p.bars)
    for (Beat& bt : b.beats)
      for (Event& e : bt.events) {
        if (!forced) {
          EchoFx f;
          f.repeats = 4;
          f.level = 128 + 40;
          e.echo = f;
          forced = true;
        } else if (!owned) {
          e.userOwned = true;
          owned = true;
        }
      }
  FxPassSettings s;
  s.echoAmount = s.stutterAmount = 1.0;
  const Pattern a = applyFxPass(p, s);
  const Pattern again = applyFxPass(a, s);
  CHECK(bytesOf(again) == bytesOf(a));  // idempotent
  s = FxPassSettings{};
  const Pattern cleared = applyFxPass(a, s);
  std::size_t left = 0;
  forEvents(cleared, [&](const Event& e) { left += e.echo.has_value() + e.stutter.has_value() + e.roll.has_value() + e.tremPan.has_value(); });
  CHECK(left == 1);  // only the forced echo
  bool seenForced = false;
  forEvents(cleared, [&](const Event& e) {
    if (e.echo) seenForced = e.echo->repeats == 4 && e.echo->level == 168;
  });
  CHECK(seenForced);
}
