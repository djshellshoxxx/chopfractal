#pragma once
// One labelled slider row of the inspector and the effects rack. The slider commits on release (one undo step per drag) and on
// every keyboard, wheel or typed change; setValue() syncs it from the model without committing. A mixed selection shows "mixed".
#include <functional>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

class ParamRow : public juce::Component {
 public:
  ParamRow(const juce::String& label, const juce::String& tooltip, double min, double max, double step, std::function<juce::String(double)> format) : format_(std::move(format)) {
    setComponentID("ParamRow");
    name_.setText(label, juce::dontSendNotification);
    name_.setTooltip(tooltip);
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);
    slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 20);
    slider_.setRange(min, max, step);
    slider_.setTitle(label);
    slider_.setTooltip(tooltip);
    slider_.textFromValueFunction = [this](double v) { return mixed_ ? juce::String("mixed") : format_(v); };
    slider_.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.-+").getDoubleValue(); };
    slider_.onDragEnd = [this] { commit(); };
    slider_.onValueChange = [this] {
      if (syncing_) return;
      if (!slider_.isMouseButtonDown()) commit();
      else if (onLive) live();
    };
    addAndMakeVisible(slider_);
  }
  void setValue(double v, bool mixed) {
    syncing_ = true;
    mixed_ = mixed;
    slider_.setValue(v, juce::dontSendNotification);
    slider_.updateText();
    syncing_ = false;
  }
  void setDefault(double v) { slider_.setDoubleClickReturnValue(true, v); }
  double value() const { return slider_.getValue(); }
  bool dragging() const { return slider_.isMouseButtonDown(); }
  void setLabelWidth(int w) { labelW_ = w; resized(); }
  void resized() override {
    auto a = getLocalBounds();
    name_.setBounds(a.removeFromLeft(labelW_));
    slider_.setBounds(a);
  }
  std::function<void(double)> onCommit;
  std::function<void(double)> onLive;  // while the pointer drags: a preview of the value (no undo step), at most every 40 ms

 private:
  void live() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastLiveMs_ < 40.0) return;
    lastLiveMs_ = now;
    onLive(slider_.getValue());
  }
  void commit() {
    if (syncing_) return;
    mixed_ = false;
    slider_.updateText();
    if (onCommit) onCommit(slider_.getValue());
  }
  std::function<juce::String(double)> format_;
  juce::Label name_;
  juce::Slider slider_;
  bool mixed_ = false, syncing_ = false;
  double lastLiveMs_ = 0.0;
  int labelW_ = 78;
};
