// Live gestures (F08) through the real processor and the Gestures drawer: a held button reaches the renderer, MIDI CC does the
// same, a closed drawer lets go, and the settings travel with the project.
#include "GesturePanel.h"
#include "perform_test_support.hpp"
#include "workflow_test_support.hpp"

using namespace shelltest;
namespace rg = chopfractal::render;

namespace {
bool finiteBlock(ChopFractalProcessor& p, FakePlayHead& ph, std::int64_t& frame, const juce::MidiBuffer& midiIn = {}) {
  p.setPlayHead(&ph);
  juce::MidiBuffer midi(midiIn);
  juce::AudioBuffer<float> buf(2, 512);
  buf.clear();
  p.processBlock(buf, midi);
  frame += 512;
  ph.ppq += 512.0 * ph.bpm / (60.0 * 48000.0);
  p.setPlayHead(nullptr);
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < 512; ++i)
      if (!std::isfinite(buf.getSample(c, i))) return false;
  return true;
}
}  // namespace

CHOP_TEST(a_held_gesture_button_reaches_the_renderer_and_release_lets_go) {
  if (!haveDisplay()) return;
  EditorRig rig(true);
  if (!rig.ed) return;
  CHECK(rig.ed->openDrawer("gestures"));
  pump(100);
  auto& panel = rig.ed->gesturePanel();
  FakePlayHead ph;
  std::int64_t frame = 0;
  const struct {
    juce::Button* b;
    rg::GestureKind kind;
  } rows[] = {{&panel.stutterButton(), rg::GestureKind::Stutter}, {&panel.reverseButton(), rg::GestureKind::Reverse}, {&panel.tapeStopButton(), rg::GestureKind::TapeStop}};
  for (const auto& row : rows) {
    CHECK(!rig.proc.gestureActive(row.kind));
    row.b->setState(juce::Button::buttonDown);
    CHECK(finiteBlock(rig.proc, ph, frame));
    if (row.kind != rg::GestureKind::Stutter) CHECK(rig.proc.gestureActive(row.kind));  // Stutter needs a hit to freeze
    row.b->setState(juce::Button::buttonNormal);
    CHECK(finiteBlock(rig.proc, ph, frame));
    CHECK(!rig.proc.gestureActive(row.kind));
  }
}

CHOP_TEST(closing_the_drawer_releases_a_held_gesture) {
  if (!haveDisplay()) return;
  EditorRig rig(true);
  if (!rig.ed) return;
  CHECK(rig.ed->openDrawer("gestures"));
  pump(100);
  FakePlayHead ph;
  std::int64_t frame = 0;
  rig.ed->gesturePanel().reverseButton().setState(juce::Button::buttonDown);
  CHECK(finiteBlock(rig.proc, ph, frame));
  CHECK(rig.proc.gestureActive(rg::GestureKind::Reverse));
  rig.ed->closeDrawer();
  pump(100);
  CHECK(finiteBlock(rig.proc, ph, frame));
  CHECK(!rig.proc.gestureActive(rg::GestureKind::Reverse));
}

CHOP_TEST(midi_cc_20_to_22_hold_and_release_the_gestures) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  FakePlayHead ph;
  std::int64_t frame = 0;
  juce::MidiBuffer on, off;
  on.addEvent(juce::MidiMessage::controllerEvent(1, 21, 127), 100);
  on.addEvent(juce::MidiMessage::controllerEvent(1, 22, 100), 200);
  on.addEvent(juce::MidiMessage::controllerEvent(1, 23, 127), 300);  // not a gesture
  off.addEvent(juce::MidiMessage::controllerEvent(1, 21, 0), 50);
  off.addEvent(juce::MidiMessage::controllerEvent(1, 22, 63), 60);
  CHECK(finiteBlock(p, ph, frame, on));
  CHECK(p.gestureActive(rg::GestureKind::Reverse) && p.gestureActive(rg::GestureKind::TapeStop) && !p.gestureActive(rg::GestureKind::Stutter));
  CHECK(finiteBlock(p, ph, frame, off));
  CHECK(!p.gestureActive(rg::GestureKind::Reverse) && !p.gestureActive(rg::GestureKind::TapeStop));
}

CHOP_TEST(gesture_settings_move_the_processor_and_travel_with_the_project) {
  if (!haveDisplay()) return;
  juce::MemoryBlock saved;
  {
    EditorRig rig(true);
    if (!rig.ed) return;
    CHECK(rig.ed->openDrawer("gestures"));
    pump(100);
    auto& panel = rig.ed->gesturePanel();
    CHECK(selectItem(panel.rateBox(), "1/32"));
    panel.tapeStopSlider().setValue(900.0, juce::sendNotificationSync);
    panel.spinUpSlider().setValue(60.0, juce::sendNotificationSync);
    CHECK_EQ(rig.proc.gestureStutterRate.load(), 2);
    CHECK_NEAR(rig.proc.gestureTapeStopMs.load(), 900.0, 0.5);
    CHECK_NEAR(rig.proc.gestureSpinUpMs.load(), 60.0, 0.5);
    rig.proc.getStateInformation(saved);
  }
  ChopFractalProcessor b;
  prepare(b);
  b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  CHECK(waitForRestore(b));
  CHECK_EQ(b.gestureStutterRate.load(), 2);
  CHECK_NEAR(b.gestureTapeStopMs.load(), 900.0, 0.5);
  CHECK_NEAR(b.gestureSpinUpMs.load(), 60.0, 0.5);
  std::unique_ptr<ChopFractalEditor> ed = std::make_unique<ChopFractalEditor>(b);
  ed->setSize(1100, 720);
  CHECK_EQ(ed->gesturePanel().rateBox().getSelectedId(), 3);
}

CHOP_TEST(gestures_drawer_controls_are_described_and_reachable_from_the_palette) {
  if (!haveDisplay()) return;
  EditorRig rig(false);
  if (!rig.ed) return;
  CHECK(rig.ed->openDrawer("gestures"));
  pump(100);
  checkDescribed(rig.ed->gesturePanel(), "gestures");
  CHECK(rig.ed->registry().find("action.gestures") != nullptr);
}
