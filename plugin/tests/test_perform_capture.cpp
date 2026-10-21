// Capture drawer (F12 section 12): the registered drawer, the controls and their enable rules driven through the real
// processor, the confirm paths, the meter and the progress/countdown readouts.
#include <cmath>

#include "perform_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

CHOP_TEST(capture_drawer_registers_and_every_control_is_described) {
  if (!haveDisplay()) return;
  PerformFixture f;
  CHECK(f.editor->openDrawer("capture"));
  CHECK(f.editor->openDrawerId() == "capture");
  CHECK(findButton(*f.editor, "Panels") != nullptr);
  auto& panel = f.editor->perform().capture();
  CHECK(panel.isVisible());
  panel.refreshNow();
  checkDescribed(panel, "capture");
  // Nothing armed yet: Arm is the only action on offer.
  CHECK(findButton(panel, "Arm")->isEnabled());
  CHECK(!findButton(panel, "Capture Now")->isEnabled() && !findButton(panel, "Stop Capture")->isEnabled());
  CHECK(!findButton(panel, "Keep Capture")->isEnabled() && !findButton(panel, "Discard Capture")->isEnabled());
  CHECK(!findButton(panel, "Undo Keep")->isVisible());
  CHECK(findCombo(panel, "Capture bars")->isVisible() && !findCombo(panel, "Max length")->isVisible());
  // Free mode swaps the bars box for the max length box.
  findCombo(panel, "Capture mode")->setSelectedId(2, juce::sendNotificationSync);
  CHECK(!findCombo(panel, "Capture bars")->isVisible() && findCombo(panel, "Max length")->isVisible());
  CHECK(panel.choice().mode == cf::capture::CaptureMode::Free && panel.choice().maxSeconds == 30.0);
  findCombo(panel, "Max length")->setSelectedId(3, juce::sendNotificationSync);
  CHECK(panel.choice().maxSeconds == 60.0);
}

CHOP_TEST(capture_buttons_arm_record_stop_keep_and_undo_keep) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("capture");
  auto& panel = f.editor->perform().capture();
  findCombo(panel, "Capture mode")->setSelectedId(2, juce::sendNotificationSync);
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  click(panel, "Arm");
  CHECK(f.proc.captureEngine().status().state == cf::capture::CaptureState::Armed);
  feedBlock(f.proc, ph, frame, 0.4f);
  panel.refreshNow();
  CHECK(findButton(panel, "Capture Now")->isEnabled());
  CHECK(!findButton(panel, "Arm")->isEnabled());
  CHECK(panel.meter().level() > 0.3f);  // the level follows the input before the recording starts
  click(panel, "Capture Now");
  for (int i = 0; i < 60; ++i) feedBlock(f.proc, ph, frame, 0.4f);
  panel.refreshNow();
  CHECK(findButton(panel, "Stop Capture")->isEnabled());
  CHECK(panel.view().stateLabel.rfind("Capturing", 0) == 0);
  click(panel, "Stop Capture");
  feedBlock(f.proc, ph, frame, 0.4f);
  panel.refreshNow();
  CHECK(f.proc.captureEngine().status().state == cf::capture::CaptureState::Captured);
  CHECK(findButton(panel, "Keep Capture")->isEnabled() && findButton(panel, "Discard Capture")->isEnabled());
  CHECK(panel.view().keepNeedsConfirm);  // the project already has a loop and a pattern
  f.answer = false;
  click(panel, "Keep Capture");
  CHECK(f.asked == 1 && f.proc.captureEngine().status().state == cf::capture::CaptureState::Captured);  // Cancel keeps the capture
  f.answer = true;
  click(panel, "Keep Capture");
  CHECK(f.asked == 2);
  CHECK(waitFor([&] { return !f.proc.captureKeepInFlight() && f.proc.captureEngine().status().state == cf::capture::CaptureState::Idle; }));
  panel.refreshNow();
  CHECK(findButton(panel, "Undo Keep")->isVisible());
  bool captured = false;
  f.proc.withSession([&](cf::composition::ProjectSession& s) { captured = s.chopMap()->info().name.rfind("Capture ", 0) == 0; });
  CHECK(captured);
  click(panel, "Undo Keep");
  panel.refreshNow();
  f.proc.withSession([&](cf::composition::ProjectSession& s) { captured = s.chopMap()->info().name.rfind("Capture ", 0) == 0; });
  CHECK(!captured);
  CHECK(!findButton(panel, "Undo Keep")->isVisible());
}

CHOP_TEST(capture_arm_asks_before_discarding_an_unkept_capture_and_discard_clears_it) {
  if (!haveDisplay()) return;
  PerformFixture f;
  f.editor->openDrawer("capture");
  auto& panel = f.editor->perform().capture();
  findCombo(panel, "Capture mode")->setSelectedId(2, juce::sendNotificationSync);
  FakePlayHead ph;
  ph.playing = false;
  std::int64_t frame = 0;
  click(panel, "Arm");
  feedBlock(f.proc, ph, frame);
  click(panel, "Capture Now");
  for (int i = 0; i < 10; ++i) feedBlock(f.proc, ph, frame);
  click(panel, "Stop Capture");
  feedBlock(f.proc, ph, frame);
  panel.refreshNow();
  CHECK(panel.view().armEnabled && panel.view().armNeedsConfirm);
  f.answer = false;
  click(panel, "Arm");
  CHECK(f.asked == 1 && f.proc.captureEngine().status().state == cf::capture::CaptureState::Captured);
  f.answer = true;
  click(panel, "Arm");
  CHECK(f.asked == 2 && f.proc.captureEngine().status().state == cf::capture::CaptureState::Armed);
  click(panel, "Stop Capture");
  feedBlock(f.proc, ph, frame);
  panel.refreshNow();
  CHECK(f.proc.captureEngine().status().state == cf::capture::CaptureState::Idle);
}

CHOP_TEST(capture_shows_a_countdown_progress_and_the_clipped_cue) {
  if (!haveDisplay()) return;
  PerformFixture f;
  f.editor->openDrawer("capture");
  auto& panel = f.editor->perform().capture();
  findCombo(panel, "Capture mode")->setSelectedId(1, juce::sendNotificationSync);
  findCombo(panel, "Capture bars")->setSelectedId(1, juce::sendNotificationSync);  // 1 bar at 120 BPM = 2 s
  FakePlayHead ph;
  ph.playing = true;
  ph.ppq = 3.9;  // just before a bar line
  std::int64_t frame = 0;
  click(panel, "Arm");
  for (int i = 0; i < 20; ++i) feedBlock(f.proc, ph, frame, 1.0f);  // a full-scale sine clips
  panel.refreshNow();
  CHECK(f.proc.captureEngine().status().state == cf::capture::CaptureState::Capturing);
  CHECK(panel.meter().clipped());
  CHECK(panel.view().meterText == "Clipped");
  bool sawCountdown = false;
  forEachControl(panel, [&](juce::Component& c) {
    if (auto* l = dynamic_cast<juce::Label*>(&c))
      if (l->getTitle() == "Capture time") sawCountdown = l->getText().startsWith("-00:");
  });
  CHECK(sawCountdown);
}
