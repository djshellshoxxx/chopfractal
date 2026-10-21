#include <chopfractal/plugin_ui_adapter/session_views.hpp>

#include <algorithm>
#include <cstddef>
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

// ---- F10 ----

std::string morphAmountText(double amount) {
  if (!(amount > 0.0)) return "A";
  if (!(amount < 1.0)) return "B";
  return std::to_string(static_cast<long long>(std::llround(amount * 100.0))) + "% B";
}

MorphView buildMorphView(const MorphInput& in) {
  MorphView v;
  const bool stored = in.hasA && in.hasB;
  v.toggleEnabled = (stored && !in.scenesPlaying) || in.active;
  v.toggleOn = in.active;
  v.toggleTooltip = !stored ? "Store A and B first."
                    : in.scenesPlaying && !in.active ? "Cancel the scene playback before morphing."
                    : "Morph between A and B. Locks are not enforced inside the blend.";
  v.sliderEnabled = in.active;
  v.sliderText = morphAmountText(in.amount);
  v.commitEnabled = v.cancelEnabled = in.active;
  v.badgeVisible = in.active;
  if (in.active) {
    const long long pct = std::llround(std::min(1.0, std::max(0.0, in.amount)) * 100.0);
    v.status = "Morphing " + std::to_string(pct) + "% toward B. Commit Morph to keep it, or Cancel Morph.";
  }
  return v;
}

std::string morphCommittedMessage() { return "Committed the morph as a new variation."; }
std::string morphCancelledMessage() { return "Morph cancelled; your pattern is unchanged."; }
std::string morphEndedMessage() { return "Morph ended because the pattern was changed."; }

void MorphCoalescer::reset(double applied) {
  applied_ = pending_ = applied;
  has_ = final_ = false;
  lastMs_ = -1000000;
}
void MorphCoalescer::setPending(double value) {
  pending_ = std::min(1.0, std::max(0.0, value));
  has_ = true;
  final_ = false;
}
void MorphCoalescer::release() { final_ = true; }
bool MorphCoalescer::take(long long nowMs, double& value) {
  if (!has_) return false;
  const double delta = std::fabs(pending_ - applied_);
  if (final_) {
    has_ = false;
    final_ = false;
    if (delta == 0.0) return false;
  } else {
    if (delta < kMinDelta || nowMs - lastMs_ < kIntervalMs) return false;
  }
  applied_ = pending_;
  lastMs_ = nowMs;
  value = pending_;
  return true;
}

// ---- Evolve scenes (F09) ----

std::string sceneEmptyText() { return "No scenes yet. Press Capture Scene, or turn on Rec Scenes and start Evolve."; }
std::string sceneStopFirstText() { return "Stop scene playback first."; }
std::string sceneCapturedMessage(int number) { return "Captured Scene " + std::to_string(number) + "."; }
std::string sceneFullMessage() { return "The scene list is full (32). Delete a scene to capture more."; }
std::string sceneRecordingFullMessage() { return "Scene recording stopped: the list is full (32)."; }
std::string sceneKeptMessage(int number) { return "Kept Scene " + std::to_string(number) + " as a new variation."; }
std::string sceneExportedMessage(int count, const std::string& folder) {
  return "Exported " + std::to_string(count) + (count == 1 ? " scene to " : " scenes to ") + folder + ".";
}
std::string sceneClearPromptTitle() { return "Delete all scenes?"; }
std::string sceneClearPromptBody() { return "This cannot be undone."; }

std::string sceneRowText(const SceneRowInput& r) {
  static const char* const kNext[] = {"next", "repeat", "stop"};
  std::string after;
  if (r.next == 3) after = "jump to " + std::to_string(r.jumpNumber);
  else after = kNext[r.next >= 0 && r.next <= 2 ? r.next : 0];
  std::string text = std::to_string(r.number) + "  " + r.name + "   " + std::to_string(r.loops) + (r.loops == 1 ? " loop" : " loops") + "   > " + after;
  if (r.playing) text += "  [Playing, loop " + std::to_string(std::min(r.loopIndex + 1, r.loops)) + " of " + std::to_string(r.loops) + "]";
  else if (r.armed) text += "  [Next]";
  if (!r.fits) text += " (does not fit the chops)";
  return text;
}

std::string sceneStatusText(const SceneStatusInput& st, bool evolveRunning, int sceneCount) {
  const std::string of = " of " + std::to_string(sceneCount);
  if (st.running && st.waitingForTransport) return "Waiting for the host to play";
  if (st.running && st.playingNumber > 0)
    return "Scene " + std::to_string(st.playingNumber) + of + ", loop " + std::to_string(std::min(st.loopIndex + 1, std::max(1, st.loops))) + " of " + std::to_string(std::max(1, st.loops));
  if (st.running && st.armedNumber > 0) return "Scene " + std::to_string(st.armedNumber) + of + " starts at the next loop";
  if (st.holding) return "Sequence finished: Scene " + std::to_string(st.playingNumber) + " keeps looping";
  if (st.recording) return evolveRunning ? "Recording scenes: " + std::to_string(sceneCount) + " captured" : "Recording scenes: waiting for Evolve";
  return "Scenes stopped";
}

SceneView buildSceneView(const SceneViewInput& in) {
  SceneView v;
  const bool active = in.status.running;
  const bool anything = active || in.status.holding;
  const int count = static_cast<int>(in.rows.size());
  const bool selected = in.selected >= 0 && in.selected < count;
  for (const SceneRowInput& r : in.rows) v.rows.push_back(sceneRowText(r));
  v.emptyText = count == 0 ? sceneEmptyText() : std::string();
  v.statusText = sceneStatusText(in.status, in.evolveRunning, count);
  v.captureEnabled = in.hasPattern;
  v.recEnabled = in.hasPattern || in.status.recording;
  v.recOn = in.status.recording;
  v.updateEnabled = selected && in.hasPattern;   // allowed while playing: the session stops playback first
  v.keepEnabled = selected;
  v.moveUpEnabled = selected && in.selected > 0 && !active;
  v.moveDownEnabled = selected && in.selected + 1 < count && !active;
  v.deleteEnabled = selected && !active;
  v.clearEnabled = count > 0 && !active;
  v.exportEnabled = count > 0;
  v.playEnabled = count > 0 && in.hasPattern && in.hasSource && !active;
  v.playOn = active;
  v.stopEnabled = anything;
  v.wrapOn = in.wrap;
  if (active && (selected || count > 0)) v.editTooltip = sceneStopFirstText();
  v.exportTooltip = "The exported WAV places each scene end to end with its tail overlapping the next. It matches live playback except where a ringing voice would have been stolen.";
  return v;
}

// ---- Arrangement export (F18) ----

namespace {
std::string oneDecimal(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}
std::string barsWord(int n) { return std::to_string(n) + (n == 1 ? " bar" : " bars"); }
}  // namespace

std::string arrangementCancelledMessage() { return "Export cancelled. No files were written."; }
std::string arrangementBarsMessage(int totalBars) { return "Arrangement needs 8 to 32 bars (now " + std::to_string(totalBars) + ")."; }
std::string arrangementSceneDeletedMessage(int sceneNumber) { return "Scene " + std::to_string(sceneNumber) + " was deleted. Section removed."; }
std::string arrangementWriteErrorMessage(const std::string& path, const std::string& reason) { return "Could not write " + path + ": " + reason; }
std::string arrangementRunningMessage() { return "An arrangement export is already running."; }
std::string arrangementExportedMessage(const std::string& wavFileName, int bars, double seconds) {
  return "Arrangement exported: " + wavFileName + " (" + std::to_string(bars) + " bars, " + oneDecimal(seconds) + " s).";
}
std::string arrangementProgressMessage(int section, int sections, double fraction) {
  const int pct = static_cast<int>(std::lround(100.0 * std::min(1.0, std::max(0.0, fraction))));
  return "Exporting arrangement: section " + std::to_string(section) + " of " + std::to_string(sections) + " (" + std::to_string(pct) + "%).";
}

std::string arrangementRowText(int index, const ArrangementRowInput& r) {
  std::string scene;
  if (r.deleted) {
    scene = "Scene deleted";
  } else {
    scene = "Scene " + std::to_string(r.sceneNumber);
    const std::string plain = "Scene " + std::to_string(r.sceneNumber);
    if (!r.sceneName.empty() && r.sceneName != plain) scene += " \"" + r.sceneName + "\"";
  }
  std::string text = std::to_string(index) + "  " + scene + "  " + barsWord(r.bars) + " x " + std::to_string(r.repeats) + "   " + barsWord(r.bars * r.repeats);
  if (!r.deleted && !r.fits) text += " (does not fit the chops)";
  return text;
}

std::string arrangementTotalText(int totalBars, double bpm) {
  const double b = std::max(1.0, std::round(bpm));
  if (totalBars < 8 || totalBars > 32) return "Total: " + std::to_string(totalBars) + " bars (needs 8 to 32)";
  return "Total: " + std::to_string(totalBars) + " bars (" + oneDecimal(static_cast<double>(totalBars) * 4.0 * 60.0 / b) + " s at " + std::to_string(static_cast<int>(b)) + " BPM)";
}

std::string arrangementTruncationNote(int sceneNumber, int patternBars, int sectionBars) {
  if (patternBars <= sectionBars) return {};
  return "Scene " + std::to_string(sceneNumber) + " has " + std::to_string(patternBars) + " bars; using the first " + std::to_string(sectionBars);
}

std::vector<int> arrangementBarChoices(int patternBars) {
  std::vector<int> out;
  const int p = std::max(1, patternBars);
  for (int b : {1, 2, 4, 8})
    if (b % p == 0) out.push_back(b);
  if (out.empty()) out = {1, 2, 4, 8};
  return out;
}

bool arrangeButtonEnabled(int sceneCount, bool hasSource) { return sceneCount > 0 && hasSource; }
std::string arrangeButtonTooltip(int sceneCount) {
  return sceneCount > 0 ? "Build an 8 to 32 bar arrangement from your scenes and export WAV, MIDI and markers." : "Record at least one scene first (Evolve scenes).";
}

ArrangementView buildArrangementView(const ArrangementViewInput& in) {
  ArrangementView v;
  const int count = static_cast<int>(in.rows.size());
  const bool selected = in.selected >= 0 && in.selected < count;
  bool deleted = false, misfit = false;
  for (int i = 0; i < count; ++i) {
    const ArrangementRowInput& r = in.rows[static_cast<std::size_t>(i)];
    v.rows.push_back(arrangementRowText(i + 1, r));
    deleted = deleted || r.deleted;
    misfit = misfit || (!r.deleted && !r.fits);
    if (!r.deleted) {
      const std::string note = arrangementTruncationNote(r.sceneNumber, r.patternBars, r.bars);
      if (!note.empty()) v.notes.push_back(note);
    }
  }
  v.totalText = arrangementTotalText(in.totalBars, in.bpm);
  const bool inRange = count > 0 && in.totalBars >= 8 && in.totalBars <= 32;
  v.valid = inRange && !deleted && !misfit;
  v.editable = !in.running;
  if (in.running) v.statusText = arrangementProgressMessage(std::max(1, in.jobSection), std::max(1, in.jobSections), in.jobFraction);
  else if (count > 0 && !inRange) v.statusText = arrangementBarsMessage(in.totalBars);
  else if (deleted) v.statusText = "A section uses a scene that was deleted.";
  else if (misfit) v.statusText = "A section uses a scene that no longer fits the chops.";
  v.exportEnabled = v.valid && in.hasSource && !in.running;
  if (!v.exportEnabled && !in.running) {
    if (!in.hasSource) v.exportTooltip = "Load a source first.";
    else if (count == 0) v.exportTooltip = "Add at least one section.";
    else v.exportTooltip = v.statusText;
  }
  v.addEnabled = in.sceneCount > 0 && count < 32 && !in.running;
  v.removeEnabled = selected && !in.running;
  v.moveUpEnabled = selected && in.selected > 0 && !in.running;
  v.moveDownEnabled = selected && in.selected + 1 < count && !in.running;
  v.useChainEnabled = in.sceneCount > 0 && !in.running;
  v.autoFillEnabled = in.sceneCount > 0 && !in.running;
  v.closeLabel = in.running ? "Cancel" : "Close";
  v.progress = in.running ? std::min(1.0, std::max(0.0, in.jobFraction)) : 0.0;
  return v;
}

}  // namespace chopfractal::ui
