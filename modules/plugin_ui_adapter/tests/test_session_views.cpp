#include <chopfractal/plugin_ui_adapter/session_views.hpp>

#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal::ui;

CHOP_TEST(midi_note_names_follow_the_spec_examples) {
  CHECK(midiNoteName(36) == "C2");
  CHECK(midiNoteText(36) == "C2 (36)");
  CHECK(midiNoteName(60) == "C4");
  CHECK(midiNoteName(61) == "C#4");
  CHECK(midiNoteName(0) == "C-1" && midiNoteName(127) == "G9");
  CHECK(midiNoteName(-1) == "?" && midiNoteName(128) == "?");
}

CHOP_TEST(midi_options_view_clamps_to_the_chops_and_enables_fixed_only_for_fixed) {
  MidiOptionsInput in;
  in.chopCount = 8;
  in.baseNote = 127;
  MidiOptionsView v = buildMidiOptionsView(in);
  CHECK(v.baseNoteMax == 120 && v.baseNote == 120);
  CHECK(!v.fixedVelocityEnabled && v.curveItems.size() == 4 && v.curveItems[0] == "Linear");
  in.curve = 3;
  in.fixedVelocity = 0;
  in.loops = 8;
  in.channel = 9;
  v = buildMidiOptionsView(in);
  CHECK(v.fixedVelocityEnabled && v.fixedVelocity == 1 && v.loopsIndex == 3 && v.channelIndex == 9);
  CHECK(v.channelItems.size() == 16 && v.channelItems[0] == "1" && v.channelItems[15] == "16");
  in.chopCount = 200;
  CHECK(buildMidiOptionsView(in).baseNoteMax == 0);
  in.chopCount = 0;
  CHECK(buildMidiOptionsView(in).baseNoteMax == 128);
}

CHOP_TEST(tempo_line_names_host_or_manual_tempo) {
  MidiOptionsInput in;
  in.bpm = 120;
  CHECK(buildMidiOptionsView(in).tempoLine == "Tempo in the file: 120.0 BPM (host tempo)");
  in.fallbackTempo = true;
  in.bpm = 93.25;
  CHECK(buildMidiOptionsView(in).tempoLine == "Tempo in the file: 93.2 BPM (manual tempo)" || buildMidiOptionsView(in).tempoLine == "Tempo in the file: 93.3 BPM (manual tempo)");
}

CHOP_TEST(loops_index_picks_the_nearest_choice) {
  CHECK(loopsIndexFor(1) == 0 && loopsIndexFor(2) == 1 && loopsIndexFor(3) == 1 && loopsIndexFor(16) == 4 && loopsIndexFor(99) == 4);
}

CHOP_TEST(drag_handles_are_disabled_without_a_pattern_and_enable_after) {
  DragOutInput in;
  DragOutView v = buildDragOutView(in);
  CHECK(!v.midi.enabled && !v.wav.enabled && !v.kit.enabled);
  CHECK(v.midi.tooltip == "Generate a pattern first." && v.kit.tooltip == "Generate a pattern first.");
  in.hasPattern = true;
  in.chopCount = 8;
  in.wavSeconds = 4.0;
  v = buildDragOutView(in);
  CHECK(v.midi.enabled && v.wav.enabled && v.kit.enabled);
  CHECK(v.midi.tooltip == "Drag the current groove into your DAW as a MIDI file. Chop 1 plays note 36 (C2).");
  CHECK(v.wav.tooltip == "Drag the current groove into your DAW as a WAV file.");
  CHECK(v.kit.tooltip == "Drag the slice WAVs and the MIDI file into a sampler or drum rack.");
}

CHOP_TEST(drag_kit_refuses_more_than_63_chops_and_a_missing_source_keeps_midi) {
  DragOutInput in;
  in.hasPattern = true;
  in.chopCount = 64;
  DragOutView v = buildDragOutView(in);
  CHECK(!v.kit.enabled && v.kit.tooltip == "Too many chops to drag (64 max). Use Export Kit." && v.midi.enabled && v.wav.enabled);
  in.chopCount = 63;
  CHECK(buildDragOutView(in).kit.enabled);
  in.sourceMissing = true;
  v = buildDragOutView(in);
  CHECK(v.midi.enabled && !v.wav.enabled && !v.kit.enabled);
  CHECK(v.wav.tooltip == "The audio for this project is missing; MIDI can still be dragged, audio cannot.");
}

CHOP_TEST(wav_drag_is_refused_over_120_seconds) {
  CHECK(dragWavSeconds(3840, 120.0, 4, 0.5) > 8.4 && dragWavSeconds(3840, 120.0, 4, 0.5) < 8.6);  // 4 loops of 1 bar at 120 BPM + tail
  CHECK(dragWavSeconds(3840, 0.0, 4, 0.5) == 0.0);
  DragOutInput in;
  in.hasPattern = true;
  in.chopCount = 4;
  in.wavSeconds = 120.5;
  DragOutView v = buildDragOutView(in);
  CHECK(!v.wav.enabled && v.wav.tooltip == dragTooLongMessage() && v.midi.enabled);
  in.wavSeconds = 120.0;
  CHECK(buildDragOutView(in).wav.enabled);
}

CHOP_TEST(drag_file_names_and_messages_are_exact) {
  CHECK(dragFileName("groove", "12", 120.0, "mid") == "chopfractal_groove_v12_120bpm.mid");
  CHECK(dragFileName("x", "current", 93.6, "wav") == "chopfractal_x_vcurrent_94bpm.wav");
  CHECK(dragStartedMessage("a.mid") == "Dragging a.mid");
  CHECK(dragClickMessage() == "Drag this button onto a DAW track.");
  CHECK(dragRenderErrorMessage("boom") == "Could not render the MIDI file: boom");
  CHECK(dragSkippedEventsMessage(3) == "3 events were skipped because their chops no longer exist.");
  CHECK(dragSkippedEventsMessage(1) == "1 event was skipped because their chops no longer exist.");
  CHECK(dragHintText("/d") == "Dragged files are kept in /d until you delete them. Use Export for files you want to keep.");
  CHECK(dragTempFileErrorMessage() == "Could not create a temporary file. Use Export instead.");
}

// ---- F10 ----

CHOP_TEST(morph_amount_text_and_view_follow_the_spec) {
  CHECK(morphAmountText(0.0) == "A" && morphAmountText(1.0) == "B" && morphAmountText(0.37) == "37% B" && morphAmountText(0.5) == "50% B");
  CHECK(morphAmountText(-1) == "A" && morphAmountText(2) == "B");
  MorphInput in;
  MorphView v = buildMorphView(in);
  CHECK(!v.toggleEnabled && v.toggleTooltip == "Store A and B first." && !v.sliderEnabled && !v.badgeVisible && v.status.empty());
  in.hasA = in.hasB = true;
  v = buildMorphView(in);
  CHECK(v.toggleEnabled && !v.commitEnabled && !v.cancelEnabled && !v.toggleOn);
  in.active = true;
  in.amount = 0.37;
  v = buildMorphView(in);
  CHECK(v.toggleOn && v.sliderEnabled && v.commitEnabled && v.cancelEnabled && v.badgeVisible && v.badgeText == "Morph preview");
  CHECK(v.status == "Morphing 37% toward B. Commit Morph to keep it, or Cancel Morph." && v.sliderText == "37% B");
  in.active = false;
  in.scenesPlaying = true;
  v = buildMorphView(in);
  CHECK(!v.toggleEnabled && v.toggleTooltip == "Cancel the scene playback before morphing.");
  CHECK(morphCommittedMessage() == "Committed the morph as a new variation." && morphCancelledMessage() == "Morph cancelled; your pattern is unchanged." &&
        morphEndedMessage() == "Morph ended because the pattern was changed.");
}

CHOP_TEST(slider_coalesces_rapid_changes) {
  MorphCoalescer c;
  c.reset(0.5);
  int applications = 0;
  double v = 0;
  for (int i = 0; i < 50; ++i) {  // 50 slider callbacks within 20 ms, polled every callback
    c.setPending(0.5 + 0.005 * (i + 1));
    if (c.take(i * 20 / 50, v)) ++applications;
  }
  CHECK(applications <= 1);
  c.release();
  CHECK(c.take(21, v) && std::fabs(v - 0.75) < 1e-9);
  CHECK(!c.take(22, v));
  // slow drag: one application per 50 ms at most, and only for real changes
  MorphCoalescer d;
  d.reset(0.0);
  int n = 0;
  for (long long t = 0; t <= 500; t += 10) {
    d.setPending(static_cast<double>(t) / 1000.0);
    if (d.take(t, v)) ++n;
  }
  CHECK(n >= 5 && n <= 11);
  // below the minimum delta nothing is applied, but release applies the final value if it differs at all
  MorphCoalescer e;
  e.reset(0.5);
  e.setPending(0.502);
  CHECK(!e.take(1000, v));
  e.release();
  CHECK(e.take(1001, v) && std::fabs(v - 0.502) < 1e-12);
  MorphCoalescer f;
  f.reset(0.5);
  f.setPending(0.5);
  f.release();
  CHECK(!f.take(5000, v));
}

CHOP_TEST(scene_row_text_matches_the_spec_examples) {
  SceneRowInput r;
  r.number = 3;
  r.name = "Evolve step 2";
  r.loops = 4;
  CHECK(sceneRowText(r) == "3  Evolve step 2   4 loops   > next");
  r.next = 1;
  CHECK(sceneRowText(r) == "3  Evolve step 2   4 loops   > repeat");
  r.next = 2;
  r.loops = 1;
  CHECK(sceneRowText(r) == "3  Evolve step 2   1 loop   > stop");
  r.next = 3;
  r.jumpNumber = 5;
  CHECK(sceneRowText(r) == "3  Evolve step 2   1 loop   > jump to 5");
  r.loops = 4;
  r.playing = true;
  r.loopIndex = 1;
  CHECK(sceneRowText(r) == "3  Evolve step 2   4 loops   > jump to 5  [Playing, loop 2 of 4]");
  r.playing = false;
  r.armed = true;
  CHECK(sceneRowText(r) == "3  Evolve step 2   4 loops   > jump to 5  [Next]");
  r.armed = false;
  r.fits = false;
  CHECK(sceneRowText(r) == "3  Evolve step 2   4 loops   > jump to 5 (does not fit the chops)");
}

CHOP_TEST(scene_status_text_covers_every_state) {
  SceneStatusInput st;
  CHECK(sceneStatusText(st, false, 7) == "Scenes stopped");
  st.recording = true;
  CHECK(sceneStatusText(st, false, 7) == "Recording scenes: waiting for Evolve");
  CHECK(sceneStatusText(st, true, 7) == "Recording scenes: 7 captured");
  st = SceneStatusInput{};
  st.running = true;
  st.playingNumber = 3;
  st.loopIndex = 1;
  st.loops = 4;
  CHECK(sceneStatusText(st, false, 7) == "Scene 3 of 7, loop 2 of 4");
  st.waitingForTransport = true;
  CHECK(sceneStatusText(st, false, 7) == "Waiting for the host to play");
  st = SceneStatusInput{};
  st.running = true;
  st.armedNumber = 2;
  CHECK(sceneStatusText(st, false, 7) == "Scene 2 of 7 starts at the next loop");
  st = SceneStatusInput{};
  st.holding = true;
  st.playingNumber = 7;
  CHECK(sceneStatusText(st, false, 7) == "Sequence finished: Scene 7 keeps looping");
}

CHOP_TEST(scene_view_enable_rules) {
  SceneViewInput in;
  CHECK(buildSceneView(in).emptyText == "No scenes yet. Press Capture Scene, or turn on Rec Scenes and start Evolve.");
  auto v = buildSceneView(in);
  CHECK(!v.captureEnabled && !v.recEnabled && !v.playEnabled && !v.clearEnabled && !v.exportEnabled && !v.stopEnabled);
  in.hasPattern = in.hasSource = true;
  v = buildSceneView(in);
  CHECK(v.captureEnabled && v.recEnabled && !v.playEnabled);
  for (int i = 0; i < 3; ++i) {
    SceneRowInput r;
    r.number = i + 1;
    r.name = "S" + std::to_string(i + 1);
    in.rows.push_back(r);
  }
  in.selected = 1;
  v = buildSceneView(in);
  CHECK(v.rows.size() == 3 && v.emptyText.empty());
  CHECK(v.playEnabled && v.moveUpEnabled && v.moveDownEnabled && v.deleteEnabled && v.clearEnabled && v.exportEnabled && v.updateEnabled && v.keepEnabled);
  in.selected = 0;
  CHECK(!buildSceneView(in).moveUpEnabled);
  in.selected = 2;
  CHECK(!buildSceneView(in).moveDownEnabled);
  in.selected = -1;
  v = buildSceneView(in);
  CHECK(!v.deleteEnabled && !v.updateEnabled && !v.keepEnabled && v.clearEnabled);
  // While scenes play: Delete, Clear All and Move are off with the tooltip; Update and Keep stay on (they stop playback first).
  in.selected = 1;
  in.status.running = true;
  v = buildSceneView(in);
  CHECK(!v.deleteEnabled && !v.clearEnabled && !v.moveUpEnabled && !v.moveDownEnabled && !v.playEnabled);
  CHECK(v.updateEnabled && v.keepEnabled && v.stopEnabled && v.playOn);
  CHECK(v.editTooltip == "Stop scene playback first.");
  in.status.running = false;
  in.status.holding = true;
  CHECK(buildSceneView(in).stopEnabled && buildSceneView(in).deleteEnabled);
}

CHOP_TEST(scene_messages_are_exact) {
  CHECK(sceneCapturedMessage(3) == "Captured Scene 3.");
  CHECK(sceneFullMessage() == "The scene list is full (32). Delete a scene to capture more.");
  CHECK(sceneRecordingFullMessage() == "Scene recording stopped: the list is full (32).");
  CHECK(sceneKeptMessage(3) == "Kept Scene 3 as a new variation.");
  CHECK(sceneExportedMessage(7, "/tmp/x") == "Exported 7 scenes to /tmp/x.");
  CHECK(sceneExportedMessage(1, "/tmp/x") == "Exported 1 scene to /tmp/x.");
  CHECK(sceneClearPromptTitle() == "Delete all scenes?" && sceneClearPromptBody() == "This cannot be undone.");
}

CHOP_TEST(arrangement_row_and_total_text_match_the_spec_examples) {
  ArrangementRowInput r;
  r.sceneNumber = 3;
  r.sceneName = "Verse";
  r.bars = 4;
  r.repeats = 2;
  CHECK(arrangementRowText(1, r) == "1  Scene 3 \"Verse\"  4 bars x 2   8 bars");
  r.sceneName = "Scene 3";
  CHECK(arrangementRowText(2, r) == "2  Scene 3  4 bars x 2   8 bars");
  r.bars = 1;
  r.repeats = 1;
  CHECK(arrangementRowText(3, r) == "3  Scene 3  1 bar x 1   1 bar");
  r.fits = false;
  CHECK(arrangementRowText(3, r) == "3  Scene 3  1 bar x 1   1 bar (does not fit the chops)");
  ArrangementRowInput d;
  d.deleted = true;
  d.bars = 2;
  d.repeats = 4;
  d.fits = false;
  CHECK(arrangementRowText(4, d) == "4  Scene deleted  2 bars x 4   8 bars");
  CHECK(arrangementTotalText(16, 120.0) == "Total: 16 bars (32.0 s at 120 BPM)");
  CHECK(arrangementTotalText(8, 133.0) == "Total: 8 bars (14.4 s at 133 BPM)");
  CHECK(arrangementTotalText(6, 120.0) == "Total: 6 bars (needs 8 to 32)");
  CHECK(arrangementTotalText(33, 120.0) == "Total: 33 bars (needs 8 to 32)");
  CHECK(arrangementTotalText(32, 120.4) == "Total: 32 bars (64.0 s at 120 BPM)");
}

CHOP_TEST(arrangement_bar_choices_and_truncation_note) {
  CHECK((arrangementBarChoices(2) == std::vector<int>{2, 4, 8}));
  CHECK((arrangementBarChoices(1) == std::vector<int>{1, 2, 4, 8}));
  CHECK((arrangementBarChoices(4) == std::vector<int>{4, 8}));
  CHECK((arrangementBarChoices(8) == std::vector<int>{8}));
  CHECK((arrangementBarChoices(3) == std::vector<int>{1, 2, 4, 8}));
  CHECK((arrangementBarChoices(16) == std::vector<int>{1, 2, 4, 8}));
  CHECK(arrangementTruncationNote(2, 4, 2) == "Scene 2 has 4 bars; using the first 2");
  CHECK(arrangementTruncationNote(2, 4, 4).empty() && arrangementTruncationNote(2, 2, 8).empty());
}

CHOP_TEST(arrangement_view_enable_rules) {
  ArrangementViewInput in;
  in.sceneCount = 3;
  in.hasSource = true;
  in.bpm = 120.0;
  ArrangementView v = buildArrangementView(in);
  CHECK(v.rows.empty() && !v.exportEnabled && v.exportTooltip == "Add at least one section.");
  CHECK(v.addEnabled && v.useChainEnabled && v.autoFillEnabled && !v.removeEnabled && v.closeLabel == "Close");
  for (int i = 0; i < 3; ++i) {
    ArrangementRowInput r;
    r.sceneNumber = i + 1;
    r.sceneName = "Scene " + std::to_string(i + 1);
    r.patternBars = 4;
    r.bars = 4;
    r.repeats = i == 0 ? 2 : 1;
    in.rows.push_back(r);
  }
  in.totalBars = 16;
  in.selected = 1;
  v = buildArrangementView(in);
  CHECK(v.valid && v.exportEnabled && v.exportTooltip.empty() && v.statusText.empty());
  CHECK(v.totalText == "Total: 16 bars (32.0 s at 120 BPM)");
  CHECK(v.removeEnabled && v.moveUpEnabled && v.moveDownEnabled);
  in.selected = 0;
  CHECK(!buildArrangementView(in).moveUpEnabled);
  in.selected = 2;
  CHECK(!buildArrangementView(in).moveDownEnabled);
  // out of range: text warning, export off
  in.totalBars = 6;
  v = buildArrangementView(in);
  CHECK(!v.valid && !v.exportEnabled && v.statusText == "Arrangement needs 8 to 32 bars (now 6)." && v.exportTooltip == v.statusText);
  in.totalBars = 33;
  CHECK(!buildArrangementView(in).exportEnabled);
  in.totalBars = 8;
  CHECK(buildArrangementView(in).exportEnabled);
  // a deleted scene blocks the export and keeps its row
  in.totalBars = 16;
  in.rows[1].deleted = true;
  v = buildArrangementView(in);
  CHECK(!v.exportEnabled && v.rows.size() == 3 && v.rows[1].find("Scene deleted") != std::string::npos);
  CHECK(v.statusText == "A section uses a scene that was deleted.");
  in.rows[1].deleted = false;
  in.rows[2].fits = false;
  CHECK(!buildArrangementView(in).exportEnabled);
  in.rows[2].fits = true;
  // a 4-bar pattern cut to 2 bars gets the Info note
  in.rows[0].bars = 2;
  in.rows[0].repeats = 8;
  v = buildArrangementView(in);
  CHECK(v.notes.size() == 1 && v.notes[0] == "Scene 1 has 4 bars; using the first 2");
  // no source: off with a reason
  in.hasSource = false;
  v = buildArrangementView(in);
  CHECK(!v.exportEnabled && v.exportTooltip == "Load a source first.");
  in.hasSource = true;
  // running: everything frozen, Cancel offered, progress text
  in.running = true;
  in.jobSection = 3;
  in.jobSections = 6;
  in.jobFraction = 0.42;
  v = buildArrangementView(in);
  CHECK(v.statusText == "Exporting arrangement: section 3 of 6 (42%)." && v.closeLabel == "Cancel" && !v.editable);
  CHECK(!v.exportEnabled && !v.addEnabled && !v.removeEnabled && !v.moveUpEnabled && !v.useChainEnabled && !v.autoFillEnabled);
  CHECK(v.progress > 0.41 && v.progress < 0.43);
}

CHOP_TEST(arrangement_messages_are_exact) {
  CHECK(arrangementProgressMessage(3, 6, 0.42) == "Exporting arrangement: section 3 of 6 (42%).");
  CHECK(arrangementProgressMessage(1, 1, 1.5) == "Exporting arrangement: section 1 of 1 (100%).");
  CHECK(arrangementExportedMessage("verse_arrangement.wav", 16, 32.0) == "Arrangement exported: verse_arrangement.wav (16 bars, 32.0 s).");
  CHECK(arrangementCancelledMessage() == "Export cancelled. No files were written.");
  CHECK(arrangementBarsMessage(6) == "Arrangement needs 8 to 32 bars (now 6).");
  CHECK(arrangementSceneDeletedMessage(4) == "Scene 4 was deleted. Section removed.");
  CHECK(arrangementWriteErrorMessage("/x/y.wav", "disk full") == "Could not write /x/y.wav: disk full");
  CHECK(arrangementRunningMessage() == "An arrangement export is already running.");
  CHECK(arrangeButtonEnabled(1, true) && !arrangeButtonEnabled(0, true) && !arrangeButtonEnabled(2, false));
  CHECK(arrangeButtonTooltip(2) == "Build an 8 to 32 bar arrangement from your scenes and export WAV, MIDI and markers.");
  CHECK(arrangeButtonTooltip(0) == "Record at least one scene first (Evolve scenes).");
}
