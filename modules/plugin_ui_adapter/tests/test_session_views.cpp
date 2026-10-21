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
