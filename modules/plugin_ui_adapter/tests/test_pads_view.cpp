#include <chopfractal/plugin_ui_adapter/pads_view.hpp>

#include "chop_test.hpp"

using namespace chopfractal::ui;

namespace {
PadUiInput ready(int chops) {
  PadUiInput in;
  in.chopCount = chops;
  in.hasSource = true;
  in.hasPattern = true;
  return in;
}
}  // namespace

CHOP_TEST(pad_note_names_use_c3_as_middle_c) {
  CHECK(padNoteName(60) == "C3");
  CHECK(padNoteName(36) == "C1");
  CHECK(padNoteName(0) == "C-2");
  CHECK(padNoteName(127) == "G8");
  CHECK(padNoteName(61) == "C#3");
  CHECK(padNoteName(-1).empty() && padNoteName(128).empty());
  CHECK(padBaseNoteText(36) == "C1 (36)");
  CHECK(padBaseNoteText(500) == "D#7 (111)");
}

CHOP_TEST(pad_velocity_runs_from_127_at_the_top_to_40_at_the_bottom) {
  CHECK(padVelocityFromY(0, 100) == 127);
  CHECK(padVelocityFromY(100, 100) == 40);
  CHECK(padVelocityFromY(50, 100) == 127 - 44);  // 87 * 0.5 = 43.5 rounds to 44
  CHECK(padVelocityFromY(-20, 100) == 127);
  CHECK(padVelocityFromY(500, 100) == 40);
  CHECK(padVelocityFromY(10, 0) == 127);
}

CHOP_TEST(pad_grid_labels_titles_and_disabled_pads) {
  PadUiInput in = ready(5);
  in.roles = {"kick", "snare", "hat"};
  const PadView v = buildPadView(in);
  CHECK(v.pads.size() == 16);
  CHECK(v.padsEnabled && v.settingsEnabled && v.emptyText.empty());
  CHECK(v.pads[0].name == "Pad 1" && v.pads[0].label == "1" && v.pads[0].role == "kick");
  CHECK(v.pads[0].title == "Pad 1: chop 1, note C1");
  CHECK(v.pads[4].enabled && v.pads[4].note == 40);
  CHECK(!v.pads[5].enabled && v.pads[5].label.empty() && v.pads[5].title == "Pad 6: no chop");
  CHECK(v.mappedChops == 5);
  CHECK(!v.showPage && v.pageCount == 1 && v.pageItems.size() == 1 && v.pageItems[0] == "Chops 1-5");
}

CHOP_TEST(pad_pages_and_notes_beyond_127) {
  PadUiInput in = ready(40);
  in.page = 2;
  PadView v = buildPadView(in);
  CHECK(v.showPage && v.pageCount == 3 && v.page == 2);
  CHECK(v.pageItems[1] == "Chops 17-32" && v.pageItems[2] == "Chops 33-40");
  CHECK(v.pads[0].chop == 32 && v.pads[7].chop == 39 && v.pads[8].chop == -1);
  in.page = 99;
  CHECK(buildPadView(in).page == 2);
  in.chopCount = 100;
  in.page = 0;
  in.baseNote = 111;
  v = buildPadView(in);
  CHECK(v.pads[15].chop == 15 && v.pads[15].enabled && v.pads[15].note == 126);
  in.page = 1;
  v = buildPadView(in);
  CHECK(v.pads[0].enabled && v.pads[0].note == 127);  // chop 16
  CHECK(!v.pads[1].enabled && v.pads[1].title.find("lower the base note") != std::string::npos);
  CHECK(v.mappedChops == 1);
}

CHOP_TEST(pad_view_enable_rules_for_source_pattern_record_and_take) {
  PadUiInput in;
  PadView v = buildPadView(in);
  CHECK(!v.padsEnabled && v.emptyText == kPadLoadFirstText && !v.recordEnabled && !v.discardEnabled);
  in = ready(4);
  in.hasPattern = false;
  v = buildPadView(in);
  CHECK(v.padsEnabled && !v.recordEnabled && v.recordHint == kPadNoPatternText);
  in.hasPattern = true;
  v = buildPadView(in);
  CHECK(v.recordEnabled && v.discardLabel == "Discard Take" && !v.discardEnabled);
  in.recordArmed = true;
  CHECK(buildPadView(in).recordHint == kPadArmedText && buildPadView(in).recordOn);
  in.hostPlaying = true;
  in.hostHasPosition = false;
  CHECK(buildPadView(in).recordHint == kPadNoPositionText);
  in.takeHits = 1;
  CHECK(buildPadView(in).discardLabel == "1 hit waiting" && buildPadView(in).discardEnabled);
  in.takeHits = 5;
  CHECK(buildPadView(in).discardLabel == "5 hits waiting");
}

CHOP_TEST(pad_lit_follows_the_activity_bits) {
  PadUiInput in = ready(4);
  in.sounding[37 >> 5] |= 1u << (37 & 31);
  const PadView v = buildPadView(in);
  CHECK(!v.pads[0].lit && v.pads[1].lit);
}

CHOP_TEST(pad_combo_items_and_quantize_mapping) {
  CHECK(padMidiChannelItems().size() == 17 && padMidiChannelItems()[0] == "Omni" && padMidiChannelItems()[16] == "16");
  for (int i = 0; i < 6; ++i) {
    const PadQuantize q = padQuantizeFromIndex(i);
    CHECK(padQuantizeIndex(q.division, q.triplet) == i);
  }
  CHECK(padQuantizeFromIndex(99).division == 16 && !padQuantizeFromIndex(-1).triplet);
  CHECK(padChokeMenuItems().size() == 5 && padChokeMenuItems()[0] == "Choke group: Off" && padChokeMenuItems()[4] == "Choke group 4");
}

CHOP_TEST(pad_take_notice_reads_like_the_spec) {
  PadTakeCounts c;
  CHECK(padTakeNotice(c).empty());
  c.added = 5;
  CHECK(padTakeNotice(c) == "Recorded 5 hits.");
  c.added = 1;
  CHECK(padTakeNotice(c) == "Recorded 1 hit.");
  c.added = 5;
  c.skippedLocked = 2;
  CHECK(padTakeNotice(c) == "Recorded 5 hits. 2 skipped: their beat is locked.");
  c.skippedLocked = 0;
  c.skippedCap = 1;
  CHECK(padTakeNotice(c) == "Recorded 5 hits. 1 skipped: the event cap (512) is reached.");
  c.skippedCap = 0;
  c.skippedUnmapped = 3;
  CHECK(padTakeNotice(c) == "Recorded 5 hits. 3 skipped: they matched no chop.");
  c.skippedDuplicate = 2;
  CHECK(padTakeNotice(c).find("2 skipped: already in the pattern.") != std::string::npos);
  PadTakeCounts only;
  only.skippedLocked = 3;
  CHECK(padTakeNotice(only) == "Recorded 0 hits. 3 skipped: their beat is locked.");
}
