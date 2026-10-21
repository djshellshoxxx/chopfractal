#include "ControlsPanel.h"

#include "ControlHelp.h"

#include <initializer_list>

namespace cf = chopfractal;

ControlsPanel::ControlsPanel(EditorModel& model, EditorCommands& commands, juce::AudioProcessorValueTreeState& apvts)
    : EditorPanelBase(model, commands, "ControlsPanel"), apvts_(apvts), tips_(apvts) {
  // Transport, undo/redo, Generate, Load Loop / Detect Chops and the hit-editing buttons live in the shell (TopBar, MacroStrip, ShellChrome).
  for (juce::Button* b : std::initializer_list<juce::Button*>{&mutate_, &dice_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  addAndMakeVisible(followHost_);
  addAndMakeVisible(transportHint_);
  followHost_.onClick = [this] { cmd_.followHost(); };
  mutate_.onClick = [this] { cmd_.mutate(); };
  dice_.onClick = [this] { cmd_.newSeed(); };

  embed_.setToggleState(model_.embedSource(), juce::dontSendNotification);
  embed_.onClick = [this] { model_.setEmbedSource(embed_.getToggleState()); };
  addAndMakeVisible(embed_);

  seedLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(seedLabel_);
  seed_.setText(juce::String(static_cast<juce::int64>(model_.seed())));
  seed_.setInputRestrictions(12, "0123456789");
  seed_.setTitle("Seed");
  seed_.onTextChange = [this] { model_.setSeed(static_cast<std::uint64_t>(seed_.getText().getLargeIntValue())); };
  addAndMakeVisible(seed_);

  addSlider(density_, "density", "Density");
  addSlider(swing_, "swing", "Swing");
  addSlider(variation_, "variation_amount", "Variation");
  addSlider(dry_, "dry_mix", "Dry mix");
  addSlider(gain_, "output_gain_db", "Output gain (dB)");

  addToggle(enable_, "effect_enable", "Effect on");
  addToggle(reverse_, "allow_reverse", "Reverse");
  addToggle(pitch_, "allow_pitch", "Pitch");
  addToggle(retrigger_, "allow_retrigger", "Retrigger");

  addToggle(filter_, "allow_filter", "Filter");
  addToggle(glide_, "allow_glide", "Tape glide");
  addToggle(crunch_, "allow_crunch", "Crunch");
  addSlider(fxIntensity_, "fx_intensity", "FX intensity");
  addSlider(chaos_, "chaos", "Chaos");
  chaos_.slider.setTooltip("One control that pushes variation, density, transforms and effects up together");
}

void ControlsPanel::addSlider(ParamSlider& s, const char* paramId, const juce::String& title) {
  s.slider.setSliderStyle(juce::Slider::LinearHorizontal);
  s.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
  s.slider.setNumDecimalPlacesToDisplay(2);
  s.slider.setTitle(title);
  addAndMakeVisible(s.slider);
  s.label.setText(title, juce::dontSendNotification);
  s.label.attachToComponent(&s.slider, true);
  s.attachment = std::make_unique<Attachment::SliderAttachment>(apvts_, paramId, s.slider);
  tips_.bind(s.slider, paramId, control_help::forName(title));
}

void ControlsPanel::addToggle(ParamToggle& t, const char* paramId, const juce::String& title) {
  t.button.setButtonText(title);
  t.button.setTitle(title);
  addAndMakeVisible(t.button);
  t.attachment = std::make_unique<Attachment::ButtonAttachment>(apvts_, paramId, t.button);
  tips_.bind(t.button, paramId, control_help::forName(title));
}

void ControlsPanel::modelChanged(Change c) {
  switch (c) {
    case Change::View: {
      const auto& v = model_.view();
      mutate_.setEnabled(v.hasPattern);
      break;
    }
    case Change::Seed:
      // The field is the editable view of the seed: leave what the user is typing alone while it still means the same number.
      if (static_cast<std::uint64_t>(seed_.getText().getLargeIntValue()) != model_.seed())
        seed_.setText(juce::String(static_cast<juce::int64>(model_.seed())), juce::dontSendNotification);
      break;
    case Change::Flags:
      if (embed_.getToggleState() != model_.embedSource()) embed_.setToggleState(model_.embedSource(), juce::dontSendNotification);
      break;
    case Change::Extras:  // a project was restored: show its saved choices
      if (static_cast<std::uint64_t>(seed_.getText().getLargeIntValue()) != model_.seed())
        seed_.setText(juce::String(static_cast<juce::int64>(model_.seed())), juce::dontSendNotification);
      break;
    default:
      break;
  }
}

void ControlsPanel::resized() {
  auto area = getLocalBounds();
  auto row1 = area.removeFromTop(30);
  mutate_.setBounds(row1.removeFromLeft(96).reduced(2));
  followHost_.setBounds(row1.removeFromLeft(100).reduced(2));
  transportHint_.setBounds(row1.reduced(8, 2));
  area.removeFromTop(6);
  auto row2 = area.removeFromTop(26);
  seedLabel_.setBounds(row2.removeFromLeft(50));
  seed_.setBounds(row2.removeFromLeft(110).reduced(2));
  dice_.setBounds(row2.removeFromLeft(90).reduced(2));
  embed_.setBounds(row2.removeFromLeft(210).reduced(2));
  area.removeFromTop(6);
  auto sliders = area.removeFromTop(128);
  auto left = sliders.removeFromLeft(sliders.getWidth() / 2);
  density_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  swing_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  variation_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  fxIntensity_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  chaos_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  dry_.slider.setBounds(sliders.removeFromTop(24).withTrimmedLeft(130));
  gain_.slider.setBounds(sliders.removeFromTop(24).withTrimmedLeft(130));
  auto toggles = sliders.removeFromTop(24);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&enable_.button, &reverse_.button, &pitch_.button, &retrigger_.button}) b->setBounds(toggles.removeFromLeft(100));
  auto toggles2 = sliders.removeFromTop(24);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&filter_.button, &glide_.button, &crunch_.button}) b->setBounds(toggles2.removeFromLeft(110));
}
