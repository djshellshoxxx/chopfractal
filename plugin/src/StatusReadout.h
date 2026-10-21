#pragma once
// A small text readout of the status bar (voices, sync box): painted text, a title for accessibility, a tooltip, and a
// repaint only when the text actually changes.
#include <juce_gui_basics/juce_gui_basics.h>

#include "CdlLookAndFeel.h"

class StatusReadout : public juce::Component, public juce::SettableTooltipClient {
 public:
  StatusReadout(const juce::String& title, cdl::FontRole role = cdl::FontRole::Value) : role_(role) {
    setTitle(title);
    setComponentID(title.replaceCharacter(' ', '_'));
  }
  void setText(const juce::String& t) {
    if (t == text_) return;
    text_ = t;
    setDescription(t);
    repaint();
  }
  const juce::String& text() const { return text_; }
  void setScale(float s) {
    scale_ = s;
    repaint();
  }
  void paint(juce::Graphics& g) override {
    g.setColour(cdl::pal().ink);
    g.setFont(cdl::font(role_, scale_));
    g.drawText(text_, getLocalBounds(), juce::Justification::centredLeft, true);
  }

 private:
  juce::String text_;
  cdl::FontRole role_;
  float scale_ = 1.0f;
};
