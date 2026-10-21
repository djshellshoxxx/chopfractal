// Performance pads (F07) through the real processor: MIDI in processBlock, the on-screen pad queue, Gate and All Notes
// Off, the MIDI channel filter, bypass, take recording and the merge at the loop midpoint. The editor's pad panel is wired
// by the GUI work; these tests drive the same processor commands.
#include <cmath>
#include <string>
#include <utility>

#include "PluginProcessor.h"
#include "chop_test.hpp"
#include "shell_test_support.hpp"

namespace cf = chopfractal;
using namespace shelltest;

namespace {
void setParam(ChopFractalProcessor& p, const char* id, float plain) {
  auto* param = dynamic_cast<juce::RangedAudioParameter*>(p.apvts.getParameter(id));
  CHECK(param != nullptr);
  if (param) param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float peakOf(const juce::AudioBuffer<float>& b, int from, int to) {
  float m = 0.f;
  for (int i = from; i < to; ++i) m = std::max(m, std::fabs(b.getSample(0, i)));
  return m;
}

// One silent-input block with the given MIDI; returns the left channel buffer.
juce::AudioBuffer<float> block(ChopFractalProcessor& p, juce::MidiBuffer midi, int frames = 512) {
  juce::AudioBuffer<float> buf(2, frames);
  buf.clear();
  p.processBlock(buf, midi);
  return buf;
}

juce::MidiBuffer noteOn(int note, int sample, int channel = 1, int velocity = 100) {
  juce::MidiBuffer m;
  m.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(velocity)), sample);
  return m;
}

// A session whose pattern holds no events, so only the pads make sound and every hit lands on a known position.
void emptyPattern(ChopFractalProcessor& p) {
  p.withSession([](cf::composition::ProjectSession& s) {
    CHECK(s.edit([](const cf::pattern::Pattern& pat, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
                cf::pattern::Pattern q = pat;
                for (auto& bar : q.bars)
                  for (auto& beat : bar.beats) beat.events.clear();
                return q;
              })
              .ok());
  });
}

int userEvents(ChopFractalProcessor& p) {
  int n = 0;
  p.withSession([&](cf::composition::ProjectSession& s) {
    if (!s.pattern()) return;
    for (const auto& bar : s.pattern()->bars)
      for (const auto& beat : bar.beats)
        for (const auto& e : beat.events) n += e.userOwned ? 1 : 0;
  });
  return n;
}

// Runs host blocks of 512 frames from block index `from` (exclusive end `to`) at 120 BPM; ppq follows the block index.
// `notes` maps an absolute frame to a note to play (note on at that frame).
void runBlocks(ChopFractalProcessor& p, FakePlayHead& ph, int from, int to, const std::vector<std::pair<std::int64_t, int>>& notes = {}) {
  p.setPlayHead(&ph);
  for (int b = from; b < to; ++b) {
    ph.ppq = static_cast<double>(b) * 512.0 / 24000.0;
    juce::MidiBuffer midi;
    for (const auto& n : notes)
      if (n.first >= static_cast<std::int64_t>(b) * 512 && n.first < static_cast<std::int64_t>(b + 1) * 512)
        midi.addEvent(juce::MidiMessage::noteOn(1, n.second, static_cast<juce::uint8>(100)), static_cast<int>(n.first - static_cast<std::int64_t>(b) * 512));
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    p.processBlock(buf, midi);
  }
  p.setPlayHead(nullptr);
}
}  // namespace

CHOP_TEST(plugin_declares_midi_input_only) {
  ChopFractalProcessor p;
  CHECK(p.acceptsMidi() && !p.producesMidi() && !p.isMidiEffect());
  CHECK(p.getLatencySamples() == 0);
}

CHOP_TEST(midi_note_on_triggers_the_mapped_chop_at_the_event_sample) {
  for (bool playing : {false, true}) {
    ChopFractalProcessor p;
    prepare(p, 2048);
    makeSession(p);
    emptyPattern(p);
    FakePlayHead ph;
    ph.playing = playing;
    p.setPlayHead(&ph);
    const auto buf = block(p, noteOn(36, 100), 2048);
    p.setPlayHead(nullptr);
    CHECK(peakOf(buf, 0, 100) == 0.f);
    int first = -1;
    for (int i = 0; i < 2048 && first < 0; ++i)
      if (buf.getSample(0, i) != 0.f) first = i;
    CHECK(first >= 101 && first <= 102);
    CHECK(peakOf(buf, 0, 2048) > 0.05f);
  }
}

CHOP_TEST(an_unmapped_note_starts_nothing) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  const auto buf = block(p, noteOn(120, 0));
  CHECK(peakOf(buf, 0, 512) == 0.f);
  CHECK(p.activePadVoices() == 0);
}

CHOP_TEST(midi_channel_filter_ignores_other_channels) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  cf::composition::PadSettings ps = p.padSettings();
  ps.midiChannel = 2;
  CHECK(p.setPadSettings(ps).ok());
  CHECK(peakOf(block(p, noteOn(36, 0, 1)), 0, 512) == 0.f);
  CHECK(peakOf(block(p, noteOn(36, 0, 2)), 0, 512) > 0.05f);
  ps.midiChannel = 0;  // omni
  CHECK(p.setPadSettings(ps).ok());
  CHECK(peakOf(block(p, noteOn(37, 0, 9)), 0, 512) > 0.05f);
}

CHOP_TEST(a_loaded_project_restores_the_midi_channel) {
  ChopFractalProcessor a;
  prepare(a);
  makeSession(a);
  cf::composition::PadSettings ps = a.padSettings();
  ps.midiChannel = 5;
  ps.baseNote = 48;
  CHECK(a.setPadSettings(ps).ok());
  juce::MemoryBlock saved;
  a.getStateInformation(saved);

  ChopFractalProcessor b;
  prepare(b);
  b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  CHECK(waitForRestore(b));
  b.pollAudioFlags();
  CHECK(b.padSettings() == ps);
  emptyPattern(b);
  CHECK(peakOf(block(b, noteOn(48, 0, 4)), 0, 512) == 0.f);   // wrong channel
  CHECK(peakOf(block(b, noteOn(36, 0, 5)), 0, 512) == 0.f);   // old base note: unmapped now
  CHECK(peakOf(block(b, noteOn(48, 0, 5)), 0, 512) > 0.05f);
}

CHOP_TEST(note_off_in_gate_mode_stops_the_voice_and_one_shot_ignores_it) {
  for (bool gate : {true, false}) {
    ChopFractalProcessor p;
    prepare(p, 256);
    makeSession(p);
    emptyPattern(p);
    setParam(p, "perf_mode", gate ? 2.f : 0.f);
    (void)block(p, noteOn(36, 0), 256);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
    (void)block(p, off, 256);
    (void)block(p, {}, 256);  // the 64-frame release finishes inside this block
    const auto later = block(p, {}, 256);
    if (gate) CHECK(peakOf(later, 0, 256) == 0.f);
    else CHECK(peakOf(later, 0, 256) > 0.001f);
  }
}

CHOP_TEST(all_notes_off_cc123_silences_pad_voices) {
  ChopFractalProcessor p;
  prepare(p, 256);
  makeSession(p);
  emptyPattern(p);
  (void)block(p, noteOn(36, 0), 256);
  (void)block(p, noteOn(37, 0), 256);
  CHECK(p.activePadVoices() >= 1);
  juce::MidiBuffer off;
  off.addEvent(juce::MidiMessage::allNotesOff(1), 0);
  (void)block(p, off, 256);
  (void)block(p, {}, 256);
  CHECK(peakOf(block(p, {}, 256), 0, 256) == 0.f);
}

CHOP_TEST(midi_is_ignored_when_effect_enable_is_off) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  setParam(p, "effect_enable", 0.f);
  CHECK(peakOf(block(p, noteOn(36, 0)), 0, 512) == 0.f);
  setParam(p, "effect_enable", 1.f);
  CHECK(peakOf(block(p, noteOn(36, 0)), 0, 512) > 0.05f);
}

CHOP_TEST(ui_pad_queue_triggers_like_midi_one_block_later) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  p.padTrigger(36, 100, true);
  const auto buf = block(p, {});
  CHECK(buf.getSample(0, 0) == 0.f);            // the voice starts at frame 0; the attack ramp begins from silence
  CHECK(peakOf(buf, 1, 512) > 0.05f);
  auto bits = p.takePadActivity();
  CHECK((bits[1] >> (36 & 31)) & 1u);           // note 36 lives in word 1
  CHECK(p.takePadActivity()[1] == 0);           // read-and-clear
  p.padTrigger(-1, 100, true);                  // out of range: ignored
  p.padTrigger(200, 100, true);
  p.padAllNotesOff();
  (void)block(p, {});
  (void)block(p, {});
  CHECK(peakOf(block(p, {}), 0, 512) == 0.f);
}

CHOP_TEST(recording_a_take_adds_quantized_events_at_the_midpoint) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  FakePlayHead ph;
  CHECK(userEvents(p) == 0);
  p.setRecordArmed(true);
  CHECK(p.recordArmed());
  // ppq 1.04 is frame 24960 (block 48, offset 384); ppq 7.95 is frame 190800 (block 372, offset 336). Two bars: 8 quarters.
  runBlocks(p, ph, 0, 380, {{24960, 36}, {190800, 36}});
  p.pollAudioFlags();  // the loop midpoint (ppq 4) was crossed: the take merges
  CHECK(p.takeHits() == 0);
  CHECK(userEvents(p) == 2);
  bool at960 = false, at0 = false;
  p.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& beat : s.pattern()->bars[0].beats)
      for (const auto& e : beat.events) {
        at960 = at960 || (e.start == 960 && e.userOwned);
        at0 = at0 || (e.start == 0 && e.userOwned);
      }
  });
  CHECK(at960 && at0);
  CHECK(p.statusMessage() == "Recorded 2 hits.");
  p.withSession([](cf::composition::ProjectSession& s) { CHECK(s.undo()); });  // one undo step
  CHECK(userEvents(p) == 0);
}

CHOP_TEST(record_flushes_when_disarmed_and_when_the_transport_stops) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  FakePlayHead ph;
  p.setRecordArmed(true);
  runBlocks(p, ph, 0, 60, {{24960, 36}});  // 60 blocks reach ppq 1.28: before the midpoint
  p.pollAudioFlags();
  CHECK(p.takeHits() == 1 && userEvents(p) == 0);  // waiting
  p.setRecordArmed(false);
  p.pollAudioFlags();
  CHECK(p.takeHits() == 0 && userEvents(p) == 1);

  // Transport stop: the poll sees the playing flag fall.
  p.setRecordArmed(true);
  p.pollAudioFlags();
  runBlocks(p, ph, 0, 60, {{30000, 37}});
  p.pollAudioFlags();
  CHECK(p.takeHits() == 1);
  ph.playing = false;
  runBlocks(p, ph, 60, 61);
  p.pollAudioFlags();
  CHECK(p.takeHits() == 0 && userEvents(p) == 2);
  p.setRecordArmed(false);
}

CHOP_TEST(recording_needs_a_pattern_and_a_discarded_take_is_not_merged) {
  ChopFractalProcessor p;
  prepare(p);
  p.setRecordArmed(true);
  CHECK(!p.recordArmed());
  CHECK(p.statusMessage() == "Recording needs a pattern. Generate one first.");
  makeSession(p);
  emptyPattern(p);
  FakePlayHead ph;
  p.setRecordArmed(true);
  runBlocks(p, ph, 0, 60, {{24960, 36}});
  p.pollAudioFlags();
  CHECK(p.takeHits() == 1);
  p.discardTake();
  CHECK(p.takeHits() == 0);
  p.setRecordArmed(false);
  p.pollAudioFlags();
  CHECK(userEvents(p) == 0);
}

CHOP_TEST(a_host_without_position_still_plays_pads_but_records_nothing) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  FakePlayHead ph;
  ph.hasPpq = false;
  p.setRecordArmed(true);
  p.setPlayHead(&ph);
  const auto buf = block(p, noteOn(36, 0));
  p.setPlayHead(nullptr);
  CHECK(peakOf(buf, 0, 512) > 0.05f);
  p.pollAudioFlags();
  CHECK(p.takeHits() == 0);
  p.setRecordArmed(true);
  CHECK(p.statusMessage() == "Recording needs the host to report its song position.");
  p.setRecordArmed(false);
}

CHOP_TEST(take_queue_overflow_is_counted_not_fatal) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  emptyPattern(p);
  FakePlayHead ph;
  p.setRecordArmed(true);
  p.setPlayHead(&ph);
  for (int b = 0; b < 6; ++b) {
    ph.ppq = 1.0 + b * 0.02;
    juce::MidiBuffer midi;
    for (int i = 0; i < 300; ++i) midi.addEvent(juce::MidiMessage::noteOn(1, 36 + (i % 8), static_cast<juce::uint8>(90)), i);
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    p.processBlock(buf, midi);
  }
  p.setPlayHead(nullptr);
  CHECK(p.takeDropped() > 0);       // 1536 hits offered, the queue holds 1024
  CHECK(p.padDropped() >= 6 * 44);  // 300 events in a block, 256 accepted
  p.pollAudioFlags();
  CHECK(p.takeHits() > 0 && p.takeHits() <= 1024);
  p.discardTake();
  p.setRecordArmed(false);
}
