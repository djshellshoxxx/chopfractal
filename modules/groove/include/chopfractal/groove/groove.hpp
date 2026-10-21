#pragma once
// Groove Lab: playback-time swing, per-role push/pull, seeded humanize and an accent curve (docs/specs/roadmap/F17).
//
// A groove never changes stored events. `grooveShift` answers, for one event, how many ticks it moves and what
// its velocity is scaled by; the composition root applies it once when it builds the playback snapshot (and the
// MIDI export applies the same function), never per audio block.
//
// Threading: pure, allocation-free, noexcept, thread-safe. Deterministic: integer arithmetic plus exact double
// operations and std::lround, so results are identical across compilers.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace chopfractal::groove {

constexpr std::uint32_t kModuleVersion = 1;
constexpr int kAccentCells = 16;
constexpr int kRoleClasses = 5;

enum class Template : std::uint8_t { Straight, Mpc54, Mpc58, Mpc62, Mpc66, Mpc71, Shuffle, Dilla, Custom, kCount };
enum class SwingGrid : std::uint8_t { Sixteenth, Eighth };
// The five groups a push/pull value applies to.
enum class RoleClass : std::uint8_t { Kick = 0, Snare, Hat, Perc, Other };
RoleClass classOf(DrumRole role);  // Kick; Snare+Ghost; Hat+OpenHat; Cymbal+Perc; Unknown/Other -> Other

struct Groove {
  bool enabled = false;
  Template tmpl = Template::Straight;
  float swingPercent = 50.f;            // 50..75 in steps of 0.5: share of a step pair taken by the first note
  SwingGrid grid = SwingGrid::Sixteenth;
  std::int8_t pushPull[kRoleClasses] = {0, 0, 0, 0, 0};  // kick, snare, hat, perc, other; ticks, -40..40
  std::uint8_t humanizeTime = 0;        // 0..30 ticks, maximum deviation
  std::uint8_t humanizeVel = 0;         // 0..25 percent
  std::uint32_t seed = 0x0F176001u;
  std::uint8_t accent[kAccentCells] = {50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50};  // 0..100 -> x0.5..x1.5
  bool bakeIntoMidi = true;
  friend bool operator==(const Groove& a, const Groove& b);
  friend bool operator!=(const Groove& a, const Groove& b) { return !(a == b); }
};

struct Shift {
  std::int32_t ticks = 0;
  float velScale = 1.f;
};

// True when the groove would not move or scale anything (Playback skips the stage entirely).
bool isNeutral(const Groove& g) noexcept;
// Clamps every field into its range and rounds swing to 0.5. An out-of-range template becomes Custom.
Groove sanitized(Groove g) noexcept;
// Fills swing / push-pull / humanize / accent of a named template, keeping enabled, seed and bakeIntoMidi.
Groove makeTemplate(Template t);
const char* templateName(Template t) noexcept;

// Delay of an off-beat in ticks for the swing definition of F17 section 4.3: (swing/100 - 0.5) * 2 * gridTicks, rounded.
std::int32_t swingDelayTicks(float swingPercent, Ticks gridTicks) noexcept;

// Shift of one event. `tickInLoop` is the event start inside the loop, `ticksPerBar` / `stepsPerBar` the pattern's
// own step grid (4/4 sixteenths: 3840 / 16). `loopIndex` selects the humanize stream (only its value modulo 8
// matters, giving a stable 8-bar feel). The total shift is limited to less than half a groove step so events never
// swap order.
Shift grooveShift(const Groove& g, RoleClass role, std::int64_t tickInLoop, std::int64_t eventIndex, int ticksPerBar, int stepsPerBar,
                  std::int64_t loopIndex = 0) noexcept;

// Applies the groove to a flattened pattern. `roles` returns the role class of a chop. Positions stay >= 0 and
// inside [0, loopTicks); a velocity scale multiplies tx.level (clamped to the level range). The list stays sorted
// order by start (a stable sort, so ties keep their order). Message thread only: the sort may allocate.
template <typename RoleOf>
void apply(const Groove& g, FlatEventList& events, Ticks loopTicks, int ticksPerBar, int stepsPerBar, RoleOf roleOf, std::int64_t loopIndex = 0);

// 46-byte wire form (F17 section 4.4): u8 version=1, u8 flags (bit0 enabled, bit1 bakeIntoMidi), u8 template, u8 grid,
// u16 swingTenths, i8 pushPull[5], u8 humanizeTime, u8 humanizeVel, u32 seed, u8 accent[16], 5 zero bytes.
constexpr std::size_t kSerializedBytes = 46;
std::vector<std::uint8_t> serialize(const Groove& g);
// Unknown version, wrong size or garbage returns the default groove with ok = false. Values are clamped.
struct ParseResult {
  Groove groove;
  bool ok = false;
};
ParseResult parse(const std::uint8_t* data, std::size_t size) noexcept;

// ---- template implementation detail (header-only part) ----
template <typename RoleOf>
void apply(const Groove& g, FlatEventList& events, Ticks loopTicks, int ticksPerBar, int stepsPerBar, RoleOf roleOf, std::int64_t loopIndex) {
  if (isNeutral(g) || loopTicks <= 0) return;
  std::int64_t index = 0;
  for (FlatEvent& e : events) {
    const Shift s = grooveShift(g, roleOf(e.chop), e.start, index++, ticksPerBar, stepsPerBar, loopIndex);
    Ticks t = e.start + s.ticks;
    if (t < 0) t = 0;
    if (t >= loopTicks) t = loopTicks - 1;
    e.start = t;
    float lv = e.tx.level * s.velScale;
    e.tx.level = lv > limits::kMaxLevel ? limits::kMaxLevel : (lv < 0.f ? 0.f : lv);
  }
  std::stable_sort(events.begin(), events.end(), [](const FlatEvent& a, const FlatEvent& b) { return a.start < b.start; });
}

}  // namespace chopfractal::groove
