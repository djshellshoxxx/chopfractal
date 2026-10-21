#pragma once
// Thin view models for the session-level features (F13 drag-out and MIDI options, F10 morph, F09 scenes, F18
// arrangement). Pure functions from plain values to the texts, enable states and ranges a panel shows, so the
// wording and the rules are unit-tested here and any editor (the JUCE one included) only draws them and calls the
// session commands listed in docs/specs/roadmap/F13, F10, F09 and F18 ("Commands the GUI must call").
// Nothing here knows about ProjectSession; the caller passes values it read from it.
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::ui {

// ---------------------------------------------------------------------------------------------
// F13: MIDI options and the three drag handles
// ---------------------------------------------------------------------------------------------

// "C2" for 36 (C4 = 60, the scientific convention the spec's own examples use). 0..127, anything else gives "?".
std::string midiNoteName(int note);
// "C2 (36)".
std::string midiNoteText(int note);

constexpr int kMidiLoopChoices[5] = {1, 2, 4, 8, 16};
constexpr int kMaxDragChops = 63;  // 63 slices + the MIDI file = 64 files

struct MidiOptionsInput {
  int baseNote = 36;
  int curve = 0;  // 0 Linear, 1 Soft, 2 Hard, 3 Fixed
  int fixedVelocity = 100;
  int loops = 1;
  int channel = 0;  // 0..15
  int chopCount = 0;
  double bpm = 120.0;
  bool fallbackTempo = false;  // true: the manual tempo is in use because the host gave none
};
struct MidiOptionsView {
  int baseNote = 36;             // clamped into [0, baseNoteMax]
  int baseNoteMax = 127;         // 127 - chopCount + 1 (never below 0); the slider's live maximum
  std::string baseNoteText;      // "C2 (36)"
  std::vector<std::string> curveItems{"Linear", "Soft", "Hard", "Fixed"};
  int curve = 0;
  bool fixedVelocityEnabled = false;  // only for Fixed
  int fixedVelocity = 100;            // clamped 1..127
  std::vector<std::string> loopItems{"1", "2", "4", "8", "16"};
  int loopsIndex = 0;
  std::vector<std::string> channelItems;  // "1".."16"
  int channelIndex = 0;
  std::string tempoLine;  // "Tempo in the file: 120.0 BPM (host tempo)" or "... (manual tempo)"
};
MidiOptionsView buildMidiOptionsView(const MidiOptionsInput& in);
// Index into kMidiLoopChoices of the nearest permitted loop count (the settings store the count itself).
int loopsIndexFor(int loops);

struct DragOutInput {
  bool hasPattern = false;
  bool sourceMissing = false;
  int chopCount = 0;           // chops that would become slices
  double wavSeconds = 0.0;     // loops * pattern length + tail at the export tempo, see dragWavSeconds()
  int baseNote = 36;           // for the MIDI tooltip
};
struct DragHandleView {
  bool enabled = false;
  std::string tooltip;  // the explanation when disabled, else the normal tooltip
};
struct DragOutView {
  DragHandleView midi, wav, kit;
  std::string hint;  // the persistent grey line under the group, needs the folder
};
// loops * lengthTicks / 960 quarter notes at bpm, plus the tail.
double dragWavSeconds(std::int64_t lengthTicks, double bpm, int loops, double tailSeconds);
DragOutView buildDragOutView(const DragOutInput& in);
std::string dragHintText(const std::string& folder);
// "chopfractal_<stem>_v<version>_<bpm>bpm.<ext>"; `stem` is already a file-name-safe label, `version` is a node id or "current".
std::string dragFileName(const std::string& stem, const std::string& version, double bpm, const std::string& ext);

// Status line texts (exact strings of the F13 spec).
std::string dragStartedMessage(const std::string& fileName);       // "Dragging <file>"
std::string dragClickMessage();                                      // "Drag this button onto a DAW track."
std::string dragRenderErrorMessage(const std::string& reason);     // "Could not render the MIDI file: <reason>"
std::string dragTooLongMessage();
std::string dragSkippedEventsMessage(int skipped);                 // "N events were skipped because their chops no longer exist."
std::string dragTempFileErrorMessage();

}  // namespace chopfractal::ui
