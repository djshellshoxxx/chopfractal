#include <chopfractal/plugin_ui_adapter/outputs_view.hpp>

#include "chop_test.hpp"

using namespace chopfractal::ui;

namespace {
OutputsUiInput ready() {
  OutputsUiInput in;
  in.hasSource = true;
  for (int i = 1; i <= 3; ++i) {
    OutputsChopInput c;
    c.number = i;
    in.chops.push_back(c);
  }
  return in;
}
}  // namespace

CHOP_TEST(outputs_status_line_follows_the_host_and_the_source) {
  OutputsUiInput in;
  OutputsView v = buildOutputsView(in);
  CHECK(!v.enabled && v.status == "Outputs: load a source first");
  in.hasSource = true;
  CHECK(buildOutputsView(in).status == "Outputs: main only (the host enabled no extra outputs)");
  in.auxEnabledMask = 0b111;
  v = buildOutputsView(in);
  CHECK(v.activeAux == 3 && v.status == "Outputs: main + 3 aux active");
}

CHOP_TEST(outputs_bus_names_and_mode_items) {
  CHECK(outputBusName(0) == "Main" && outputBusName(1) == "Out 2" && outputBusName(8) == "Out 9");
  CHECK(outputBusName(-1).empty() && outputBusName(9).empty());
  const OutputsView v = buildOutputsView(ready());
  CHECK(v.modeItems.size() == 4 && v.modeItems[0] == "Main only" && v.modeItems[3] == "Custom");
}

CHOP_TEST(outputs_items_beyond_the_host_are_suffixed_greyed_and_get_a_tooltip) {
  OutputsUiInput in = ready();
  in.auxEnabledMask = 0b1;  // only Out 2
  in.chops[1].explicitBus = 3;
  const OutputsView v = buildOutputsView(in);
  const OutputChoice& c = v.chopRows[1].choice;
  CHECK(c.items.size() == 9 && c.items[0] == "Main" && c.items[1] == "Out 2" && c.items[2] == "Out 3 (off in host)");
  CHECK(!c.greyed[0] && !c.greyed[1] && c.greyed[2]);
  CHECK(c.selected == 3);
  CHECK(v.chopRows[1].tooltip == kOutputOffInHostTooltip);
  CHECK(v.chopRows[0].tooltip.empty());
}

CHOP_TEST(outputs_chop_rows_offer_role_default_only_for_routed_roles) {
  OutputsUiInput in = ready();
  in.auxEnabledMask = 0xFF;
  in.chops[0].role = "kick";
  in.chops[1].role = "snare";
  in.chops[1].explicitBus = 0;
  in.roles = {{"kick", 2}};
  const OutputsView v = buildOutputsView(in);
  CHECK(v.chopRows[0].label == "Chop 01 (kick)" && v.chopRows[0].title == "Output for chop 01");
  CHECK(v.chopRows[0].roleDefaultOffered && v.chopRows[0].choice.items.back() == "Role default");
  CHECK(v.chopRows[0].choice.selected == 9);
  CHECK(!v.chopRows[1].roleDefaultOffered && v.chopRows[1].choice.items.size() == 9);
  CHECK(v.roleRows.size() == 1 && v.roleRows[0].title == "Output for role kick" && v.roleRows[0].choice.selected == 2);
}

CHOP_TEST(outputs_buttons_and_row_limits) {
  OutputsUiInput in = ready();
  OutputsView v = buildOutputsView(in);
  CHECK(v.autoByChopEnabled && !v.autoByRoleEnabled && !v.resetEnabled && v.visibleRows == 3);
  in.mode = 2;
  in.roles = {{"kick", 0}};
  for (int i = 4; i <= 20; ++i) {
    OutputsChopInput c;
    c.number = i;
    in.chops.push_back(c);
  }
  v = buildOutputsView(in);
  CHECK(v.autoByRoleEnabled && v.resetEnabled && v.visibleRows == 8 && v.chopRows.size() == 20);
  in.chops.clear();
  v = buildOutputsView(in);
  CHECK(!v.autoByChopEnabled && v.emptyText == kOutputNoChopsText);
}
