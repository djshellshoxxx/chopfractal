#include "GesturePanel.h"

namespace rg = chopfractal::render;

namespace {
constexpr rg::GestureKind kKinds[3] = {rg::GestureKind::Stutter, rg::GestureKind::Reverse, rg::GestureKind::TapeStop};
const char* const kNames[3] = {"Stutter", "Reverse", "Tape Stop"};
const char* const kTips[3] = {"Hold to freeze the last hit into a repeating slice", "Hold to play everything backwards",
                              "Hold to slow the music to a halt, release to spin back up"};

void styleLabel(juce::Label& l, const juce::String& text) {
  l.setText(text, juce::dontSendNotification);
  l.setJustificationType(juce::Justification::centredLeft);
}

void styleSlider(juce::Slider& s, const juce::String& title, const juce::String& tip, float value) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
  s.setNormalisableRange(juce::NormalisableRange<double>(20.0, 4000.0, 1.0, 0.4));
  s.setTextValueSuffix(" ms");
  s.setTitle(title);
  s.setTooltip(tip);
  s.setDoubleClickReturnValue(true, value);
  s.setValue(value, juce::dontSendNotification);
}
}  // namespace

GesturePanel::GesturePanel(ChopFractalProcessor& processor) : proc_(processor) {
  setComponentID("GesturePanel");
  setTitle("Gestures");
  for (int i = 0; i < 3; ++i) {
    auto& b = buttons_[static_cast<std::size_t>(i)];
    b.setButtonText(juce::String("Hold ") + kNames[i]);  // not the bare effect names: the Tools toggles with those names carry other help
    b.setTitle(juce::String(kNames[i]) + " gesture");
    b.setTooltip(kTips[i]);
    b.setComponentID(juce::String("gesture:") + kNames[i]);
    b.onStateChange = [this, i] { held(i, buttons_[static_cast<std::size_t>(i)].isDown()); };
    addAndMakeVisible(b);
  }
  styleLabel(rateLabel_, "Stutter rate");
  styleLabel(stopLabel_, "Tape stop time");
  styleLabel(spinLabel_, "Tape spin-up time");
  for (auto* l : {&rateLabel_, &stopLabel_, &spinLabel_}) addAndMakeVisible(*l);
  rate_.addItem("1/8", 1);
  rate_.addItem("1/16", 2);
  rate_.addItem("1/32", 3);
  rate_.setSelectedId(juce::jlimit(0, rg::kStutterRates - 1, proc_.gestureStutterRate.load()) + 1, juce::dontSendNotification);
  rate_.setTitle("Stutter rate");
  rate_.setTooltip("How fast a held Stutter repeats");
  rate_.onChange = [this] { proc_.gestureStutterRate = rate_.getSelectedId() - 1; };
  addAndMakeVisible(rate_);
  styleSlider(stopMs_, "Tape stop time", "How long a held Tape Stop takes to reach a standstill", proc_.gestureTapeStopMs.load());
  styleSlider(spinMs_, "Tape spin-up time", "How long the music takes to get back to speed after a Tape Stop", proc_.gestureSpinUpMs.load());
  stopMs_.onValueChange = [this] { proc_.gestureTapeStopMs = static_cast<float>(stopMs_.getValue()); };
  spinMs_.onValueChange = [this] { proc_.gestureSpinUpMs = static_cast<float>(spinMs_.getValue()); };
  addAndMakeVisible(stopMs_);
  addAndMakeVisible(spinMs_);
  hint_.setText("MIDI CC 20, 21 and 22 hold Stutter, Reverse and Tape Stop", juce::dontSendNotification);
  hint_.setJustificationType(juce::Justification::topLeft);
  hint_.setMinimumHorizontalScale(1.0f);
  hint_.setTitle("Gesture MIDI controllers");
  hint_.setTooltip("Send a controller value of 64 or more to hold the gesture");
  addAndMakeVisible(hint_);
  startTimerHz(15);
}

GesturePanel::~GesturePanel() {
  stopTimer();
  proc_.releaseAllGestures();
}

void GesturePanel::held(int index, bool on) {
  auto& d = down_[static_cast<std::size_t>(index)];
  if (d == on) return;
  d = on;
  proc_.setGesture(kKinds[index], on);
}

void GesturePanel::visibilityChanged() {
  if (isShowing()) return;
  for (int i = 0; i < 3; ++i) held(i, false);  // a closed drawer must never leave a gesture stuck
}

void GesturePanel::timerCallback() {
  for (int i = 0; i < 3; ++i) {
    auto& b = buttons_[static_cast<std::size_t>(i)];
    const bool active = proc_.gestureActive(kKinds[i]);
    if (b.getToggleState() != active) b.setToggleState(active, juce::dontSendNotification);
  }
}

void GesturePanel::paint(juce::Graphics&) {}

void GesturePanel::resized() {
  auto r = getLocalBounds().reduced(12, 10);
  for (auto& b : buttons_) {
    b.setBounds(r.removeFromTop(36));
    r.removeFromTop(6);
  }
  r.removeFromTop(14);
  auto a = r.removeFromTop(28);
  rateLabel_.setBounds(a.removeFromLeft(120));
  rate_.setBounds(a.removeFromLeft(100));
  r.removeFromTop(8);
  auto b = r.removeFromTop(28);
  stopLabel_.setBounds(b.removeFromLeft(120));
  stopMs_.setBounds(b);
  r.removeFromTop(8);
  auto c = r.removeFromTop(28);
  spinLabel_.setBounds(c.removeFromLeft(120));
  spinMs_.setBounds(c);
  r.removeFromTop(12);
  hint_.setBounds(r.removeFromTop(40));
}
