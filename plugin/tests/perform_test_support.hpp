#pragma once
// Helpers shared by the wave-two panel tests (capture, tempo match, pads, scenes, outputs, arrangement).
#include <chopfractal/composition/project_session.hpp>
#include <memory>
#include <string>

#include "PluginEditor.h"
#include "shell_test_support.hpp"

namespace shelltest {

// An editor at the default size with the drawer open; the panel timers only run in a showing window, so tests refresh by hand.
struct PerformFixture {
  ChopFractalProcessor proc;
  std::unique_ptr<ChopFractalEditor> editor;
  explicit PerformFixture(bool withLoop = false, std::uint64_t seed = 42, int width = 1100, int height = 720) {
    prepare(proc);
    if (withLoop) makeSession(proc, seed);
    editor = std::make_unique<ChopFractalEditor>(proc);
    editor->setSize(width, height);
    editor->perform().commands().confirm = [this](const juce::String&, const juce::String&, const juce::String&, std::function<void()> ok) {
      ++asked;
      if (answer && ok) ok();
    };
  }
  int asked = 0;
  bool answer = true;
};

// Every control a person can operate carries an accessible title and a one-clause tooltip without a final period.
inline void checkDescribed(juce::Component& root, const char* what) {
  forEachControl(root, [&](juce::Component& c) {
    const bool control = dynamic_cast<juce::Button*>(&c) || dynamic_cast<juce::Slider*>(&c) || dynamic_cast<juce::ComboBox*>(&c) ||
                         dynamic_cast<juce::TextEditor*>(&c);
    if (!control) return;
    if (c.getTitle().isEmpty()) {
      std::fprintf(stderr, "%s: control without a title (id '%s')\n", what, c.getComponentID().toRawUTF8());
      CHECK(false);
    }
    auto* client = dynamic_cast<juce::SettableTooltipClient*>(&c);
    const juce::String tip = client ? client->getTooltip() : juce::String();
    // A disabled control may carry the sentence that says why (the view models' wording), which can end in a period.
    if (tip.isEmpty() || (tip.endsWith(".") && c.isEnabled())) {
      std::fprintf(stderr, "%s: control '%s' has no tooltip or a final period: '%s'\n", what, c.getTitle().toRawUTF8(), tip.toRawUTF8());
      CHECK(false);
    }
  });
}

// Button::triggerClick posts a message, so the click lands once the loop has run.
// Waits until the button has actually delivered its click (a fixed short pump races under load: the full suite flaked on it).
inline void tap(juce::Button* b) {
  struct Clicked : juce::Button::Listener {
    bool seen = false;
    void buttonClicked(juce::Button*) override { seen = true; }
  } listener;
  b->addListener(&listener);
  b->triggerClick();
  waitFor([&] { return listener.seen; }, 3000);
  b->removeListener(&listener);
  pump(5);
}

inline void click(juce::Component& root, const juce::String& text) {
  auto* b = findButton(root, text);
  CHECK(b != nullptr);
  if (b) {
    CHECK(b->isEnabled());
    tap(b);
  }
}

// One host block of a 1 kHz sine at the given amplitude; the playhead advances.
inline void feedBlock(ChopFractalProcessor& p, FakePlayHead& ph, std::int64_t& frame, float amp = 0.5f, int block = 512) {
  p.setPlayHead(&ph);
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, block);
  for (int i = 0; i < block; ++i) {
    const float x = amp * std::sin(2.f * 3.14159265f * 1000.f * static_cast<float>(frame + i) / 48000.f);
    buf.setSample(0, i, x);
    buf.setSample(1, i, -x);
  }
  p.processBlock(buf, midi);
  frame += block;
  ph.ppq += static_cast<double>(block) * ph.bpm / (60.0 * 48000.0);
  p.setPlayHead(nullptr);
}

}  // namespace shelltest

namespace shelltest {
// Selects the item with this text (notifying), true when it exists.
inline bool selectItem(juce::ComboBox& box, const juce::String& text) {
  for (int i = 0; i < box.getNumItems(); ++i)
    if (box.getItemText(i) == text) {
      box.setSelectedItemIndex(i, juce::sendNotificationSync);
      return true;
    }
  return false;
}
inline juce::Label* findLabelByTitle(juce::Component& root, const juce::String& title) {
  juce::Label* found = nullptr;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* l = dynamic_cast<juce::Label*>(&c))
      if (l->getTitle() == title) found = l;
  });
  return found;
}
}  // namespace shelltest
