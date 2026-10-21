#include "MacroStrip.h"

#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "CdlLookAndFeel.h"

namespace host = chopfractal::host;

namespace {
struct MacroDef {
  const char* paramId;
  const char* label;
  const char* tip;  // one clause, no final period
};
// Order is the on-screen order. Everything else (range, default, unit) is read from the manifest.
constexpr MacroDef kMacros[] = {
    {"variation_amount", "Variation", "How far each new pattern strays from the source feel"},
    {"density", "Density", "How many hits fill the pattern"},
    {"swing", "Swing", "Push off-beat hits later"},
    {"chaos", "Chaos", "Push variation, density and effects up together"},
    {"stutter_amount", "Stutter", "Strength of retriggered stutter hits"},
    {"break_glitch", "Glitch", "How often the break engine adds glitches"},
    {"break_groove", "Groove", "Amount of push and pull in the break feel"},
    {"dry_mix", "Mix", "Blend of the untouched source under the pattern"},
    {"output_gain_db", "Output", "Final output level of the plugin"},
};
juce::String minus(juce::String s) { return s.replace("-", juce::String::charToString(0x2212)); }
}  // namespace

juce::String MacroKnob::unitOf(const host::ParamDef& d) { return d.unit && d.unit[0] ? juce::String(d.unit) : juce::String(); }

juce::String MacroKnob::formatNumber(const host::ParamDef& d, double v) {
  if (d.kind == host::ParamKind::Choice) {
    auto items = juce::StringArray::fromTokens(d.choices ? d.choices : "", "|", "");
    return items[juce::jlimit(0, std::max(0, items.size() - 1), static_cast<int>(std::lround(v)))];
  }
  const juce::String unit = unitOf(d);
  if (unit == "%") return juce::String(static_cast<int>(std::lround(d.maxValue <= 1.0 ? v * 100.0 : v)));
  if (unit == "dB") return minus(juce::String(v, 1));
  return minus(juce::String(v, d.step >= 1.0 ? 0 : 2));
}

MacroKnob::MacroKnob(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& label, const juce::String& tip)
    : apvts_(apvts), def_(host::findParam(paramId)), label_(label) {
  slider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  slider_.setVelocityBasedMode(false);
  slider_.setMouseDragSensitivity(200);
  slider_.setScrollWheelEnabled(true);
  slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
  slider_.setTitle(label + " macro");  // not the bare name: the help catalog would replace the one-clause tip
  slider_.setTooltip(tip);
  slider_.onValueChange = [this] { repaint(); };
  if (std::strcmp(paramId, "stutter_amount") == 0) {
    // The Stutter amount only reaches the pattern while "Allow Stutter" is on; turning the macro up must not be silent.
    slider_.onValueChange = [this] {
      repaint();
      if (slider_.getValue() > 0.0)
        if (auto* allow = apvts_.getParameter("allow_stutter"))
          if (allow->getValue() < 0.5f) allow->setValueNotifyingHost(1.0f);
    };
  }
  addAndMakeVisible(slider_);
  if (apvts_.getParameter(paramId) != nullptr) attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts_, paramId, slider_);
  if (def_ != nullptr) slider_.setDoubleClickReturnValue(true, def_->defaultValue);  // double-click resets to the manifest default
  setDriftFromParameter();
}

void MacroKnob::setDriftFromParameter() {
  if (auto* p = apvts_.getParameter(def_ ? def_->id : "")) slider_.getProperties().set(cdl::kDriftProperty, p->getValue());
  repaint();
}

juce::String MacroKnob::valueText() const {
  return def_ ? formatNumber(*def_, slider_.getValue()) + (def_->kind == host::ParamKind::Choice ? juce::String() : unitOf(*def_).isEmpty() ? juce::String() : " " + unitOf(*def_))
              : juce::String();
}

void MacroKnob::resized() {
  const int labelH = static_cast<int>(std::lround(16 * scale_)), valueH = static_cast<int>(std::lround(16 * scale_));
  auto area = getLocalBounds().reduced(2, 0);
  area.removeFromBottom(labelH + valueH + 4);
  const int d = std::min(area.getWidth(), std::max(area.getHeight(), 10));
  slider_.setBounds(area.withSizeKeepingCentre(d, d));
}

void MacroKnob::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  const int labelH = static_cast<int>(std::lround(16 * scale_)), valueH = static_cast<int>(std::lround(16 * scale_));
  auto lower = getLocalBounds().removeFromBottom(labelH + valueH + 4);
  g.setColour(p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Label, scale_));
  g.drawText(label_.toUpperCase(), lower.removeFromTop(labelH + 2), juce::Justification::centred, false);
  if (!def_) return;
  const juce::String num = formatNumber(*def_, slider_.getValue());
  const juce::String unit = def_->kind == host::ParamKind::Choice ? juce::String() : unitOf(*def_);
  const auto f = cdl::font(cdl::FontRole::Value, scale_);
  g.setFont(f);
  const int nw = juce::GlyphArrangement::getStringWidthInt(f, num);
  const int uw = unit.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt(cdl::font(cdl::FontRole::Hint, scale_), unit) + 2;
  const int x = lower.getCentreX() - (nw + uw) / 2;
  g.setColour(p.ink);
  g.drawText(num, x, lower.getY(), nw + 2, lower.getHeight(), juce::Justification::centredLeft, false);
  if (uw > 0) {
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::Hint, scale_));
    g.drawText(unit, x + nw + 2, lower.getY(), uw + 4, lower.getHeight(), juce::Justification::centredLeft, false);
  }
}

MacroStrip::MacroStrip(EditorModel& model, EditorCommands& commands, juce::AudioProcessorValueTreeState& apvts)
    : EditorPanelBase(model, commands, "MacroStrip"), apvts_(apvts) {
  generate_.getProperties().set("cdlPrimary", true);
  generate_.setTitle("Generate");
  generate_.onClick = [this] { cmd_.generate(); };
  reroll_.setTitle("Reroll");
  reroll_.onClick = [this] { cmd_.reroll(); };
  evolve_.setTitle("Evolve");
  evolve_.onClick = [this] {
    if (onEvolve) onEvolve(evolve_.getToggleState());
  };
  for (juce::Component* c : std::initializer_list<juce::Component*>{&generate_, &reroll_, &evolve_, &profile_, &levelMatch_, &limiter_}) addAndMakeVisible(*c);
  generate_.setEnabled(model_.view().hasSource);

  if (const auto* def = host::findParam("break_profile")) {
    profile_.addItemList(juce::StringArray::fromTokens(def->choices, "|", ""), 1);
    profile_.setTitle("Break profile");
    profile_.setTooltip("Style of break the generator imitates");
    profileAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts_, "break_profile", profile_);
  }
  levelMatch_.setTitle("Level match");
  levelMatch_.setTooltip("Match the rendered level to the source loop");
  limiterAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts_, "limiter_enable", limiter_);
  matchAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts_, "level_match", levelMatch_);
  limiter_.setTitle("Safety limiter");
  limiter_.setTooltip("Catch peaks above the ceiling");
  reroll_.setTooltip("New seed, then generate");
  evolve_.setTooltip("Keep changing the pattern while it plays");

  for (const auto& m : kMacros) {
    if (host::findParam(m.paramId) == nullptr || apvts_.getParameter(m.paramId) == nullptr) continue;  // data driven: a missing parameter has no knob
    knobs_.push_back(std::make_unique<MacroKnob>(apvts_, m.paramId, m.label, m.tip));
    addAndMakeVisible(*knobs_.back());
  }
}

void MacroStrip::setScale(float s) {
  scale_ = s;
  for (auto& k : knobs_) k->setScale(s);
  resized();
  repaint();
}

void MacroStrip::resetDriftTicks() {
  for (auto& k : knobs_) k->setDriftFromParameter();
}

void MacroStrip::syncEvolve() {
  if (evolve_.getToggleState() != model_.evolveRunning()) evolve_.setToggleState(model_.evolveRunning(), juce::dontSendNotification);
}

void MacroStrip::modelChanged(Change c) {
  if (c == Change::View) generate_.setEnabled(model_.view().hasSource);
  if (c == Change::Evolve) syncEvolve();
  if (c == Change::Extras) resetDriftTicks();  // a project was restored: its values are the new reference
}

void MacroStrip::resized() {
  const float s = scale_;
  auto px = [s](float v) { return static_cast<int>(std::lround(v * s)); };
  auto area = getLocalBounds();
  const bool tight = getHeight() < px(150);
  clusterArea_ = area.removeFromLeft(std::max(px(232), 176));
  area.removeFromLeft(px(12));
  knobArea_ = area;
  {
    auto c = clusterArea_.reduced(0, px(8));
    generate_.setBounds(c.removeFromTop(px(tight ? 40 : 52)));
    c.removeFromTop(px(6));
    auto row = c.removeFromTop(px(tight ? 26 : 32));
    reroll_.setBounds(row.removeFromLeft((row.getWidth() - px(6)) / 2));
    row.removeFromLeft(px(6));
    evolve_.setBounds(row);
    c.removeFromTop(px(6));
    profile_.setBounds(c.removeFromTop(px(tight ? 22 : 26)));
  }
  // Cells: eight knobs of one unit and the Output cell, which is 1.5 units wide to carry the Match and Limit toggles.
  const int n = static_cast<int>(knobs_.size());
  if (n == 0) return;
  // The Output cell carries the Match and Limit toggles to the right of its knob.
  // Wide enough for the widest toggle label at this scale (box, gap, text and a little air), never less than the design width.
  const auto labelFont = cdl::font(cdl::FontRole::Body, scale_);
  const int labelW = std::max(juce::GlyphArrangement::getStringWidthInt(labelFont, "Match"), juce::GlyphArrangement::getStringWidthInt(labelFont, "Limit"));
  const int togglesFloor = std::max(px(78), px(2 + 14 + 10 + 6) + labelW);
  const float units = static_cast<float>(n);
  const int unit = std::clamp(static_cast<int>(static_cast<float>(knobArea_.getWidth() - togglesFloor) / units), px(72), px(130));
  const int total = n * unit + togglesFloor;
  int x = knobArea_.getX() + std::max(0, (knobArea_.getWidth() - total) / 2);
  for (int i = 0; i < n; ++i) {
    const bool output = i == n - 1;
    const int w = output ? unit + togglesFloor : unit;
    auto cell = juce::Rectangle<int>(x, knobArea_.getY(), w, knobArea_.getHeight());
    if (output) {
      auto toggles = cell.removeFromRight(togglesFloor);
      knobs_[static_cast<std::size_t>(i)]->setBounds(cell);
      const int h = px(22);
      toggles = toggles.withSizeKeepingCentre(toggles.getWidth(), 2 * h + px(6)).withY(cell.getY() + px(18));
      levelMatch_.setBounds(toggles.removeFromTop(h));
      toggles.removeFromTop(px(6));
      limiter_.setBounds(toggles.removeFromTop(h));
    } else {
      knobs_[static_cast<std::size_t>(i)]->setBounds(cell);
    }
    x += w;
  }
}

void MacroStrip::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  g.setColour(p.panel);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
  g.setColour(p.edge);
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
  // stepped trace between the cluster and the knobs
  const float x = static_cast<float>(clusterArea_.getRight()) + static_cast<float>(getWidth() > 0 ? 0 : 0) + 5.0f;
  juce::Path t;
  t.startNewSubPath(x, 10.0f);
  t.lineTo(x, static_cast<float>(getHeight()) * 0.5f - 6.0f);
  t.lineTo(x + 3.0f, static_cast<float>(getHeight()) * 0.5f - 3.0f);
  t.lineTo(x + 3.0f, static_cast<float>(getHeight()) - 10.0f);
  g.setColour(p.edge);
  g.strokePath(t, juce::PathStrokeType(1.0f));
}
