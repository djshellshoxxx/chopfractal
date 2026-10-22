// The Sound drawer and parameter reachability: every host parameter has a control in the editor, every control carries a
// one-clause tooltip with the describeParam value line, and a control moves its parameter and follows it.
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <set>
#include <string>

#include "ParamTip.h"
#include "SoundPanel.h"
#include "perform_test_support.hpp"
#include "workflow_test_support.hpp"

using namespace shelltest;
namespace host = chopfractal::host;

namespace {
// The param:<id> component IDs of every control below the editor, drawers included.
std::set<std::string> controlIds(juce::Component& root) {
  std::set<std::string> ids;
  forEachControl(root, [&](juce::Component& c) {
    const juce::String id = c.getComponentID();
    if (id.startsWith("param:")) ids.insert(id.substring(6).toStdString());
  });
  return ids;
}
}  // namespace

CHOP_TEST(every_host_parameter_has_a_control_in_the_editor) {
  if (!haveDisplay()) return;
  EditorRig rig(false);
  if (!rig.ed) return;
  const auto ids = controlIds(*rig.ed);
  for (const auto& d : host::parameterManifest()) {
    if (ids.count(d.id) == 0) std::fprintf(stderr, "no UI control for parameter '%s'\n", d.id);
    CHECK(ids.count(d.id) == 1);
  }
}

CHOP_TEST(sound_drawer_opens_and_every_control_is_described_with_the_value_line) {
  if (!haveDisplay()) return;
  for (auto size : {std::pair<int, int>{1100, 720}, std::pair<int, int>{880, 560}}) {
    EditorRig rig(false, size.first, size.second);
    if (!rig.ed) return;
    CHECK(rig.ed->openDrawer("sound"));
    pump(60);
    auto& panel = rig.ed->soundPanel();
    CHECK(panel.getWidth() > 200);
    for (const auto& id : panel.boundIds()) {
      auto* c = panel.controlFor(id);
      CHECK(c != nullptr);
      if (!c) continue;
      auto* client = dynamic_cast<juce::SettableTooltipClient*>(c);
      CHECK(client != nullptr);
      const auto tip = client ? client->getTooltip() : juce::String();
      const auto* def = host::findParam(id);
      CHECK(def != nullptr);
      CHECK(!c->getTitle().isEmpty());
      if (def) {
        const bool ends = tip.endsWith(juce::String(host::describeParam(*def, def->defaultValue).c_str()));  // the value line closes the tooltip
        if (!ends) std::fprintf(stderr, "tooltip of '%s' is '%s'\n", id.c_str(), tip.toRawUTF8());
        CHECK(ends);
      }
      const auto help = tip.upToFirstOccurrenceOf("\n", false, false);
      CHECK(help.length() > 10 && !help.endsWith("."));  // one clause, no final period
    }
    checkDescribed(panel, "sound");
  }
}

CHOP_TEST(sound_controls_move_their_parameters_and_follow_them) {
  if (!haveDisplay()) return;
  EditorRig rig(false);
  if (!rig.ed) return;
  auto& panel = rig.ed->soundPanel();
  auto* echo = dynamic_cast<juce::ToggleButton*>(panel.controlFor("allow_echo"));
  auto* rate = dynamic_cast<juce::ComboBox*>(panel.controlFor("echo_rate"));
  auto* amount = dynamic_cast<juce::Slider*>(panel.controlFor("echo_amount"));
  auto* ceiling = dynamic_cast<juce::Slider*>(panel.controlFor("limiter_ceiling_db"));
  auto* trem = dynamic_cast<juce::ComboBox*>(panel.controlFor("trem_mode"));
  CHECK(echo && rate && amount && ceiling && trem);
  if (!(echo && rate && amount && ceiling && trem)) return;
  auto value = [&](const char* id) { return static_cast<double>(*rig.proc.apvts.getRawParameterValue(id)); };
  CHECK(value("allow_echo") == 0.0);
  echo->setToggleState(true, juce::sendNotificationSync);
  CHECK(value("allow_echo") == 1.0);
  selectItem(*rate, "Dotted 1/8");
  CHECK(value("echo_rate") == 2.0);
  amount->setValue(75.0, juce::sendNotificationSync);
  CHECK_NEAR(value("echo_amount"), 75.0, 0.01);
  CHECK(amount->getTextFromValue(75.0) == "75 %");
  ceiling->setValue(-3.0, juce::sendNotificationSync);
  CHECK_NEAR(value("limiter_ceiling_db"), -3.0, 0.01);
  CHECK(ceiling->getTextFromValue(-3.0).contains("dB"));
  // Host automation or a preset moves the parameter: the control follows.
  auto* p = rig.proc.apvts.getParameter("trem_mode");
  p->setValueNotifyingHost(p->convertTo0to1(0.0f));
  pump(40);
  CHECK(trem->getSelectedItemIndex() == 0);
  // Double-click resets to the manifest default.
  CHECK(amount->getDoubleClickReturnValue() == host::findParam("echo_amount")->defaultValue);
}

CHOP_TEST(parameter_tooltips_show_the_current_value_when_the_pointer_arrives) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  ParamTip tip(p.apvts);
  juce::Slider slider;
  tip.bind(slider, "density", "How many hits fill the pattern");
  CHECK(slider.getComponentID() == "param:density");
  CHECK(slider.getTooltip() == "How many hits fill the pattern\nDensity: 50% (range 0% to 100%, default 50%)");
  auto* density = p.apvts.getParameter("density");
  density->setValueNotifyingHost(density->convertTo0to1(0.8f));
  CHECK(slider.getTooltip().contains("Density: 50%"));  // not rebuilt yet
  const juce::Time now = juce::Time::getCurrentTime();
  juce::MouseEvent ev(juce::Desktop::getInstance().getMainMouseSource(), {}, juce::ModifierKeys(), 0.f, 0.f, 0.f, 0.f, 0.f, &slider, &slider, now, {}, now, 1, false);
  tip.mouseEnter(ev);
  CHECK(slider.getTooltip().contains("Density: 80%"));
  CHECK(slider.getTooltip().startsWith("How many hits fill the pattern\n"));
}
