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

CHOP_TEST(arrangement_rows_are_edited_filled_and_exported_with_progress_and_cancel) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  captureTwoScenes(rig.proc);
  rig.ed->openDrawer("export");
  rig.ed->exportDrawer().refresh();
  pump(60);
  auto& d = rig.ed->exportDrawer();
  CHECK(d.arrangementView().rows.empty());
  auto* add = findButton(rig.ed->exportDrawer(), "Add");
  auto* autoFill = findButton(rig.ed->exportDrawer(), "Auto fill 16 bars");
  auto* exportArr = findButton(rig.ed->exportDrawer(), "Export arrangement");
  auto* remove = findButton(rig.ed->exportDrawer(), "Remove");
  auto* up = findButton(rig.ed->exportDrawer(), "Up");
  CHECK(add && autoFill && exportArr && remove && up);
  if (!(add && autoFill && exportArr && remove && up)) return;
  CHECK(!exportArr->isEnabled());  // an empty arrangement cannot be exported
  CHECK(!remove->isEnabled());
  add->onClick();
  add->onClick();
  CHECK_EQ(d.arrangementView().rows.size(), 2u);
  CHECK(exportArr->isEnabled());
  up->onClick();  // the new selection is the second row, which moves up
  rig.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK_EQ(s.arrangement().sections.size(), 2u); });
  remove->onClick();
  CHECK_EQ(d.arrangementView().rows.size(), 1u);
  autoFill->onClick();
  CHECK(d.arrangementView().rows.size() >= 2u);

  // Export through the session API, with the folder chosen by the test.
  const juce::File out = user.root.getChildFile("arr");
  out.createDirectory();
  CHECK(rig.ed->commands().startArrangementExport(out.getFullPathName().toStdString()).ok());
  CHECK(waitFor([&] { return d.cancelVisible(); }, 3000) || !rig.ed->commands().arrangementState().running);
  if (d.cancelVisible()) {
    CHECK(!exportArr->isEnabled());
    rig.ed->commands().cancelArrangement();
  }
  CHECK(waitFor([&] { return !d.cancelVisible() && !rig.ed->commands().arrangementState().running; }, 15000));
  CHECK(rig.proc.statusMessage().isNotEmpty());
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
