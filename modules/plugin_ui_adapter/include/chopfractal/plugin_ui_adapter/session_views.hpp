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

// ---------------------------------------------------------------------------------------------
// F10: A/B morph
// ---------------------------------------------------------------------------------------------

struct MorphInput {
  bool hasA = false, hasB = false;
  bool active = false;
  double amount = 0.5;
  bool scenesPlaying = false;  // F09: morph and scene playback exclude each other
};
struct MorphView {
  bool toggleEnabled = false;       // the "Morph" toggle
  std::string toggleTooltip;        // "Store A and B first." / "Cancel the scene playback before morphing." / the normal text
  bool toggleOn = false;
  bool sliderEnabled = false;       // "Morph amount" (disabled while the toggle is off)
  std::string sliderText;           // "A", "B" or "37% B"
  bool commitEnabled = false, cancelEnabled = false;
  bool badgeVisible = false;        // the "Morph preview" text badge on the pattern panel
  std::string badgeText = "Morph preview";
  std::string status;               // "Morphing 37% toward B. Commit Morph to keep it, or Cancel Morph." while active, else empty
};
// "A" at 0, "B" at 1, otherwise "<percent toward B>% B".
std::string morphAmountText(double amount);
MorphView buildMorphView(const MorphInput& in);
std::string morphCommittedMessage();   // "Committed the morph as a new variation."
std::string morphCancelledMessage();   // "Morph cancelled; your pattern is unchanged."
std::string morphEndedMessage();       // "Morph ended because the pattern was changed."

// Coalesces slider drags into session setMorph calls: a pending value is applied when it differs from the applied one by
// at least kMinDelta and kIntervalMs have passed since the last application; the final value (release) is always applied.
// The editor calls setPending() from the slider callback, release() on mouse-up and take() from its timer (10 Hz or faster).
class MorphCoalescer {
 public:
  static constexpr double kMinDelta = 0.005;
  static constexpr long long kIntervalMs = 50;
  void reset(double applied);
  void setPending(double value);
  void release();
  // True when the caller must apply `value` now (and then the coalescer treats it as applied).
  bool take(long long nowMs, double& value);
  double applied() const { return applied_; }

 private:
  double applied_ = 0.5, pending_ = 0.5;
  bool has_ = false, final_ = false;
  long long lastMs_ = -1000000;
};

// ---- Evolve scenes (F09) ----
// Pure functions of plain values: the GUI copies session facts into these structs (docs/specs/roadmap/F09-evolve-scenes.md
// section 12 lists the session call behind every control).
struct SceneRowInput {
  int number = 1;                // position + 1
  std::string name;
  int loops = 4;
  int next = 0;                  // 0 next, 1 repeat, 2 stop, 3 jump
  int jumpNumber = 0;            // display number of the jump target
  bool playing = false;
  int loopIndex = 0;             // loops of the playing scene completed
  bool armed = false;
  bool fits = true;
};
std::string sceneRowText(const SceneRowInput& row);
struct SceneStatusInput {
  bool running = false, holding = false, recording = false, waitingForTransport = false;
  int playingNumber = 0;         // 0 while the first scene waits for its boundary
  int armedNumber = 0;
  int loopIndex = 0, loops = 0;
};
struct SceneViewInput {
  std::vector<SceneRowInput> rows;
  SceneStatusInput status;
  int selected = -1;             // row index or -1
  bool hasPattern = false, hasSource = false;
  bool evolveRunning = false;
  bool wrap = true;
  int capturedCount = 0;         // rows.size(), repeated so a caller may pass trimmed rows
};
struct SceneView {
  std::vector<std::string> rows;
  std::string emptyText;         // shown while there are no rows
  std::string statusText;        // 10 Hz status label
  bool captureEnabled = false, updateEnabled = false, keepEnabled = false;
  bool moveUpEnabled = false, moveDownEnabled = false, deleteEnabled = false, clearEnabled = false;
  bool exportEnabled = false, playEnabled = false, stopEnabled = false, recEnabled = false;
  bool recOn = false, playOn = false, wrapOn = true;
  std::string editTooltip;       // "Stop scene playback first." while Delete / Clear All / Move are disabled by playback, else empty
  std::string exportTooltip;     // the WAV caveat
};
SceneView buildSceneView(const SceneViewInput& in);
std::string sceneStatusText(const SceneStatusInput& st, bool evolveRunning, int sceneCount);
std::string sceneCapturedMessage(int number);                // "Captured Scene 3."
std::string sceneFullMessage();                              // "The scene list is full (32). Delete a scene to capture more."
std::string sceneRecordingFullMessage();                     // "Scene recording stopped: the list is full (32)."
std::string sceneKeptMessage(int number);                    // "Kept Scene 3 as a new variation."
std::string sceneExportedMessage(int count, const std::string& folder);  // "Exported 7 scenes to <folder>."
std::string sceneClearPromptTitle();                         // "Delete all scenes?"
std::string sceneClearPromptBody();                          // "This cannot be undone."
std::string sceneEmptyText();                                // the empty-state line of the list
std::string sceneStopFirstText();                            // "Stop scene playback first."

}  // namespace chopfractal::ui
