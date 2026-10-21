// Export drawer: settings persist with the project, MIDI options reach the kit and the dragged files, the drag handles
// produce files, and the arrangement list is edited and exported through the F18 session API with progress and Cancel.
#include <utility>

#include "workflow_test_support.hpp"

using namespace shelltest;

namespace {
void captureTwoScenes(ChopFractalProcessor& p) {
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.captureScene("Verse").ok());
    CHECK(s.captureScene("Chorus").ok());
  });
}
}  // namespace

CHOP_TEST(export_drawer_settings_persist_in_the_project_and_drive_the_estimate) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  rig.ed->openDrawer("export");
  pump(60);
  auto& d = rig.ed->exportDrawer();
  CHECK(d.isVisible() && rig.ed->openDrawerId() == "export");
  auto* format = findCombo(rig.ed->exportDrawer(), "Export format");
  auto* rate = findCombo(rig.ed->exportDrawer(), "Export sample rate");
  auto* loops = findCombo(rig.ed->exportDrawer(), "Export loops");
  CHECK(format && rate && loops);
  if (!(format && rate && loops)) return;
  format->setSelectedId(3, juce::sendNotificationSync);
  rate->setSelectedId(4, juce::sendNotificationSync);
  loops->setSelectedId(3, juce::sendNotificationSync);
  rig.proc.withSession([&](cf::composition::ProjectSession& s) {
    CHECK_EQ(s.extras().exportUi.formatIndex, 2);
    CHECK_EQ(s.extras().exportUi.rateIndex, 3);
    CHECK_EQ(s.extras().exportUi.loopsIndex, 2);
  });
  CHECK(d.exportView().estimate.find("MB") != std::string::npos || d.exportView().estimate.find("KB") != std::string::npos);
  CHECK(d.exportView().wavEnabled && d.exportView().kitEnabled);
  juce::MemoryBlock state;
  rig.proc.getStateInformation(state);
  ChopFractalProcessor other;
  prepare(other);
  other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  waitForRestore(other);
  other.withSession([&](cf::composition::ProjectSession& s) {
    CHECK_EQ(s.extras().exportUi.formatIndex, 2);
    CHECK_EQ(s.extras().exportUi.rateIndex, 3);
  });
}

CHOP_TEST(midi_options_flow_into_the_dragged_midi_and_the_kit) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  rig.ed->openDrawer("export");
  pump(60);
  auto* base = findSlider(rig.ed->exportDrawer(), "MIDI base note");
  auto* curve = findCombo(rig.ed->exportDrawer(), "MIDI velocity curve");
  auto* fixed = findSlider(rig.ed->exportDrawer(), "MIDI fixed velocity");
  auto* ch = findCombo(rig.ed->exportDrawer(), "MIDI channel");
  CHECK(base && curve && fixed && ch);
  if (!(base && curve && fixed && ch)) return;
  CHECK(!fixed->isEnabled());  // only the Fixed curve uses it
  base->setValue(48, juce::sendNotificationSync);
  curve->setSelectedId(4, juce::sendNotificationSync);
  CHECK(fixed->isEnabled());
  fixed->setValue(90, juce::sendNotificationSync);
  ch->setSelectedId(10, juce::sendNotificationSync);
  rig.proc.withSession([&](cf::composition::ProjectSession& s) {
    const auto& m = s.extras().exportUi.midi;
    CHECK_EQ(m.baseNote, 48);
    CHECK_EQ(static_cast<int>(m.curve), 3);
    CHECK_EQ(m.fixedVelocity, 90);
    CHECK_EQ(m.channel, 9);
  });
  const auto mo = rig.ed->commands().midiOptions();
  CHECK_EQ(mo.baseNote, 48);
  const juce::File midi = rig.ed->makeDragFile(false);
  CHECK(midi.existsAsFile() && midi.getSize() > 20);
  juce::MemoryBlock bytes;
  midi.loadFileAsData(bytes);
  bool sawVelocity = false;  // a note-on on channel 10 (0x99) with velocity 90
  const auto* b = static_cast<const unsigned char*>(bytes.getData());
  for (std::size_t i = 0; i + 2 < bytes.getSize(); ++i)
    if (b[i] == 0x99 && b[i + 2] == 90) sawVelocity = true;
  CHECK(sawVelocity);
  auto set = rig.ed->commands().makeKitDragSet();
  CHECK(set != nullptr);
}

CHOP_TEST(drag_handles_produce_files_and_follow_the_pattern_state) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig(false);
  if (!rig.ed) return;
  rig.ed->openDrawer("export");
  pump(60);
  auto& d = rig.ed->exportDrawer();
  CHECK(!d.dragMidi().isEnabled() && !d.dragWav().isEnabled() && !d.dragKit().isEnabled());  // no pattern yet
  CHECK(!d.exportView().wavEnabled);
  CHECK(d.dragMidi().getTooltip().length() > 20);
  CHECK(d.dragMidi().getTooltip().isNotEmpty() && !d.dragMidi().getTooltip().endsWithChar('.'));
  makeSession(rig.proc);
  rig.ed->commands().say("refresh");
  pump(200);
  CHECK(d.dragMidi().isEnabled() && d.dragWav().isEnabled() && d.dragKit().isEnabled());
  const auto wav = rig.ed->makeDragFile(true);
  CHECK(wav.existsAsFile() && wav.getFileExtension() == ".wav");
}

// The arrangement is edited and exported in the Arrange drawer only; the export drawer has no second copy of it.
CHOP_TEST(arrangement_lives_in_the_arrange_drawer_and_not_in_the_export_drawer) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  captureTwoScenes(rig.proc);
  rig.ed->openDrawer("export");
  rig.ed->exportDrawer().refresh();
  pump(60);
  for (const char* name : {"Add", "Remove", "Up", "Down", "Use scene chain", "Auto fill 16 bars", "Export arrangement", "Export Arrangement"})
    CHECK(findButton(rig.ed->exportDrawer(), name) == nullptr);
  CHECK(findCombo(rig.ed->exportDrawer(), "Arrangement transition") == nullptr);
  rig.ed->openDrawer("arrange");
  pump(60);
  CHECK(findButton(rig.ed->perform().arrangement(), "Export Arrangement") != nullptr);
  CHECK(findButton(rig.ed->perform().arrangement(), "Auto fill 16 bars") != nullptr);
}

CHOP_TEST(export_drawer_is_laid_out_at_both_sizes_and_every_control_has_a_tooltip) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  for (auto size : {std::pair<int, int>{1100, 720}, std::pair<int, int>{880, 560}}) {
    EditorRig rig(true, size.first, size.second);
    if (!rig.ed) return;
    captureTwoScenes(rig.proc);
    rig.ed->openDrawer("export");
    rig.ed->exportDrawer().refresh();
    pump(120);
    forEachControl(rig.ed->exportDrawer(), [&](juce::Component& c) {
      if (dynamic_cast<juce::Label*>(&c) || dynamic_cast<juce::ScrollBar*>(&c)) return;
      if (c.getTitle().isEmpty() && dynamic_cast<juce::TextButton*>(&c)) CHECK(false);
      if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c)) CHECK(t->getTooltip().isNotEmpty() || c.getWidth() == 0);
    });
    snapshot(*rig.ed, "export_drawer");
  }
}
