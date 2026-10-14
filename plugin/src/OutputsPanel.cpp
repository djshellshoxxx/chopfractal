#include "OutputsPanel.h"

#include <algorithm>
#include <initializer_list>

#include "PluginProcessor.h"

namespace cf = chopfractal;
using cf::composition::OutputRouting;
using cf::composition::RoutingMode;

OutputsPanel::OutputsPanel(ChopFractalProcessor& processor) : proc_(processor) {
  setName("OutputsPanel");
  setWantsKeyboardFocus(true);
  mode_.setTitle("Output routing");
  mode_.addItemList({"Main only", "By role", "By chop", "Custom"}, 1);
  chopBus_.setTitle("Output for the selected chop");
  roleBus_.setTitle("Output for the selected role");
  chopList_.setTitle("Route table");
  roleList_.setTitle("Role table");
  chopModel_.count = [this] { return static_cast<int>(chopRows_.size()); };
  chopModel_.text = [this](int r) { return r < static_cast<int>(chopRows_.size()) ? chopRows_[static_cast<std::size_t>(r)] : juce::String(); };
  chopModel_.onSelect = [this](int) { refresh(); };
  roleModel_.count = [this] { return static_cast<int>(roleRows_.size()); };
  roleModel_.text = [this](int r) { return r < static_cast<int>(roleRows_.size()) ? roleRows_[static_cast<std::size_t>(r)] : juce::String(); };
  roleModel_.onSelect = [this](int) { refresh(); };
  chopList_.setModel(&chopModel_);
  roleList_.setModel(&roleModel_);
  chopCaption_.setText("Chops", juce::dontSendNotification);
  roleCaption_.setText("Roles", juce::dontSendNotification);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&chopList_, &roleList_, &mode_, &chopBus_, &roleBus_, &autoRole_, &autoChop_, &reset_, &close_, &status_, &chopCaption_, &roleCaption_}) addAndMakeVisible(*c);
  for (juce::Button* b : {&autoRole_, &autoChop_, &reset_, &close_}) b->setTitle(b->getButtonText());
  mode_.onChange = [this] {
    if (loading_) return;
    switch (mode_.getSelectedId()) {
      case 1: reset_.triggerClick(); break;
      case 2: autoRole_.triggerClick(); break;
      case 3: autoChop_.triggerClick(); break;
      default: break;
    }
  };
  autoRole_.onClick = [this] {
    proc_.withSession([](cf::composition::ProjectSession& s) { s.autoRouteByRole(); });
    report();
  };
  autoChop_.onClick = [this] {
    proc_.withSession([](cf::composition::ProjectSession& s) { s.autoRouteByChop(); });
    report();
  };
  reset_.onClick = [this] {
    apply([](OutputRouting& r, const cf::composition::ProjectSession&) { r = OutputRouting{}; });
  };
  close_.onClick = [this] { close(); };
  chopBus_.onChange = [this] {
    if (loading_) return;
    const int row = chopList_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(chopIds_.size())) return;
    const std::uint64_t chop = chopIds_[static_cast<std::size_t>(row)];
    const int id = chopBus_.getSelectedId();  // 1 = Main ... 9 = Out 9, 10 = role default
    apply([=](OutputRouting& r, const cf::composition::ProjectSession&) {
      if (id == 10) r.chopBus.erase(chop);
      else r.chopBus[chop] = static_cast<std::uint8_t>(id - 1);
      r.mode = RoutingMode::Custom;
    });
  };
  roleBus_.onChange = [this] {
    if (loading_) return;
    const int row = roleList_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(roleNames_.size())) return;
    const std::string role = roleNames_[static_cast<std::size_t>(row)];
    const int id = roleBus_.getSelectedId();
    apply([=](OutputRouting& r, const cf::composition::ProjectSession&) {
      if (id == 1) r.roleBus.erase(role);
      else r.roleBus[role] = static_cast<std::uint8_t>(id - 1);
      r.mode = RoutingMode::Custom;
    });
  };
  fillBusItems(chopBus_, true);
  fillBusItems(roleBus_, false);
  setVisible(false);
}

void OutputsPanel::fillBusItems(juce::ComboBox& box, bool withRoleDefault) {
  loading_ = true;
  box.clear(juce::dontSendNotification);
  box.addItem("Main", 1);
  for (int b = 1; b <= cf::render::kMaxAuxBuses; ++b) box.addItem("Out " + juce::String(b + 1) + (b > aux_ ? " (off in host)" : ""), b + 1);
  if (withRoleDefault) box.addItem("Role default", 10);
  loading_ = false;
}

void OutputsPanel::open() {
  refresh();
  setVisible(true);
  toFront(true);
  grabKeyboardFocus();
  startTimerHz(5);
}

void OutputsPanel::close() {
  setVisible(false);
  stopTimer();
}

bool OutputsPanel::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    close();
    return true;
  }
  return false;
}

void OutputsPanel::apply(const std::function<void(OutputRouting&, const cf::composition::ProjectSession&)>& edit) {
  cf::Status st;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    OutputRouting r = s.outputRouting();
    edit(r, s);
    st = s.setOutputRouting(r);
  });
  report(st.ok() ? juce::String() : juce::String(st.error().message));
}

void OutputsPanel::report(const juce::String& fallback) {
  juce::String note;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& n : s.takeNotices()) note = juce::String(n.text);
  });
  if (fallback.isNotEmpty()) proc_.setStatusMessage(fallback);
  else if (note.isNotEmpty()) proc_.setStatusMessage(note);
  refresh();
}

void OutputsPanel::refresh() {
  const int aux = proc_.activeAuxBuses();
  if (aux != aux_) {
    aux_ = aux;
    fillBusItems(chopBus_, true);
    fillBusItems(roleBus_, false);
  }
  OutputRouting routing;
  bool haveSource = false;
  std::vector<juce::String> chopRows, roleRows;
  std::vector<std::uint64_t> chopIds;
  std::vector<std::string> roleNames;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    routing = s.outputRouting();
    haveSource = s.chops() != nullptr;
    if (!s.chops()) return;
    int n = 0;
    for (const auto& c : s.chops()->chops) {
      ++n;
      const std::string role = s.roleMap().roleOf(c.id);
      const int bus = s.effectiveBus(c.id);
      chopRows.push_back("Chop " + juce::String(n).paddedLeft('0', 2) + (role.empty() ? juce::String() : " (" + juce::String(role) + ")") + "   > " + (bus == 0 ? juce::String("Main") : "Out " + juce::String(bus + 1)) +
                         (bus > aux_ ? " (off in host)" : ""));
      chopIds.push_back(c.id.value);
      if (!role.empty() && std::find(roleNames.begin(), roleNames.end(), role) == roleNames.end()) roleNames.push_back(role);
    }
    for (const auto& r : roleNames) {
      const auto it = routing.roleBus.find(r);
      const int bus = it == routing.roleBus.end() ? 0 : it->second;
      roleRows.push_back(juce::String(r) + "   > " + (bus == 0 ? juce::String("Main") : "Out " + juce::String(bus + 1)));
    }
  });
  const bool changed = chopRows != chopRows_ || roleRows != roleRows_;
  chopRows_ = std::move(chopRows);
  roleRows_ = std::move(roleRows);
  chopIds_ = std::move(chopIds);
  roleNames_ = std::move(roleNames);
  if (changed) {
    chopList_.updateContent();
    roleList_.updateContent();
    chopList_.repaint();
    roleList_.repaint();
  }
  loading_ = true;
  mode_.setSelectedId(static_cast<int>(routing.mode) + 1, juce::dontSendNotification);
  const int crow = chopList_.getSelectedRow();
  if (crow >= 0 && crow < static_cast<int>(chopIds_.size())) {
    const auto it = routing.chopBus.find(chopIds_[static_cast<std::size_t>(crow)]);
    chopBus_.setSelectedId(it == routing.chopBus.end() ? 10 : it->second + 1, juce::dontSendNotification);
  }
  const int rrow = roleList_.getSelectedRow();
  if (rrow >= 0 && rrow < static_cast<int>(roleNames_.size())) {
    const auto it = routing.roleBus.find(roleNames_[static_cast<std::size_t>(rrow)]);
    roleBus_.setSelectedId(it == routing.roleBus.end() ? 1 : it->second + 1, juce::dontSendNotification);
  }
  loading_ = false;
  chopBus_.setEnabled(crow >= 0);
  roleBus_.setEnabled(rrow >= 0);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&mode_, &autoRole_, &autoChop_, &reset_}) c->setEnabled(haveSource);
  juce::String text;
  if (!haveSource) text = "Outputs: load a source first";
  else if (aux_ == 0) text = "Outputs: main only (the host enabled no extra outputs)";
  else text = "Outputs: main + " + juce::String(aux_) + " aux active";
  if (status_.getText() != text) status_.setText(text, juce::dontSendNotification);
}

void OutputsPanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xf015181d));
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRect(getLocalBounds());
}

void OutputsPanel::resized() {
  auto r = getLocalBounds().reduced(10);
  status_.setBounds(r.removeFromTop(24));
  auto top = r.removeFromTop(30);
  mode_.setBounds(top.removeFromLeft(160).reduced(2));
  for (juce::Button* b : std::initializer_list<juce::Button*>{&autoRole_, &autoChop_, &reset_}) b->setBounds(top.removeFromLeft(130).reduced(2));
  close_.setBounds(top.removeFromRight(130).reduced(2));
  auto left = r.removeFromLeft(r.getWidth() / 2 - 5);
  r.removeFromLeft(10);
  chopCaption_.setBounds(left.removeFromTop(22));
  chopBus_.setBounds(left.removeFromBottom(30).removeFromLeft(240).reduced(2));
  chopList_.setBounds(left);
  roleCaption_.setBounds(r.removeFromTop(22));
  roleBus_.setBounds(r.removeFromBottom(30).removeFromLeft(240).reduced(2));
  roleList_.setBounds(r);
}
