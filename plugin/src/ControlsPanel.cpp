#include "ControlsPanel.h"

#include <initializer_list>

namespace cf = chopfractal;

ControlsPanel::ControlsPanel(EditorModel& model, EditorCommands& commands, juce::AudioProcessorValueTreeState& apvts)
    : EditorPanelBase(model, commands, "ControlsPanel"), apvts_(apvts) {
  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &generate_, &mutate_, &undo_, &redo_, &zoom_, &collapse_, &lock_, &dice_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  addAndMakeVisible(play_);
  addAndMakeVisible(stop_);
  addAndMakeVisible(followHost_);
  addAndMakeVisible(transportHint_);
  addAndMakeVisible(editMode_);
  editMode_.setTooltip("Off: drag to select sections. On: drag a hit to move or resize it.");
  editMode_.setToggleState(model_.editMode(), juce::dontSendNotification);
  editMode_.onClick = [this] { model_.setEditMode(editMode_.getToggleState()); };
  play_.setTitle("Play");
  stop_.setTitle("Stop");
  play_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff228b44));
  play_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  stop_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffc0392b));
  stop_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  play_.setTooltip("Play the generated pattern from the beginning, including in the standalone app.");
  stop_.setTooltip("Stop local pattern playback. Press Play to restart from the beginning.");
  play_.onClick = [this] { cmd_.play(); };
  stop_.onClick = [this] { cmd_.stop(); };
  followHost_.onClick = [this] { cmd_.followHost(); };
  load_.onClick = [this] { cmd_.chooseFile(); };
  detect_.onClick = [this] { cmd_.detectChops(static_cast<DetectChoice>(model_.detectMode())); };
  generate_.onClick = [this] { cmd_.generate(); };
  mutate_.onClick = [this] { cmd_.mutate(); };
  undo_.onClick = [this] { cmd_.undo(); };
  redo_.onClick = [this] { cmd_.redo(); };
  zoom_.setTooltip("Subdivide the selected pattern hits into nested rhythms. Select hits or waveform sections first.");
  zoom_.onClick = [this] { cmd_.zoomInSelected(); };
  collapse_.onClick = [this] { cmd_.collapseSelected(); };
  lock_.onClick = [this] { cmd_.toggleLockSelectedBar(); };
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

  bars_.addItemList(juce::StringArray::fromTokens(cf::host::parameterManifest()[cf::host::kPatternBars].choices, "|", ""), 1);
  bars_.setTitle("Pattern length in bars");
  addAndMakeVisible(bars_);
  barsAttachment_ = std::make_unique<Attachment::ComboBoxAttachment>(apvts_, "pattern_bars", bars_);
  barsLabel_.attachToComponent(&bars_, true);

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
}

void ControlsPanel::addToggle(ParamToggle& t, const char* paramId, const juce::String& title) {
  t.button.setButtonText(title);
  t.button.setTitle(title);
  addAndMakeVisible(t.button);
  t.attachment = std::make_unique<Attachment::ButtonAttachment>(apvts_, paramId, t.button);
}

void ControlsPanel::modelChanged(Change c) {
  switch (c) {
    case Change::View: {
      const auto& v = model_.view();
      for (juce::Button* b : std::initializer_list<juce::Button*>{&detect_, &generate_}) b->setEnabled(v.hasSource);
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
      if (editMode_.getToggleState() != model_.editMode()) editMode_.setToggleState(model_.editMode(), juce::dontSendNotification);
      break;
    case Change::Extras:  // a project was restored: show its saved choices
      if (static_cast<std::uint64_t>(seed_.getText().getLargeIntValue()) != model_.seed())
        seed_.setText(juce::String(static_cast<juce::int64>(model_.seed())), juce::dontSendNotification);
      if (editMode_.getToggleState() != model_.editMode()) editMode_.setToggleState(model_.editMode(), juce::dontSendNotification);
      break;
    default:
      break;
  }
}

void ControlsPanel::resized() {
  auto area = getLocalBounds();
  auto transport = area.removeFromTop(36);
  play_.setBounds(transport.removeFromLeft(104).reduced(2));
  stop_.setBounds(transport.removeFromLeft(104).reduced(2));
  followHost_.setBounds(transport.removeFromLeft(100).reduced(2));
  editMode_.setBounds(transport.removeFromRight(180).reduced(2));
  transportHint_.setBounds(transport.reduced(8, 2));
  auto row1 = area.removeFromTop(30);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &generate_, &mutate_, &undo_, &redo_, &zoom_, &collapse_, &lock_})
    b->setBounds(row1.removeFromLeft(96).reduced(2));
  area.removeFromTop(6);
  auto row2 = area.removeFromTop(26);
  seedLabel_.setBounds(row2.removeFromLeft(50));
  seed_.setBounds(row2.removeFromLeft(110).reduced(2));
  dice_.setBounds(row2.removeFromLeft(90).reduced(2));
  embed_.setBounds(row2.removeFromLeft(210).reduced(2));
  bars_.setBounds(row2.removeFromRight(90).reduced(2));
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
