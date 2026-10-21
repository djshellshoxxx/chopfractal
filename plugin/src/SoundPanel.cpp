#include "SoundPanel.h"

#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include <cmath>
#include <cstring>

#include "CdlLookAndFeel.h"
#include "CdlTheme.h"

namespace cf = chopfractal;

namespace {
// One clause each, no final period. The value, range and default follow on a second line (ParamTip).
struct Help {
  const char* id;
  const char* text;
};
constexpr Help kHelp[] = {
    {"engine_version", "Break builds patterns from drum roles and a style, Legacy is the original random engine"},
    {"break_profile", "The break style that sets which steps get hits and how hard"},
    {"break_backbeat", "Rolling puts ghost notes on steps 7 and 15 between the snares"},
    {"break_anchors", "How strictly the kick on 1 and the snares on 5 and 13 are kept"},
    {"break_groove", "How much push and pull the break feel adds"},
    {"break_humanize", "How far hits drift from the grid, tight for anchors and looser for the rest"},
    {"break_fill", "How often the last bar of a phrase carries a fill"},
    {"break_glitch", "How often glitch gestures such as rolls, reverses and tape stops appear"},
    {"groove_enable", "Apply the groove template when the pattern plays, the stored hits stay on the grid"},
    {"groove_template", "The swing feel to apply, from straight to MPC and shuffle grooves"},
    {"groove_swing", "Where the off-beat 16ths sit between straight at 50 percent and heavy triplet swing"},
    {"groove_hum_time", "Random timing offset per hit in ticks, the same every time for a given seed"},
    {"groove_hum_vel", "Random level offset per hit, the same every time for a given seed"},
    {"allow_stutter", "Let generation turn long hits into buffer-repeat stutters"},
    {"stutter_amount", "How many eligible hits get a stutter"},
    {"stutter_rate", "The note value of the first repeat"},
    {"stutter_gate", "How much of each repeat is audible"},
    {"stutter_decay_db", "Level change per repeat, negative fades out and positive builds up"},
    {"stutter_pitch", "Pitch ramp across the repeats in semitones"},
    {"allow_roll", "Let generation turn hits into rolls that speed up or slow down"},
    {"roll_amount", "How many eligible hits get a roll"},
    {"roll_rate", "The note value the roll starts on"},
    {"roll_rate_end", "The note value the roll ends on"},
    {"roll_freeze", "Where in the hit the roll stops moving and holds its last rate"},
    {"allow_echo", "Let generation add tempo-synced echoes to hits"},
    {"echo_amount", "How many eligible hits get an echo"},
    {"echo_rate", "The delay time of each repeat as a note value"},
    {"echo_decay", "How much quieter each repeat is than the last"},
    {"echo_tone", "Darker to brighter repeats"},
    {"allow_tremolo", "Let generation add tremolo or auto-pan to hits"},
    {"trem_amount", "How many eligible hits get tremolo or pan"},
    {"trem_mode", "Tremolo moves the level, Pan moves the stereo position, Both does both"},
    {"trem_rate", "The speed of the modulation as a note value"},
    {"trem_depth", "How far the level or position moves"},
    {"level_match", "Match the loudness of the pattern to the loaded loop"},
    {"match_trim_db", "Fine tune the matched level up or down"},
    {"limiter_enable", "Catch peaks above the ceiling with a true-peak safe limiter"},
    {"limiter_ceiling_db", "The level the limiter never lets the output pass"},
    {"slice_polish", "Fade hit edges and cut off tails so slices do not click"},
};
const char* helpFor(const char* id) {
  for (const auto& h : kHelp)
    if (std::strcmp(h.id, id) == 0) return h.text;
  return nullptr;
}
}  // namespace

juce::Component* SoundPanel::Row::control() const {
  if (slider) return slider.get();
  if (combo) return combo.get();
  return toggle.get();
}

SoundPanel::SoundPanel(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts), tips_(apvts) {
  setComponentID("SoundPanel");
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&body_, false);
  viewport_.setScrollBarsShown(true, false);
  viewport_.setTitle("Sound");

  addSection("Break engine", {"engine_version", "break_profile", "break_backbeat", "break_anchors", "break_groove", "break_humanize", "break_fill", "break_glitch"});
  addSection("Groove Lab", {"groove_enable", "groove_template", "groove_swing", "groove_hum_time", "groove_hum_vel"});
  addSection("Stutter", {"allow_stutter", "stutter_amount", "stutter_rate", "stutter_gate", "stutter_decay_db", "stutter_pitch"});
  addSection("Roll", {"allow_roll", "roll_amount", "roll_rate", "roll_rate_end", "roll_freeze"});
  addSection("Echo", {"allow_echo", "echo_amount", "echo_rate", "echo_decay", "echo_tone"});
  addSection("Tremolo-Pan", {"allow_tremolo", "trem_amount", "trem_mode", "trem_rate", "trem_depth"});
  addSection("Output", {"level_match", "match_trim_db", "limiter_enable", "limiter_ceiling_db", "slice_polish"});
  body_.setSize(320, 900);
}

void SoundPanel::addSection(const char* title, std::initializer_list<const char*> ids) {
  Section sec;
  sec.title = title;
  for (const char* id : ids) {
    const cf::host::ParamDef* def = cf::host::findParam(id);
    jassert(def != nullptr);  // a manifest id that is missing is a programming error caught by the reachability test
    if (def == nullptr) continue;
    Row row;
    row.id = id;
    const juce::String help = helpFor(id) != nullptr ? juce::String(helpFor(id)) : juce::String(def->name);
    const juce::String name = def->name;
    if (def->kind == cf::host::ParamKind::Float) {
      row.label = std::make_unique<juce::Label>();
      row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
      row.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 20);
      row.slider->setTitle(name);
      row.sliderAttachment = std::make_unique<juce::SliderParameterAttachment>(*apvts_.getParameter(id), *row.slider, nullptr);
      const std::string unit = def->unit ? def->unit : "";
      const bool unitPercent = unit == "%" && def->maxValue <= 1.0;
      row.slider->textFromValueFunction = [unit, unitPercent, step = def->step](double v) {
        if (unitPercent) return juce::String(static_cast<int>(std::lround(v * 100.0))) + " %";
        const bool whole = step >= 1.0 || unit == "ticks" || unit == "%";
        const juce::String n = whole ? juce::String(static_cast<int>(std::lround(v))) : juce::String(v, 1);
        return unit.empty() ? n : n + " " + juce::String(unit);
      };
      row.slider->valueFromTextFunction = [unitPercent](const juce::String& t) {
        const double v = t.retainCharacters("0123456789.-+").getDoubleValue();
        return unitPercent ? v / 100.0 : v;
      };
      row.slider->updateText();  // the attachment installs the parameter's own text functions; ours replace them (manifest units, percent as a whole number)
      row.slider->setDoubleClickReturnValue(true, def->defaultValue);
    } else if (def->kind == cf::host::ParamKind::Choice) {
      row.label = std::make_unique<juce::Label>();
      row.combo = std::make_unique<juce::ComboBox>();
      row.combo->setTitle(name);
      auto* choice = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter(id));
      if (choice != nullptr) row.combo->addItemList(choice->choices, 1);
      row.comboAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(*apvts_.getParameter(id), *row.combo, nullptr);
    } else {
      row.toggle = std::make_unique<juce::ToggleButton>(name);
      row.toggle->setTitle(name);
      row.toggleAttachment = std::make_unique<juce::ButtonParameterAttachment>(*apvts_.getParameter(id), *row.toggle, nullptr);
    }
    if (row.label) {
      row.label->setText(name, juce::dontSendNotification);
      row.label->setMinimumHorizontalScale(0.8f);
      row.label->setInterceptsMouseClicks(false, false);
      body_.addAndMakeVisible(*row.label);
    }
    body_.addAndMakeVisible(*row.control());
    row.control()->setComponentID(juce::String("param:") + id);
    tips_.bind(*row.control(), id, help);
    sec.rows.push_back(rows_.size());
    rows_.push_back(std::move(row));
  }
  sections_.push_back(std::move(sec));
}

std::vector<std::string> SoundPanel::boundIds() const {
  std::vector<std::string> out;
  for (const auto& r : rows_) out.push_back(r.id);
  return out;
}

juce::Component* SoundPanel::controlFor(const std::string& paramId) const {
  for (const auto& r : rows_)
    if (r.id == paramId) return r.control();
  return nullptr;
}

void SoundPanel::Body::paint(juce::Graphics& g) {
  for (const auto& s : owner.sections_) cdl::paintSectionLabel(g, s.title, s.area.withHeight(20));
}

void SoundPanel::layoutBody(int width) {
  const auto s = [](int v) { return std::max(1, static_cast<int>(std::lround(static_cast<float>(v) * cdl::uiScale()))); };
  const int pad = s(8), gap = s(4), rowH = s(26), head = s(22), labelW = s(128);
  int y = 0;
  for (auto& sec : sections_) {
    sec.area = {0, y, width, 0};
    y += head;
    for (const std::size_t i : sec.rows) {
      auto& row = rows_[i];
      juce::Rectangle<int> r(pad, y, width - 2 * pad, rowH);
      if (row.label) row.label->setBounds(r.removeFromLeft(labelW));
      row.control()->setBounds(r);
      y += rowH + gap;
    }
    sec.area.setHeight(y - sec.area.getY());
    y += s(8);
  }
  body_.setSize(width, y + s(8));
}

void SoundPanel::resized() {
  viewport_.setBounds(getLocalBounds());
  layoutBody(std::max(240, getWidth() - viewport_.getScrollBarThickness()));
}
