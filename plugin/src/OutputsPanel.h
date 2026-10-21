#pragma once
// Outputs drawer (F11 section 12): the routing mode, one output choice per role and per chop, and the three auto-route
// buttons. Every item, grey-out, tooltip and enable flag comes from ui::buildOutputsView().
#include <chopfractal/plugin_ui_adapter/outputs_view.hpp>

#include "DrawerPanel.h"
#include "PerformCommands.h"

class OutputsPanel : public DrawerPanel {
 public:
  OutputsPanel(EditorModel&, EditorCommands&, PerformCommands&);
  const chopfractal::ui::OutputsView& view() const { return state_.view; }
  juce::ComboBox* roleCombo(const juce::String& role);
  juce::ComboBox* chopCombo(int number);  // 1-based chop number

 private:
  struct Row {
    juce::Label label;
    juce::ComboBox combo;
    std::string key;  // role name, or the chop id as text
  };
  int layoutBody(int width) override;
  void tick() override;
  void rebuildRows();
  void fill(Row&, const chopfractal::ui::OutputChoice&, const std::string& tooltip);

  PerformCommands& perform_;
  PerformCommands::OutputsState state_;
  std::string structureKey_;
  bool updating_ = false;
  std::vector<std::unique_ptr<Row>> roleRows_, chopRows_;
  juce::Label status_, modeLabel_, empty_;
  juce::ComboBox mode_;
  juce::TextButton byRole_{chopfractal::ui::kOutputAutoByRoleLabel}, byChop_{chopfractal::ui::kOutputAutoByChopLabel}, reset_{chopfractal::ui::kOutputResetLabel};
};
