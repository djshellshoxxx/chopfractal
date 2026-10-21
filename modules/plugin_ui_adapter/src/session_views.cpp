#include <chopfractal/plugin_ui_adapter/session_views.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::ui {
namespace {
std::string fmt1(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}
}  // namespace

std::string midiNoteName(int note) {
  static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (note < 0 || note > 127) return "?";
  return std::string(names[note % 12]) + std::to_string(note / 12 - 1);
}

std::string midiNoteText(int note) { return midiNoteName(note) + " (" + std::to_string(note) + ")"; }

int loopsIndexFor(int loops) {
  int best = 0;
  for (int i = 0; i < 5; ++i)
    if (std::abs(kMidiLoopChoices[i] - loops) < std::abs(kMidiLoopChoices[best] - loops)) best = i;
  return best;
}

MidiOptionsView buildMidiOptionsView(const MidiOptionsInput& in) {
  MidiOptionsView v;
  v.baseNoteMax = std::max(0, 127 - std::max(0, in.chopCount) + 1);
  v.baseNote = std::min(v.baseNoteMax, std::max(0, in.baseNote));
  v.baseNoteText = midiNoteText(v.baseNote);
  v.curve = std::min(3, std::max(0, in.curve));
  v.fixedVelocityEnabled = v.curve == 3;
  v.fixedVelocity = std::min(127, std::max(1, in.fixedVelocity));
  v.loopsIndex = loopsIndexFor(in.loops);
  for (int c = 1; c <= 16; ++c) v.channelItems.push_back(std::to_string(c));
  v.channelIndex = std::min(15, std::max(0, in.channel));
  v.tempoLine = "Tempo in the file: " + fmt1(in.bpm) + " BPM (" + (in.fallbackTempo ? "manual tempo" : "host tempo") + ")";
  return v;
}

double dragWavSeconds(std::int64_t lengthTicks, double bpm, int loops, double tailSeconds) {
  if (!(bpm > 0.0)) return 0.0;
  return static_cast<double>(loops) * static_cast<double>(lengthTicks) / 960.0 * 60.0 / bpm + tailSeconds;
}

std::string dragHintText(const std::string& folder) {
  return "Dragged files are kept in " + folder + " until you delete them. Use Export for files you want to keep.";
}

DragOutView buildDragOutView(const DragOutInput& in) {
  DragOutView v;
  const std::string noPattern = "Generate a pattern first.";
  const std::string noAudio = "The audio for this project is missing; MIDI can still be dragged, audio cannot.";
  const int note = std::min(127, std::max(0, in.baseNote));
  v.midi.enabled = in.hasPattern;
  v.midi.tooltip = in.hasPattern ? "Drag the current groove into your DAW as a MIDI file. Chop 1 plays note " + std::to_string(note) + " (" + midiNoteName(note) + ")."
                                 : noPattern;
  v.wav.enabled = in.hasPattern && !in.sourceMissing && in.wavSeconds <= 120.0;
  v.wav.tooltip = !in.hasPattern ? noPattern
                  : in.sourceMissing ? noAudio
                  : in.wavSeconds > 120.0 ? dragTooLongMessage()
                                          : "Drag the current groove into your DAW as a WAV file.";
  v.kit.enabled = in.hasPattern && !in.sourceMissing && in.chopCount <= kMaxDragChops && in.chopCount >= 1;
  v.kit.tooltip = !in.hasPattern ? noPattern
                  : in.sourceMissing ? noAudio
                  : in.chopCount > kMaxDragChops ? "Too many chops to drag (64 max). Use Export Kit."
                  : in.chopCount < 1 ? "Add chops first."
                                     : "Drag the slice WAVs and the MIDI file into a sampler or drum rack.";
  return v;
}

std::string dragFileName(const std::string& stem, const std::string& version, double bpm, const std::string& ext) {
  return "chopfractal_" + stem + "_v" + version + "_" + std::to_string(static_cast<long long>(std::llround(bpm))) + "bpm." + ext;
}

std::string dragStartedMessage(const std::string& fileName) { return "Dragging " + fileName; }
std::string dragClickMessage() { return "Drag this button onto a DAW track."; }
std::string dragRenderErrorMessage(const std::string& reason) { return "Could not render the MIDI file: " + reason; }
std::string dragTooLongMessage() { return "Too long to drag: reduce Loops in the export settings (limit 120 s of audio)."; }
std::string dragSkippedEventsMessage(int skipped) {
  return std::to_string(skipped) + (skipped == 1 ? " event was" : " events were") + " skipped because their chops no longer exist.";
}
std::string dragTempFileErrorMessage() { return "Could not create a temporary file. Use Export instead."; }

}  // namespace chopfractal::ui
