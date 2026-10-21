#pragma once
// View model for the performance pads (F07): the 4x4 grid, its labels and titles, the page list, the settings boxes and the
// text of the messages a take produces. Pure functions of plain data, so the whole panel is testable without a GUI. The
// editor reads a PadView each timer tick; the handlers call the commands listed in
// docs/specs/roadmap/F07-performance-pads.md section 12.
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::ui {

constexpr int kPadsPerPage = 16;
constexpr int kMaxMidiNote = 127;

struct PadUiInput {
  int chopCount = 0;                       // chops in snapshot order
  std::vector<std::string> roles;          // per chop (may be shorter than chopCount; missing = no role)
  std::vector<std::uint8_t> chokeGroups;   // per chop, 0 = none (may be shorter)
  int baseNote = 36;                       // 0..111
  int page = 0;                            // 0-based; clamped to the page count
  int midiChannel = 0;                     // 0 omni, 1..16
  int quantizeDivision = 16;               // 0 off, 4, 8, 16, 32
  bool quantizeTriplet = false;
  int merge = 0;                           // 0 add, 1 replace
  bool hasSource = false;
  bool hasPattern = false;
  bool recordArmed = false;
  int takeHits = 0;
  bool hostPlaying = false;
  bool hostHasPosition = true;
  std::uint32_t sounding[4] = {0, 0, 0, 0};  // bit n of word n >> 5: note n started recently (lights the pad)
};

struct PadCell {
  int number = 0;                // 1..16, the pad's position on the page
  int chop = -1;                 // 0-based chop index, -1 for an empty pad
  int note = -1;                 // MIDI note, -1 when the chop has no note
  bool enabled = false;
  bool lit = false;              // the note started recently (shown with an outline and a marker, never colour alone)
  std::string name;              // component name and button text: "Pad N"
  std::string label;             // painted on the pad: "M" (chop number), empty for an empty pad
  std::string role;              // painted under the label
  std::string noteName;          // "C1"
  std::string title;             // "Pad N: chop M, note C1", or why the pad is disabled
  int chokeGroup = 0;
};

struct PadView {
  std::vector<PadCell> pads;     // always kPadsPerPage cells
  int pageCount = 1;
  int page = 0;
  bool showPage = false;         // more than one page
  std::vector<std::string> pageItems;   // "Chops 1-16", "Chops 17-32", ...
  bool padsEnabled = false;      // needs a source with chops
  std::string emptyText;         // shown instead of the pads when they are unavailable
  bool settingsEnabled = false;
  std::string baseNoteText;      // "C1 (36)"
  bool recordEnabled = false;    // needs a pattern
  bool recordOn = false;
  std::string recordHint;        // why Record is disabled, or what to do next while it is armed
  bool discardEnabled = false;
  std::string discardLabel;      // "Discard Take" or "N hits waiting"
  int mappedChops = 0;           // chops that have a MIDI note
};

PadView buildPadView(const PadUiInput& in);

// Note names follow the convention C3 = MIDI 60, so 36 is "C1" and 0 is "C-2". Out of range input returns an empty string.
std::string padNoteName(int note);
std::string padBaseNoteText(int note);
// Pad on-screen velocity from the vertical mouse position: 127 at the top edge, 40 at the bottom. Out-of-range y clamps.
int padVelocityFromY(double y, double height);
// Combo box items, in order.
std::vector<std::string> padMidiChannelItems();   // "Omni", "1" .. "16" (index = channel)
constexpr const char* kPadQuantizeItems[6] = {"Off", "1/4", "1/8", "1/16", "1/32", "1/16T"};
constexpr const char* kPadMergeItems[2] = {"Add to pattern", "Replace slots"};
constexpr const char* kPadModeItems[3] = {"Poly", "Mono", "Gate"};
constexpr const char* kPadCurveItems[4] = {"Linear", "Soft", "Hard", "Fixed"};
struct PadQuantize {
  int division = 16;
  bool triplet = false;
};
PadQuantize padQuantizeFromIndex(int index);   // out of range gives the default 1/16
int padQuantizeIndex(int division, bool triplet);
// The right-click menu of a pad: item 0 is "Choke group: Off", item n is "Choke group n" (1..4).
std::vector<std::string> padChokeMenuItems();

// What a merged take reports, as plain numbers (mirrors composition::TakeReport).
struct PadTakeCounts {
  int added = 0, skippedLocked = 0, skippedCap = 0, skippedUnmapped = 0, skippedDuplicate = 0;
  int eventCap = 512;
};
// "Recorded 5 hits." followed by one sentence per skip reason. Empty when nothing happened (no hits at all).
std::string padTakeNotice(const PadTakeCounts& counts);
constexpr const char* kPadArmedText = "Recording armed: start the transport and play the pads.";
constexpr const char* kPadNoPositionText = "Recording needs the host to report its song position.";
constexpr const char* kPadNoPatternText = "Recording needs a pattern. Generate one first.";
constexpr const char* kPadNeedsChopsText = "Pads need a source with chops.";
constexpr const char* kPadLoadFirstText = "Load a source and detect chops to play the pads.";
constexpr const char* kPadTakeFullText = "Take buffer full (4096 hits); extra hits were ignored.";
constexpr const char* kPadNoNoteText = "No MIDI note available for this chop; lower the base note.";
constexpr const char* kPadNoMidiText = "This host does not send MIDI to effects; use the on-screen pads.";

}  // namespace chopfractal::ui
