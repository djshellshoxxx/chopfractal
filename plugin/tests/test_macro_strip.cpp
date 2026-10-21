// Macro strip: data driven from the host parameter manifest, units, tooltips, reset and the drift tick.
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <memory>

#include "CdlLookAndFeel.h"
#include "MacroStrip.h"
#include "PluginEditor.h"
#include "shell_test_support.hpp"

using namespace shelltest;
namespace host = chopfractal::host;

CHOP_TEST(macro_knob_text_uses_the_manifest_units) {
  const auto* density = host::findParam("density");
  const auto* output = host::findParam("output_gain_db");
  const auto* stutter = host::findParam("stutter_amount");
  const auto* profile = host::findParam("break_profile");
  CHECK(density && output && stutter && profile);
  if (!(density && output && stutter && profile)) return;
  CHECK(MacroKnob::formatNumber(*density, 0.5) == "50" && MacroKnob::unitOf(*density) == "%");
  CHECK(MacroKnob::formatNumber(*stutter, 25.0) == "25");  // already a percentage in the manifest
  CHECK(MacroKnob::formatNumber(*output, -12.4) == juce::String::charToString(0x2212) + "12.4" && MacroKnob::unitOf(*output) == "dB");
  CHECK(MacroKnob::formatNumber(*profile, 1.0) == "Jungle");
}

CHOP_TEST(macro_strip_knobs_follow_the_parameters_reset_on_double_click_and_have_clause_tooltips) {
  if (!haveDisplay()) return;
  ChopFractalProcessor p;
  ChopFractalEditor editor(p);
  const auto& knobs = editor.macroStrip().knobs();
  CHECK(knobs.size() == 9);
  const char* expected[] = {"variation_amount", "density", "swing", "chaos", "stutter_amount", "break_glitch", "break_groove", "dry_mix", "output_gain_db"};
  for (std::size_t i = 0; i < knobs.size() && i < 9; ++i) {
    auto* k = knobs[i].get();
    CHECK(k->def() != nullptr && juce::String(k->def()->id) == expected[i]);
    if (!k->def()) continue;
    auto& s = k->slider();
    const auto tip = s.getTooltip();
    CHECK(tip.length() > 20 && !tip.endsWith(".") && !tip.contains(". "));  // one clause, no final period
    CHECK(s.getDoubleClickReturnValue() == k->def()->defaultValue);
    // The slider is bound to the parameter: moving the parameter moves the knob, and the reset value is the manifest default.
    auto* param = p.apvts.getParameter(k->def()->id);
    CHECK(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(k->def()->maxValue)));
    pump(30);
    CHECK_NEAR(s.getValue(), k->def()->maxValue, 1e-3);
    CHECK(k->valueText().isNotEmpty());
    s.setValue(k->def()->defaultValue, juce::sendNotificationSync);
    CHECK(s.getProperties()[cdl::kDriftProperty].isDouble());  // the drift tick marks the value the session opened with
  }
}
