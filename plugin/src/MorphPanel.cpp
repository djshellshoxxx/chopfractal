#include "MorphPanel.h"

#include <initializer_list>

#include "PluginProcessor.h"

namespace cf = chopfractal;

MorphPanel::MorphPanel(ChopFractalProcessor& processor) : proc_(processor) {
  morph_.setTitle("Morph between A and B");
  morph_.setTooltip("Blend continuously between the stored patterns A and B. Locks are not enforced inside the blend. Store A and B first.");
  amount_.setTitle("Morph amount");
  amount_.setRange(0.0, 1.0, 0.01);
  amount_.setValue(0.5, juce::dontSendNotification);
  amount_.setSliderStyle(juce::Slider::LinearHorizontal);
  amount_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
  amount_.setDoubleClickReturnValue(true, 0.5);
  amount_.setNumDecimalPlacesToDisplay(2);
  amount_.textFromValueFunction = [](double v) { return amountText(v); };
  amount_.setTooltip("Position between A (left) and B (right). Double-click to return to 50%.");
  ends_.setText("A    B", juce::dontSendNotification);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&morph_, &commit_, &cancel_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  addAndMakeVisible(amount_);
  addAndMakeVisible(ends_);
  morph_.onClick = [this] { toggled(); };
  amount_.onValueChange = [this] {
    if (!syncing_) dirty_ = true;
  };
  amount_.onDragEnd = [this] { applyPending(); };
  commit_.onClick = [this] { commit(); };
  cancel_.onClick = [this] { cancel(); };
  sync();
  startTimerHz(20);
}

juce::String MorphPanel::amountText(double v) {
  if (v <= 0.0) return "A";
  if (v >= 1.0) return "B";
  return juce::String(juce::roundToInt(v * 100.0)) + "% B";
}

void MorphPanel::resized() {
  auto r = getLocalBounds();
  morph_.setBounds(r.removeFromLeft(80));
  ends_.setBounds(r.removeFromLeft(0));
  amount_.setBounds(r.removeFromLeft(330).reduced(2));
  commit_.setBounds(r.removeFromLeft(110).reduced(2));
  cancel_.setBounds(r.removeFromLeft(110).reduced(2));
}

void MorphPanel::sync() {
  bool both = false, active = false;
  double t = 0.5;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    both = s.pattern() && s.hasMorphSlots();
    active = s.morphActive();
    t = s.morphAmount();
  });
  previewing_ = active;
  syncing_ = true;
  if (morph_.getToggleState() != active) morph_.setToggleState(active, juce::dontSendNotification);
  if (active && !amount_.isMouseButtonDown() && std::abs(amount_.getValue() - t) > 0.005) amount_.setValue(t, juce::dontSendNotification);
  syncing_ = false;
  morph_.setEnabled(both || active);
  amount_.setEnabled(active);
  commit_.setEnabled(active);
  cancel_.setEnabled(active);
  morph_.setTooltip(both || active ? "Blend continuously between the stored patterns A and B. Locks are not enforced inside the blend."
                                   : "Store A and B first.");
}

void MorphPanel::timerCallback() {
  if (dirty_) applyPending();
  sync();
}

void MorphPanel::toggled() {
  if (!morph_.getToggleState()) {
    cancel();
    return;
  }
  cf::Status st;
  const double t = amount_.getValue();
  proc_.withSession([&](cf::composition::ProjectSession& s) { st = s.beginMorph(t); });
  if (!st.ok()) {
    proc_.setStatusMessage(st.error().message);
    morph_.setToggleState(false, juce::dontSendNotification);
    return;
  }
  dirty_ = false;
  proc_.setStatusMessage("Morphing " + juce::String(juce::roundToInt(t * 100.0)) + "% toward B. Commit Morph to keep it, or Cancel Morph.");
  sync();
}

void MorphPanel::applyPending() {
  dirty_ = false;
  cf::Status st;
  const double t = amount_.getValue();
  bool active = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    active = s.morphActive();
    if (active) st = s.setMorph(t);
  });
  if (!active) return;
  ++applied_;
  proc_.setStatusMessage(st.ok() ? "Morphing " + juce::String(juce::roundToInt(t * 100.0)) + "% toward B. Commit Morph to keep it, or Cancel Morph."
                                 : juce::String(st.error().message));
}

void MorphPanel::commit() {
  applyPending();
  cf::Status st;
  juce::String note;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    st = s.commitMorph();
    for (const auto& n : s.takeNotices()) note = juce::String(n.text);
  });
  proc_.setStatusMessage(st.ok() ? (note.isNotEmpty() ? note : juce::String("Committed the morph as a new variation.")) : juce::String(st.error().message));
  sync();
}

void MorphPanel::cancel() {
  juce::String note;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    s.cancelMorph();
    for (const auto& n : s.takeNotices()) note = juce::String(n.text);
  });
  dirty_ = false;
  if (note.isNotEmpty()) proc_.setStatusMessage(note);
  sync();
}
