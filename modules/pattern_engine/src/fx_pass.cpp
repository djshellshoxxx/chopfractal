#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/pattern_engine/fx_pass.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::pattern {
namespace {

constexpr Ticks kMinStutterTicks = 480;  // a repeat needs room: at least a 1/8 note
constexpr std::uint64_t kSaltEcho = 0xE01, kSaltTrem = 0xE02, kSaltStutter = 0xE03, kSaltRoll = 0xE04;

double unit(std::uint64_t key, std::uint64_t id, std::uint64_t salt, std::uint64_t slot = 0) {
  return static_cast<double>(deriveKey(key, id, salt, slot) >> 11) * (1.0 / 9007199254740992.0);
}

double clamp01(double v) { return std::isfinite(v) ? std::min(1.0, std::max(0.0, v)) : 0.0; }

bool echoForced(const EchoFx& e) { return e.level != 255 && e.level >= 128; }
bool tremForced(const TremPanFx& t) { return t.mode == 254 || (t.depth != 255 && t.depth >= 128); }

std::uint8_t pickIndex(double u, int count) { return static_cast<std::uint8_t>(std::min(count - 1, static_cast<int>(u * count))); }

void decorate(Event& e, const FxPassSettings& s, std::uint64_t key) {
  const std::uint64_t id = e.id.value;
  // Clear what an earlier pass wrote.
  if (e.echo && !echoForced(*e.echo)) e.echo.reset();
  if (e.tremPan && !tremForced(*e.tremPan)) e.tremPan.reset();
  if (e.locked || e.userOwned) return;
  e.stutter.reset();
  e.roll.reset();

  const bool longHit = e.duration >= kMinStutterTicks;
  bool gesture = false;
  if (longHit && unit(key, id, kSaltStutter) < clamp01(s.stutterAmount)) {
    StutterFx f;
    const int base = std::min(kStutterDivisions - 1, std::max(0, s.stutterRate));
    f.rate = static_cast<std::uint8_t>(std::min(kStutterDivisions - 1, base + (unit(key, id, kSaltStutter, 1) < 0.3 ? 1 : 0)));
    f.rateEnd = unit(key, id, kSaltStutter, 2) < 0.35 ? static_cast<std::uint8_t>(std::min<int>(f.rate + 2, kStutterDivisions - 1)) : f.rate;
    f.gate = static_cast<std::uint8_t>(std::min(100, std::max(10, s.stutterGate + static_cast<int>(unit(key, id, kSaltStutter, 3) * 11.0) - 5)));
    f.tail = static_cast<std::uint8_t>(10 + unit(key, id, kSaltStutter, 4) * 40.0);
    f.decay = static_cast<std::int8_t>(std::min(60, std::max(-120, s.stutterDecay)));
    f.pitch = static_cast<std::int8_t>(std::min(120, std::max(-120, s.stutterPitch)));
    f.probability = static_cast<std::uint8_t>(100 - unit(key, id, kSaltStutter, 8) * 25.0);
    f.pan = static_cast<std::uint8_t>(unit(key, id, kSaltStutter, 9) * 50.0);
    e.stutter = sanitize(f);
    gesture = true;
  }
  if (!gesture && longHit && unit(key, id, kSaltRoll) < clamp01(s.rollAmount)) {
    RollFx f;
    f.rate = static_cast<std::uint8_t>(std::min(kStutterDivisions - 1, std::max(0, s.rollRate)));
    f.rateEnd = static_cast<std::uint8_t>(std::min(kStutterDivisions - 1, std::max<int>(f.rate, s.rollRateEnd)));
    f.velStart = static_cast<std::uint8_t>(30 + unit(key, id, kSaltRoll, 3) * 30.0);
    f.velEnd = static_cast<std::uint8_t>(85 + unit(key, id, kSaltRoll, 4) * 15.0);
    f.freeze = static_cast<std::uint8_t>(std::min(100, std::max(0, s.rollFreeze)));
    f.pitch = 0;
    f.flags = static_cast<std::uint8_t>((unit(key, id, kSaltRoll, 6) < 0.3 ? kRollPalindrome : 0) | (unit(key, id, kSaltRoll, 7) < 0.6 ? kRollSmooth : 0));
    e.roll = sanitize(f);
    gesture = true;
  }
  if (!e.echo && !gesture && unit(key, id, kSaltEcho) < clamp01(s.echoAmount)) {
    EchoFx f;
    f.repeats = static_cast<std::uint8_t>(1 + static_cast<int>(unit(key, id, kSaltEcho, 1) * 3.0));
    f.rateIndex = 255;
    f.level = 255;
    e.echo = sanitize(f);
  }
  if (!e.tremPan && unit(key, id, kSaltTrem) < clamp01(s.tremPanAmount)) {
    TremPanFx f;
    f.mode = pickIndex(unit(key, id, kSaltTrem, 1), 3);
    f.rateIndex = pickIndex(unit(key, id, kSaltTrem, 2), 4);
    f.depth = static_cast<std::uint8_t>(30 + unit(key, id, kSaltTrem, 3) * 50.0);
    e.tremPan = sanitize(f);
  }
}

}  // namespace

Pattern applyFxPass(const Pattern& pattern, const FxPassSettings& settings) {
  Pattern out = pattern;
  const std::uint64_t key = deriveKey(settings.seed, pattern.settings.seed, 0xF0C5ull);
  for (Bar& bar : out.bars)
    for (Beat& beat : bar.beats)
      for (Event& e : beat.events) decorate(e, settings, key);
  return out;
}

}  // namespace chopfractal::pattern
