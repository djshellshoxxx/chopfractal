#include <chopfractal/plugin_ui_adapter/pads_view.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace chopfractal::ui {

std::string padNoteName(int note) {
  if (note < 0 || note > kMaxMidiNote) return {};
  static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return std::string(names[note % 12]) + std::to_string(note / 12 - 2);
}

std::string padBaseNoteText(int note) {
  note = std::clamp(note, 0, 111);
  return padNoteName(note) + " (" + std::to_string(note) + ")";
}

int padVelocityFromY(double y, double height) {
  if (!(height > 0.0) || !std::isfinite(y)) return 127;
  const double t = std::clamp(y / height, 0.0, 1.0);
  return static_cast<int>(127 - std::lround(87.0 * t));
}

std::vector<std::string> padMidiChannelItems() {
  std::vector<std::string> items{"Omni"};
  for (int c = 1; c <= 16; ++c) items.push_back(std::to_string(c));
  return items;
}

PadQuantize padQuantizeFromIndex(int index) {
  switch (index) {
    case 0: return {0, false};
    case 1: return {4, false};
    case 2: return {8, false};
    case 3: return {16, false};
    case 4: return {32, false};
    case 5: return {16, true};
    default: return {16, false};
  }
}

int padQuantizeIndex(int division, bool triplet) {
  if (division == 0) return 0;
  if (division == 16 && triplet) return 5;
  switch (division) {
    case 4: return 1;
    case 8: return 2;
    case 16: return 3;
    case 32: return 4;
    default: return 3;
  }
}

std::vector<std::string> padChokeMenuItems() {
  std::vector<std::string> items{"Choke group: Off"};
  for (int g = 1; g <= 4; ++g) items.push_back("Choke group " + std::to_string(g));
  return items;
}

PadView buildPadView(const PadUiInput& in) {
  PadView v;
  const int chops = std::max(0, in.chopCount);
  const int baseNote = std::clamp(in.baseNote, 0, 111);
  v.pageCount = std::max(1, (chops + kPadsPerPage - 1) / kPadsPerPage);
  v.page = std::clamp(in.page, 0, v.pageCount - 1);
  v.showPage = v.pageCount > 1;
  for (int p = 0; p < v.pageCount; ++p) {
    const int first = p * kPadsPerPage + 1;
    const int last = std::min(chops, first + kPadsPerPage - 1);
    v.pageItems.push_back("Chops " + std::to_string(first) + "-" + std::to_string(last));
  }
  v.padsEnabled = in.hasSource && chops > 0;
  v.settingsEnabled = v.padsEnabled;
  if (!v.padsEnabled) v.emptyText = kPadLoadFirstText;
  v.baseNoteText = padBaseNoteText(baseNote);

  for (int n = 1; n <= kPadsPerPage; ++n) {
    PadCell c;
    c.number = n;
    c.name = "Pad " + std::to_string(n);
    const int chop = v.page * kPadsPerPage + n - 1;
    if (v.padsEnabled && chop < chops) {
      c.chop = chop;
      c.label = std::to_string(chop + 1);
      if (static_cast<std::size_t>(chop) < in.roles.size()) c.role = in.roles[static_cast<std::size_t>(chop)];
      if (static_cast<std::size_t>(chop) < in.chokeGroups.size()) c.chokeGroup = in.chokeGroups[static_cast<std::size_t>(chop)];
      const int note = baseNote + chop;
      if (note <= kMaxMidiNote) {
        c.note = note;
        c.enabled = true;
        c.noteName = padNoteName(note);
        c.title = c.name + ": chop " + std::to_string(chop + 1) + ", note " + c.noteName;
        c.lit = ((in.sounding[note >> 5] >> (note & 31)) & 1u) != 0;
        ++v.mappedChops;
      } else {
        c.title = c.name + ": " + kPadNoNoteText;
      }
    } else {
      c.title = c.name + ": no chop";
    }
    v.pads.push_back(std::move(c));
  }

  v.recordOn = in.recordArmed;
  v.recordEnabled = v.padsEnabled && in.hasPattern;
  if (!v.padsEnabled) v.recordHint = kPadNeedsChopsText;
  else if (!in.hasPattern) v.recordHint = kPadNoPatternText;
  else if (in.recordArmed) v.recordHint = in.hostPlaying && !in.hostHasPosition ? kPadNoPositionText : kPadArmedText;
  v.discardEnabled = in.takeHits > 0;
  v.discardLabel = in.takeHits > 0 ? std::to_string(in.takeHits) + (in.takeHits == 1 ? " hit waiting" : " hits waiting") : "Discard Take";
  return v;
}

std::string padTakeNotice(const PadTakeCounts& c) {
  const int skipped = c.skippedLocked + c.skippedCap + c.skippedUnmapped + c.skippedDuplicate;
  if (c.added == 0 && skipped == 0) return {};
  std::string t = "Recorded " + std::to_string(c.added) + (c.added == 1 ? " hit." : " hits.");
  if (c.skippedLocked > 0) t += " " + std::to_string(c.skippedLocked) + " skipped: their beat is locked.";
  if (c.skippedCap > 0) t += " " + std::to_string(c.skippedCap) + " skipped: the event cap (" + std::to_string(c.eventCap) + ") is reached.";
  if (c.skippedUnmapped > 0) t += " " + std::to_string(c.skippedUnmapped) + " skipped: they matched no chop.";
  if (c.skippedDuplicate > 0) t += " " + std::to_string(c.skippedDuplicate) + " skipped: already in the pattern.";
  return t;
}

}  // namespace chopfractal::ui
