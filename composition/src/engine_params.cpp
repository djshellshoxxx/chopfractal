// Host parameters -> engine v2 settings and Groove Lab (F17) values.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::composition {
namespace {
int choiceIndex(const host::ParamValues& v, host::ParamIndex i, int maxIndex) {
  const double x = v.get(i);
  const long k = std::lround(std::isfinite(x) ? x : 0.0);
  return static_cast<int>(std::min<long>(maxIndex, std::max<long>(0, k)));
}
}  // namespace

void applyBreakParams(const host::ParamValues& v, pattern::Settings& s, bool setEngineVersion) {
  if (setEngineVersion) s.engineVersion = choiceIndex(v, host::kEngineVersion, 1) >= 1 ? pattern::kEngineVersionBreak : pattern::kEngineVersionLegacy;
  s.brk.profile = static_cast<breakeng::Profile>(choiceIndex(v, host::kBreakProfile, breakeng::kProfileCount - 1));
  s.brk.backbeat = static_cast<breakeng::Backbeat>(choiceIndex(v, host::kBreakBackbeat, 1));
  s.brk.anchors = static_cast<breakeng::AnchorMode>(choiceIndex(v, host::kBreakAnchors, 2));
  auto unit = [&](host::ParamIndex i) { return std::min(1.0, std::max(0.0, std::isfinite(v.get(i)) ? v.get(i) : 0.5)); };
  s.brk.groove = unit(host::kBreakGroove);
  s.brk.humanize = unit(host::kBreakHumanize);
  s.brk.fill = unit(host::kBreakFill);
  s.brk.glitch = unit(host::kBreakGlitch);
  if (s.engineVersion >= pattern::kEngineVersionBreak) {
    // The Glitch macro must be audible on its own: the planner only places stutters, reversed swells and tape stops
    // while the matching allowance is on. Glitch raises those allowances like Chaos does and never lowers them.
    if (s.brk.glitch >= 0.2) s.allowRetrigger = true;
    if (s.brk.glitch >= 0.3) s.allowReverse = true;
    if (s.brk.glitch >= 0.4) s.allowGlide = true;
  }
}

groove::Groove grooveFromParams(const host::ParamValues& v, const groove::Groove& base) {
  groove::Groove g = base;
  g.enabled = v.get(host::kGrooveEnable) >= 0.5;
  const int t = choiceIndex(v, host::kGrooveTemplate, static_cast<int>(groove::Template::Custom));
  const auto tmpl = static_cast<groove::Template>(t);
  if (tmpl != base.tmpl && tmpl != groove::Template::Custom) {
    // Picking a preset loads its push/pull, accents and grid; the automatable values below then override it.
    const groove::Groove preset = groove::makeTemplate(tmpl);
    g.grid = preset.grid;
    for (int i = 0; i < groove::kRoleClasses; ++i) g.pushPull[i] = preset.pushPull[i];
    for (int i = 0; i < groove::kAccentCells; ++i) g.accent[i] = preset.accent[i];
  }
  g.tmpl = tmpl;
  g.swingPercent = static_cast<float>(v.get(host::kGrooveSwing));
  g.humanizeTime = static_cast<std::uint8_t>(std::lround(std::max(0.0, std::min(30.0, v.get(host::kGrooveHumTime)))));
  g.humanizeVel = static_cast<std::uint8_t>(std::lround(std::max(0.0, std::min(25.0, v.get(host::kGrooveHumVel)))));
  return groove::sanitized(g);
}

}  // namespace chopfractal::composition
