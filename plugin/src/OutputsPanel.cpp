#include "OutputsPanel.h"

namespace cf = chopfractal;
using namespace perform_ui;

OutputsPanel::OutputsPanel(EditorModel& m, EditorCommands& c, PerformCommands& perform) : DrawerPanel(m, c, "OutputsPanel", 10), perform_(perform) {
  styleLabel(status_, "", false);
  status_.setJustificationType(juce::Justification::topLeft);
  styleLabel(modeLabel_, "Routing");
  styleLabel(empty_, "");
  empty_.setJustificationType(juce::Justification::topLeft);
  int id = 1;
  for (const char* item : cf::ui::kOutputModeItems) mode_.addItem(item, id++);
  describe(mode_, "Output routing", "Send everything to Main, or give each role or chop its own output");
  describe(byRole_, cf::ui::kOutputAutoByRoleLabel, "Give each role its own output, in the order they first appear");
  describe(byChop_, cf::ui::kOutputAutoByChopLabel, "Spread the chops over the outputs, one each in turn");
  describe(reset_, cf::ui::kOutputResetLabel, "Send everything back to the Main output");
  describe(status_, "Outputs status", "How many extra outputs your DAW has enabled");
  add({&status_, &modeLabel_, &mode_, &empty_, &byRole_, &byChop_, &reset_});
  mode_.onChange = [this] {
    if (updating_) return;
    switch (mode_.getSelectedId() - 1) {
      case 0: perform_.routeMain(); break;
      case 1: perform_.routeByRole(); break;
      case 2: perform_.routeByChop(); break;
      default: break;  // Custom: the box shows it after a manual edit; choosing it changes nothing
    }
    tick();
  };
  byRole_.onClick = [this] { perform_.routeByRole(); tick(); };
  byChop_.onClick = [this] { perform_.routeByChop(); tick(); };
  reset_.onClick = [this] { perform_.routeMain(); tick(); };
  tick();
}

juce::ComboBox* OutputsPanel::roleCombo(const juce::String& role) {
  for (auto& r : roleRows_)
    if (r->key == role.toStdString()) return &r->combo;
  return nullptr;
}

juce::ComboBox* OutputsPanel::chopCombo(int number) {
  const int i = number - 1;
  return i >= 0 && i < static_cast<int>(chopRows_.size()) ? &chopRows_[static_cast<std::size_t>(i)]->combo : nullptr;
}

void OutputsPanel::fill(Row& r, const cf::ui::OutputChoice& choice, const std::string& tooltip) {
  const bool same = r.combo.getNumItems() == static_cast<int>(choice.items.size());
  bool equal = same;
  for (int i = 0; equal && i < r.combo.getNumItems(); ++i) equal = r.combo.getItemText(i) == juce::String::fromUTF8(choice.items[static_cast<std::size_t>(i)].c_str());
  if (!equal) {
    r.combo.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < choice.items.size(); ++i) r.combo.addItem(juce::String::fromUTF8(choice.items[i].c_str()), static_cast<int>(i) + 1);
  }
  for (std::size_t i = 0; i < choice.greyed.size(); ++i) r.combo.setItemEnabled(static_cast<int>(i) + 1, !choice.greyed[i]);
  r.combo.setSelectedId(choice.selected + 1, juce::dontSendNotification);
  r.combo.setTooltip(tooltip.empty() ? juce::String("Choose which output this plays on") : juce::String::fromUTF8(tooltip.c_str()));
}

void OutputsPanel::rebuildRows() {
  const auto& v = state_.view;
  std::string key;
  for (const auto& r : v.roleRows) key += "R" + r.name + "|";
  for (const auto& r : v.chopRows) key += "C" + r.label + "|";
  if (key == structureKey_) return;
  structureKey_ = key;
  for (auto* rows : {&roleRows_, &chopRows_})
    for (auto& r : *rows) {
      body_.removeChildComponent(&r->label);
      body_.removeChildComponent(&r->combo);
    }
  roleRows_.clear();
  chopRows_.clear();
  auto make = [this](const std::string& label, const std::string& title, const std::string& rowKey, std::function<void(int)> change) {
    auto row = std::make_unique<Row>();
    row->key = rowKey;
    styleLabel(row->label, juce::String::fromUTF8(label.c_str()));
    row->combo.setTitle(juce::String::fromUTF8(title.c_str()));
    row->combo.onChange = [this, &combo = row->combo, change] {
      if (!updating_) change(combo.getSelectedId() - 1);
    };
    body_.addAndMakeVisible(row->label);
    body_.addAndMakeVisible(row->combo);
    return row;
  };
  for (const auto& r : v.roleRows) {
    const std::string role = r.name.rfind("Role ", 0) == 0 ? r.name.substr(5) : r.name;
    auto row = make(r.label, r.title, role, [this, role](int bus) { perform_.setRoleBus(role, bus); tick(); });
    row->combo.setComponentID("role-" + juce::String(role));
    roleRows_.push_back(std::move(row));
  }
  for (std::size_t i = 0; i < v.chopRows.size(); ++i) {
    const std::uint64_t chopId = i < state_.chopIds.size() ? state_.chopIds[i] : 0;
    auto row = make(v.chopRows[i].label, v.chopRows[i].title, std::to_string(chopId), [this, chopId](int index) {
      perform_.setChopBus(chopId, index >= 9 ? -1 : index);  // item 10 is "Role default"
      tick();
    });
    row->combo.setComponentID("chop-" + juce::String(v.chopRows[i].number));
    chopRows_.push_back(std::move(row));
  }
  relayout();
}

void OutputsPanel::tick() {
  state_ = perform_.outputsState();
  const auto& v = state_.view;
  updating_ = true;
  status_.setText(juce::String::fromUTF8(v.status.c_str()), juce::dontSendNotification);
  mode_.setSelectedId(v.modeSelected + 1, juce::dontSendNotification);
  mode_.setEnabled(v.enabled);
  empty_.setText(juce::String::fromUTF8(v.emptyText.c_str()), juce::dontSendNotification);
  rebuildRows();
  for (std::size_t i = 0; i < v.roleRows.size() && i < roleRows_.size(); ++i) fill(*roleRows_[i], v.roleRows[i].choice, v.roleRows[i].tooltip);
  for (std::size_t i = 0; i < v.chopRows.size() && i < chopRows_.size(); ++i) fill(*chopRows_[i], v.chopRows[i].choice, v.chopRows[i].tooltip);
  byRole_.setEnabled(v.autoByRoleEnabled);
  byChop_.setEnabled(v.autoByChopEnabled);
  reset_.setEnabled(v.resetEnabled);
  updating_ = false;
}

int OutputsPanel::layoutBody(int width) {
  beginLayout();
  Stack s(width);
  const int lw = Stack::scaled(110);
  status_.setBounds(s.row(Stack::scaled(34)));
  labelled(s.row(), modeLabel_, mode_, Stack::scaled(70));
  empty_.setBounds(s.row(empty_.getText().isEmpty() ? Stack::scaled(2) : Stack::scaled(34)));
  if (!roleRows_.empty()) heading(s, "Roles");
  for (auto& r : roleRows_) labelled(s.row(), r->label, r->combo, lw);
  if (!chopRows_.empty()) heading(s, "Chops");
  for (auto& r : chopRows_) labelled(s.row(), r->label, r->combo, lw);
  grid(s, {&byRole_, &byChop_, &reset_}, 2);
  body_.repaint();
  return s.height();
}
