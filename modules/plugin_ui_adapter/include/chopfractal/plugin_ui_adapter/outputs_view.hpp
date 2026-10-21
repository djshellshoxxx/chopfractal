#pragma once
// View model for the Outputs section (F11): the status line, the routing mode box, the role and chop route tables and the
// button labels. Pure functions of plain data, so the whole section is testable without a GUI. The editor reads an
// OutputsView each timer tick; the handlers call the commands listed in docs/specs/roadmap/F11-multi-output-stems.md
// section 12.
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::ui {

constexpr int kOutputAuxBuses = 8;   // "Out 2" .. "Out 9"; bus 0 is "Main"
constexpr int kOutputVisibleRows = 8;

struct OutputsChopInput {
  int number = 0;               // 1-based position in the chop snapshot
  std::string role;             // empty = none
  int explicitBus = -1;         // the chop's own override, -1 when it has none
};
struct OutputsRoleInput {
  std::string name;
  int bus = 0;                  // 0..8
};

struct OutputsUiInput {
  bool hasSource = false;
  int mode = 0;                       // 0 main only, 1 by role, 2 by chop, 3 custom
  std::uint32_t auxEnabledMask = 0;   // bit i set: "Out i + 2" is enabled in the host
  std::vector<OutputsRoleInput> roles;  // only roles that exist (and may be routed), in display order
  std::vector<OutputsChopInput> chops;  // snapshot order
};

struct OutputChoice {
  std::vector<std::string> items;     // "Main", "Out 2" .. "Out 9" (+ " (off in host)"), then "Role default" when offered
  std::vector<bool> greyed;           // same size as items
  int selected = 0;
};

struct OutputRoleRow {
  std::string name;                   // component name: "Role kick"
  std::string label;                  // painted: the role name
  std::string title;                  // accessibility title: "Output for role kick"
  OutputChoice choice;
  std::string tooltip;                // set when the selected output is off in the host
};
struct OutputChopRow {
  int number = 0;
  std::string label;                  // "Chop 03" or "Chop 03 (kick)"
  std::string title;                  // "Output for chop 03"
  OutputChoice choice;
  bool roleDefaultOffered = false;
  std::string tooltip;
};

struct OutputsView {
  bool enabled = false;               // needs a source
  int activeAux = 0;                  // enabled aux outputs
  std::string status;
  std::vector<std::string> modeItems; // "Main only", "By role", "By chop", "Custom"
  int modeSelected = 0;
  std::vector<OutputRoleRow> roleRows;
  std::vector<OutputChopRow> chopRows;
  int visibleRows = 0;                // min(chop rows, kOutputVisibleRows)
  std::string emptyText;              // shown instead of the tables when there is nothing to route
  bool autoByRoleEnabled = false;     // needs a role to route
  bool autoByChopEnabled = false;
  bool resetEnabled = false;          // routing is not already Main only
};

OutputsView buildOutputsView(const OutputsUiInput& in);

// "Main", "Out 2" .. "Out 9"; empty for a bus outside 0..8.
std::string outputBusName(int bus);
constexpr const char* kOutputModeItems[4] = {"Main only", "By role", "By chop", "Custom"};
constexpr const char* kOutputAutoByRoleLabel = "Auto: by role";
constexpr const char* kOutputAutoByChopLabel = "Auto: by chop";
constexpr const char* kOutputResetLabel = "Reset to Main";
constexpr const char* kOutputRoleDefaultItem = "Role default";
constexpr const char* kOutputOffInHostSuffix = " (off in host)";
constexpr const char* kOutputOffInHostTooltip = "This output is not enabled in your DAW; the sound plays on the main output until you enable it.";
constexpr const char* kOutputLoadFirstText = "Outputs: load a source first";
constexpr const char* kOutputMainOnlyText = "Outputs: main only (the host enabled no extra outputs)";
constexpr const char* kOutputNoRolesText = "No chops have a role yet; use By chop or set roles first.";
constexpr const char* kOutputNoChopsText = "Detect chops to route them.";

}  // namespace chopfractal::ui
