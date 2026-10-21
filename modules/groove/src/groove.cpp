// Groove Lab core: templates, swing / push-pull / humanize / accent shift, and the 46-byte state form.
// Sources of the template numbers: docs/specs/roadmap/F17-groove-lab.md section 4.2 and 12 (classic MPC swing range
// 50 to 75 percent; "Dilla" is an offered starting point, not a claim of copying a record).
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/groove/groove.hpp>

#include <cmath>
#include <cstring>

namespace chopfractal::groove {

namespace {
constexpr std::int8_t kPushMax = 40;
constexpr std::uint8_t kHumTimeMax = 30;
constexpr std::uint8_t kHumVelMax = 25;

template <typename T>
T clampT(T v, T lo, T hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

Ticks gridTicksOf(const Groove& g, int ticksPerBar, int stepsPerBar) noexcept {
  const Ticks base = stepsPerBar > 0 ? static_cast<Ticks>(ticksPerBar / stepsPerBar) : 240;
  return g.grid == SwingGrid::Eighth ? base * 2 : base;
}

// Accent pattern of the Dilla template: 100,45,70,45, 110,45,70,50, ... scaled to the 0..100 cell range (110 caps at 100).
constexpr std::uint8_t kDillaAccent[kAccentCells] = {91, 41, 64, 41, 100, 41, 64, 45, 91, 41, 64, 41, 100, 45, 64, 50};
}  // namespace

RoleClass classOf(DrumRole role) {
  switch (role) {
    case DrumRole::Kick: return RoleClass::Kick;
    case DrumRole::Snare:
    case DrumRole::Ghost: return RoleClass::Snare;
    case DrumRole::Hat:
    case DrumRole::OpenHat: return RoleClass::Hat;
    case DrumRole::Cymbal:
    case DrumRole::Perc: return RoleClass::Perc;
    default: return RoleClass::Other;
  }
}

bool operator==(const Groove& a, const Groove& b) {
  return a.enabled == b.enabled && a.tmpl == b.tmpl && a.swingPercent == b.swingPercent && a.grid == b.grid &&
         std::memcmp(a.pushPull, b.pushPull, sizeof a.pushPull) == 0 && a.humanizeTime == b.humanizeTime && a.humanizeVel == b.humanizeVel &&
         a.seed == b.seed && std::memcmp(a.accent, b.accent, sizeof a.accent) == 0 && a.bakeIntoMidi == b.bakeIntoMidi;
}

bool isNeutral(const Groove& g) noexcept {
  if (!g.enabled) return true;
  if (g.swingPercent != 50.f) return false;
  for (std::int8_t p : g.pushPull)
    if (p != 0) return false;
  if (g.humanizeTime != 0 || g.humanizeVel != 0) return false;
  for (std::uint8_t a : g.accent)
    if (a != 50) return false;
  return true;
}

Groove sanitized(Groove g) noexcept {
  if (static_cast<int>(g.tmpl) >= static_cast<int>(Template::kCount)) g.tmpl = Template::Custom;
  if (static_cast<int>(g.grid) > 1) g.grid = SwingGrid::Sixteenth;
  float sw = g.swingPercent;
  if (!(sw == sw)) sw = 50.f;
  sw = clampT(sw, 50.f, 75.f);
  g.swingPercent = std::round(sw * 10.f) / 10.f;  // the wire form holds tenths, so the triplet 66.7 survives; the UI steps by 0.5
  for (std::int8_t& p : g.pushPull) p = clampT<std::int8_t>(p, -kPushMax, kPushMax);
  g.humanizeTime = clampT<std::uint8_t>(g.humanizeTime, 0, kHumTimeMax);
  g.humanizeVel = clampT<std::uint8_t>(g.humanizeVel, 0, kHumVelMax);
  for (std::uint8_t& a : g.accent) a = clampT<std::uint8_t>(a, 0, 100);
  return g;
}

const char* templateName(Template t) noexcept {
  static const char* const names[] = {"Straight", "MPC 54", "MPC 58", "MPC 62", "MPC 66", "MPC 71", "Shuffle", "Dilla", "Custom"};
  const int i = static_cast<int>(t);
  return i >= 0 && i < static_cast<int>(sizeof names / sizeof names[0]) ? names[i] : "Custom";
}

Groove makeTemplate(Template t) {
  Groove g;
  g.tmpl = t;
  switch (t) {
    case Template::Mpc54: g.swingPercent = 54.f; break;
    case Template::Mpc58: g.swingPercent = 58.f; break;
    case Template::Mpc62: g.swingPercent = 62.f; break;
    case Template::Mpc66: g.swingPercent = 66.7f; break;  // the triplet: 80 ticks on a 16th pair
    case Template::Mpc71: g.swingPercent = 71.f; break;
    case Template::Shuffle:
      g.swingPercent = 66.7f;
      g.grid = SwingGrid::Eighth;
      break;
    case Template::Dilla: {
      g.swingPercent = 58.f;
      const std::int8_t pp[kRoleClasses] = {6, 14, -6, 4, 0};
      for (int i = 0; i < kRoleClasses; ++i) g.pushPull[i] = pp[i];
      g.humanizeTime = 6;
      g.humanizeVel = 6;
      for (int i = 0; i < kAccentCells; ++i) g.accent[i] = kDillaAccent[i];
      break;
    }
    default: break;
  }
  return g;
}

std::int32_t swingDelayTicks(float swingPercent, Ticks gridTicks) noexcept {
  const double sw = clampT(static_cast<double>(swingPercent), 50.0, 75.0);
  return static_cast<std::int32_t>(std::lround((sw / 100.0 - 0.5) * 2.0 * static_cast<double>(gridTicks)));
}

Shift grooveShift(const Groove& g, RoleClass role, std::int64_t tickInLoop, std::int64_t eventIndex, int ticksPerBar, int stepsPerBar, std::int64_t loopIndex) noexcept {
  Shift out;
  if (isNeutral(g) || ticksPerBar <= 0 || stepsPerBar <= 0) return out;
  const Ticks gridTicks = gridTicksOf(g, ticksPerBar, stepsPerBar);
  if (gridTicks <= 0) return out;
  const std::int64_t baseStep = ticksPerBar / stepsPerBar;
  double total = 0.0;

  // (2) swing: events near an odd groove step move by the delay; off-grid events by a distance-scaled share.
  const std::int32_t delay = swingDelayTicks(g.swingPercent, gridTicks);
  if (delay != 0) {
    const std::int64_t step = (tickInLoop + gridTicks / 2) / gridTicks;  // nearest step line
    const std::int64_t dist = tickInLoop >= step * gridTicks ? tickInLoop - step * gridTicks : step * gridTicks - tickInLoop;
    if ((step % 2) == 1) {
      const std::int64_t plateau = gridTicks / 8, edge = gridTicks / 4;
      double scale = 1.0;
      if (dist > edge) scale = 0.0;
      else if (dist > plateau) scale = 1.0 - static_cast<double>(dist - plateau) / static_cast<double>(edge - plateau);
      total += static_cast<double>(delay) * scale;
    }
  }
  // (3) push / pull of the role class.
  total += static_cast<double>(g.pushPull[static_cast<int>(role)]);

  // (4) humanize: a local stream that depends only on (seed, eventIndex, loopIndex mod 8).
  const std::uint64_t key = mix64(static_cast<std::uint64_t>(g.seed) ^ (static_cast<std::uint64_t>(eventIndex) * 0x9E3779B97F4A7C15ull) ^
                                  static_cast<std::uint64_t>(((loopIndex % 8) + 8) % 8));
  const std::uint64_t k2 = mix64(key ^ 0xD1B54A32D192ED03ull);
  const double u1 = static_cast<double>(key >> 11) * (1.0 / 9007199254740992.0);
  const double u2 = static_cast<double>(k2 >> 11) * (1.0 / 9007199254740992.0);
  if (g.humanizeTime > 0) total += std::round((u1 * 2.0 - 1.0) * static_cast<double>(g.humanizeTime));

  // Total shift stays below half a groove step so two events on neighbouring steps never swap order.
  const double bound = static_cast<double>(gridTicks / 2 - 1);
  total = clampT(total, -bound, bound);
  out.ticks = static_cast<std::int32_t>(std::lround(total));

  // (5) accent cell of the nearest 16th of the bar, then humanize velocity.
  double vel = 1.0;
  const std::int64_t barPos = ((tickInLoop % ticksPerBar) + ticksPerBar) % ticksPerBar;
  const std::int64_t cell = ((barPos * kAccentCells + ticksPerBar / 2) / ticksPerBar) % kAccentCells;
  vel *= 0.5 + static_cast<double>(g.accent[cell]) / 100.0;
  (void)baseStep;
  if (g.humanizeVel > 0) vel *= 1.0 + (u2 * 2.0 - 1.0) * static_cast<double>(g.humanizeVel) / 100.0;
  out.velScale = static_cast<float>(vel);
  return out;
}

std::vector<std::uint8_t> serialize(const Groove& in) {
  const Groove g = sanitized(in);
  std::vector<std::uint8_t> b(kSerializedBytes, 0);
  b[0] = 1;
  b[1] = static_cast<std::uint8_t>((g.enabled ? 1 : 0) | (g.bakeIntoMidi ? 2 : 0));
  b[2] = static_cast<std::uint8_t>(g.tmpl);
  b[3] = static_cast<std::uint8_t>(g.grid);
  const unsigned tenths = static_cast<unsigned>(std::lround(static_cast<double>(g.swingPercent) * 10.0));
  b[4] = static_cast<std::uint8_t>(tenths & 0xFF);
  b[5] = static_cast<std::uint8_t>(tenths >> 8);
  for (int i = 0; i < kRoleClasses; ++i) b[6 + i] = static_cast<std::uint8_t>(g.pushPull[i]);
  b[11] = g.humanizeTime;
  b[12] = g.humanizeVel;
  for (int i = 0; i < 4; ++i) b[13 + i] = static_cast<std::uint8_t>((g.seed >> (8 * i)) & 0xFF);
  for (int i = 0; i < kAccentCells; ++i) b[17 + i] = g.accent[i];
  return b;  // bytes 33..45 stay zero (reserved)
}

ParseResult parse(const std::uint8_t* d, std::size_t n) noexcept {
  ParseResult r;
  if (d == nullptr || n != kSerializedBytes || d[0] != 1) return r;
  Groove g;
  g.enabled = (d[1] & 1) != 0;
  g.bakeIntoMidi = (d[1] & 2) != 0;
  g.tmpl = d[2] < static_cast<std::uint8_t>(Template::kCount) ? static_cast<Template>(d[2]) : Template::Custom;
  g.grid = d[3] == 1 ? SwingGrid::Eighth : SwingGrid::Sixteenth;
  g.swingPercent = static_cast<float>(d[4] | (d[5] << 8)) / 10.f;
  for (int i = 0; i < kRoleClasses; ++i) g.pushPull[i] = static_cast<std::int8_t>(d[6 + i]);
  g.humanizeTime = d[11];
  g.humanizeVel = d[12];
  g.seed = static_cast<std::uint32_t>(d[13]) | (static_cast<std::uint32_t>(d[14]) << 8) | (static_cast<std::uint32_t>(d[15]) << 16) | (static_cast<std::uint32_t>(d[16]) << 24);
  for (int i = 0; i < kAccentCells; ++i) g.accent[i] = d[17 + i];
  r.groove = sanitized(g);
  r.ok = true;
  return r;
}

}  // namespace chopfractal::groove
