#include <chopfractal/plugin_ui_adapter/outputs_view.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace chopfractal::ui {

std::string outputBusName(int bus) {
  if (bus < 0 || bus > kOutputAuxBuses) return {};
  return bus == 0 ? "Main" : "Out " + std::to_string(bus + 1);
}

namespace {
bool busOn(std::uint32_t mask, int bus) { return bus == 0 || (bus >= 1 && bus <= kOutputAuxBuses && ((mask >> (bus - 1)) & 1u) != 0); }

OutputChoice makeChoice(std::uint32_t mask, int selectedBus, bool offerRoleDefault, bool roleDefaultSelected) {
  OutputChoice c;
  for (int b = 0; b <= kOutputAuxBuses; ++b) {
    const bool on = busOn(mask, b);
    c.items.push_back(outputBusName(b) + (on ? "" : kOutputOffInHostSuffix));
    c.greyed.push_back(!on);
  }
  if (offerRoleDefault) {
    c.items.emplace_back(kOutputRoleDefaultItem);
    c.greyed.push_back(false);
  }
  c.selected = roleDefaultSelected ? kOutputAuxBuses + 1 : std::clamp(selectedBus, 0, kOutputAuxBuses);
  return c;
}

std::string twoDigits(int n) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d", n);
  return buf;
}
}  // namespace

OutputsView buildOutputsView(const OutputsUiInput& in) {
  OutputsView v;
  for (const char* m : kOutputModeItems) v.modeItems.emplace_back(m);
  v.modeSelected = std::clamp(in.mode, 0, 3);
  for (int b = 1; b <= kOutputAuxBuses; ++b) v.activeAux += busOn(in.auxEnabledMask, b) ? 1 : 0;

  if (!in.hasSource) {
    v.status = kOutputLoadFirstText;
    v.emptyText = kOutputLoadFirstText;
    return v;
  }
  v.enabled = true;
  v.status = v.activeAux == 0 ? std::string(kOutputMainOnlyText) : "Outputs: main + " + std::to_string(v.activeAux) + " aux active";

  for (const OutputsRoleInput& r : in.roles) {
    OutputRoleRow row;
    row.name = "Role " + r.name;
    row.label = r.name;
    row.title = "Output for role " + r.name;
    row.choice = makeChoice(in.auxEnabledMask, r.bus, false, false);
    if (!busOn(in.auxEnabledMask, r.bus)) row.tooltip = kOutputOffInHostTooltip;
    v.roleRows.push_back(std::move(row));
  }
  for (const OutputsChopInput& c : in.chops) {
    OutputChopRow row;
    row.number = c.number;
    const std::string n = twoDigits(c.number);
    row.label = "Chop " + n + (c.role.empty() ? "" : " (" + c.role + ")");
    row.title = "Output for chop " + n;
    int roleBusOfChop = -1;
    if (!c.role.empty())
      for (const OutputsRoleInput& r : in.roles)
        if (r.name == c.role) roleBusOfChop = r.bus;
    row.roleDefaultOffered = roleBusOfChop >= 0;
    const bool useRole = c.explicitBus < 0 && row.roleDefaultOffered;
    const int effective = c.explicitBus >= 0 ? c.explicitBus : (roleBusOfChop >= 0 ? roleBusOfChop : 0);
    row.choice = makeChoice(in.auxEnabledMask, c.explicitBus >= 0 ? c.explicitBus : 0, row.roleDefaultOffered, useRole);
    if (!busOn(in.auxEnabledMask, effective)) row.tooltip = kOutputOffInHostTooltip;
    v.chopRows.push_back(std::move(row));
  }
  v.visibleRows = std::min<int>(static_cast<int>(v.chopRows.size()), kOutputVisibleRows);
  if (v.chopRows.empty()) v.emptyText = kOutputNoChopsText;
  v.autoByChopEnabled = !v.chopRows.empty();
  v.autoByRoleEnabled = !v.roleRows.empty();
  if (v.autoByChopEnabled && v.roleRows.empty() && in.mode == 1) v.emptyText = kOutputNoRolesText;
  v.resetEnabled = in.mode != 0 || std::any_of(in.chops.begin(), in.chops.end(), [](const OutputsChopInput& c) { return c.explicitBus >= 0; }) ||
                   std::any_of(in.roles.begin(), in.roles.end(), [](const OutputsRoleInput& r) { return r.bus != 0; });
  return v;
}

}  // namespace chopfractal::ui
