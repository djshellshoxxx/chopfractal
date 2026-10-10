# F13 MIDI drag-out

**Status:** Ready for development  **Size:** M (6 engineer-days)  **Depends on:** F00 (HistoryPanel, FeaturePanel, StatusBar), F01 (export-settings persistence, optional; see section 4)  **Blocks:** none

## 1. Summary and user value
Drag the current groove, any variation from the family tree, or the whole Producer Kit out of the editor and drop it on a DAW track or sampler: a MIDI file that replays the pattern on the chop notes, a rendered WAV of the variation, or the kit files. Today a producer must use two file dialogs (`doExportWav`, `doExportKit`) and cannot export a variation other than the active one. Drag-out makes ChopFractal feel like a groove source that feeds the rest of the session in one gesture; MIDI settings (base note, velocity curve, loops, channel) make the file fit any drum rack.

## 2. User stories and scope
- As a producer I can drag the "Drag MIDI" handle onto a DAW MIDI track so that the current groove plays my sampler's pads.
- As a producer I can drag a node from the HistoryPanel tree to export that exact variation (not just the active one), Alt-drag for audio.
- As a producer I can drag "Drag Kit" to drop the slice WAVs plus the MIDI file into a drum rack.
- As a producer I can set base note, velocity curve, loops and MIDI channel once and have them remembered.
- **In scope:** `performExternalDragDropOfFiles` from three handles and HistoryPanel nodes; temp file creation, naming, retention and cleanup; per-variation MIDI/WAV rendering in `ProjectSession`; context-menu file export for variations ("Export MIDI...", "Export WAV..."); MIDI export settings; refactor of the exportKit MIDI builder into a shared helper.
- **Non-goals:** live MIDI output from the plugin (`producesMidi()` stays false), dragging INTO the editor (F05), stem export (F11 follow-up), tempo-map or multi-track MIDI, MPE, exporting effects (filter/glide/crunch/reverse/pitch are audio-only, as documented in export-wav-and-producer-kit.md), AU/AAX drag.

## 3. UX
**FeaturePanel, group "Drag out"** (placed right of the existing export buttons; F00 owns layout). Three `DragOutHandle` components (custom `juce::Component`, painted as a raised 104x28 px button, `DraggingHandCursor`, `setTitle` set):
- `"Drag MIDI"`: tooltip `"Drag the current groove into your DAW as a MIDI file. Chop 1 plays note 36 (C2)."` (note and name follow the settings).
- `"Drag Audio"`: tooltip `"Drag the current groove into your DAW as a WAV file."`
- `"Drag Kit"`: tooltip `"Drag the slice WAVs and the MIDI file into a sampler or drum rack."`
All three are disabled (greyed, tooltip `"Generate a pattern first."`) when `pattern() == nullptr`; Drag Kit also disabled with `"Too many chops to drag (64 max). Use Export Kit."` when more than 63 enabled chops (63 slices + 1 MIDI = 64 files).
Button **"MIDI options..."** toggles an inline section (not a popup) with: Slider **"Base note"** (0..127, default 36, value text `"C2 (36)"`; C3 = 60 convention), ComboBox **"Velocity"** (`"Linear"` default, `"Soft"`, `"Hard"`, `"Fixed"`), Slider **"Fixed velocity"** (1..127, default 100, enabled only for Fixed), ComboBox **"Loops"** (`"1"` default, `"2"`, `"4"`, `"8"`, `"16"`), ComboBox **"MIDI channel"** (`"1"`..`"16"`, default `"1"`). A read-only line shows `"Tempo in the file: 120.0 BPM (host tempo)"` or `"... (manual tempo)"` from `activeBpm`/`usedFallbackTempo`.
**Gesture:** mouse down on a handle does nothing; after the pointer moves 6 device-independent px with the button down, the editor renders the file(s) and calls `juce::DragAndDropContainer::performExternalDragDropOfFiles(paths, /*canMoveFiles*/ false, &handle, callback)`. Escape or dropping on a non-accepting target cancels silently. A click without movement shows the status `"Drag this button onto a DAW track."`.
**HistoryPanel:** drag from a node beyond the same threshold exports that node's variation as MIDI; hold Alt (Option on macOS) at drag start for WAV. Activation currently happens in `ChopFractalEditor::mouseDown` (history branch); it moves to mouse-up when the pointer moved < 6 px, so a drag never switches the active variation (existing behavior for plain clicks, and `Quantize::LoopBoundary` while playing, is preserved). Node tooltip: `"Drag to your DAW: MIDI. Alt-drag: audio."`. The node context menu (`historyMenu`) gains `"Export MIDI..."` and `"Export WAV..."` after "Compare with active variation"; they open a `juce::FileChooser` (`"Export variation as MIDI"`, default `chopfractal_<label>_v<id>.mid` in `userMusicDirectory`, pattern `"*.mid"`) and write with overwrite confirmation exactly like `doExportWav`.
**Messages (StatusBar):** `"Dragging chopfractal_groove_v12_120bpm.mid"` (Info, while the drag runs); `"Drop finished."` is not shown (silence on success); errors: `"Could not render the MIDI file: <reason>"`; `"Too long to drag: reduce Loops in the export settings (limit 120 s of audio)."`; `"N events were skipped because their chops no longer exist."` (Info, variation drag); `"Could not create a temporary file. Use Export instead."`; `"The audio for this project is missing; MIDI can still be dragged, audio cannot."` when `sourceMissing()` (Drag Audio/Kit disabled with that tooltip).
Persistent hint under the group (grey, 11 px): `"Drag-out files are temporary and kept for 7 days. Use Export for files you want to keep. In REAPER, enable 'Copy imported media to project media directory' for dragged audio."`
Keyboard: handles are focusable; Space/Enter on a handle does nothing (dragging has no keyboard equivalent); the Export menu items and File choosers are the accessible path.

## 4. Data model and state
New composition types (`composition/include/.../project_session.hpp`):
```cpp
enum class VelocityCurve : std::uint8_t { Linear = 0, Soft = 1, Hard = 2, Fixed = 3 };
struct MidiExportOptions {
  int baseNote = 36;                      // 0..127; chop n (snapshot order) -> baseNote + n
  double bpm = 120.0;                     // 20..999, written as the tempo event
  int loops = 1;                          // 1..16 repeats of the pattern
  VelocityCurve curve = VelocityCurve::Linear;
  int fixedVelocity = 100;                // 1..127, used by Fixed
  int channel = 0;                        // 0..15 (UI shows 1..16)
  std::string trackName = "chopfractal";  // <= 60 chars
};
```
`KitOptions` keeps its fields (`baseNote`, `bpm`, `loops`, `overwrite`, `name`) and gains `VelocityCurve curve = Linear; int fixedVelocity = 100; int channel = 0;`. With defaults, `pattern.mid` is byte-identical to today's output.
Velocity formulas (`l = e.tx.level`, `lc = min(l, 1.0)`): Linear `clamp(lround(l*127),1,127)` (exactly today's rule), Soft `clamp(lround(127*lc*lc),1,127)`, Hard `clamp(lround(127*sqrt(lc)),1,127)`, Fixed `fixedVelocity`.
Persistence: **session blob unchanged** (no new `ProjectSession` state). MIDI settings are plugin-level export preferences, stored in `apvts.state` properties exactly like `manualBpm`/`embedSource` today: `midiBaseNote`, `midiVelocityCurve`, `midiFixedVelocity`, `midiLoops`, `midiChannel`; missing properties load as defaults, values are clamped on load, so old files and old builds interoperate (an old build ignores the unknown properties). If F01's export-settings block has landed, `MidiExportSettings {u8 baseNote; u8 curve; u8 fixedVelocity; u8 loops; u8 channel;}` is appended to it (append-only, behind the block's length prefix) instead; the property names above remain the read fallback for one release. Host parameters: none (manifest version unchanged). Golden hashes unaffected (no pattern serialization change): 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128.
Temp files: `<tempDirectory>/ChopFractal/drag/<pid>-<counter>/<name>`; constants `kDragFileRetentionDays = 7`, `kMaxDragFiles = 64`, `kMaxDragAudioSeconds = 120.0`.

## 5. Public API
Portable `modules/audio_renderer/include/.../renderer.hpp` (small refactor, no behavior change): extract the retrigger expansion from `makePlayback` into `FlatEventList expandRetriggers(const FlatEventList& events);` (non-RT, same sub-event timing/levels with `kRetriggerDecay` 0.8); `makePlayback` calls it. This lets MIDI export use the identical sub-hits as audio without needing a decoded source.
Composition (`project_session.hpp`), all message/worker thread, const:
```cpp
// MIDI bytes for the pattern stored in a history node (or the live pattern when node == history().active() and the pattern is uncommitted).
Result<std::vector<std::uint8_t>> renderVariationMidi(history::NodeId node, const MidiExportOptions& o, int* skippedEvents = nullptr) const;
Result<std::vector<std::uint8_t>> renderCurrentMidi(const MidiExportOptions& o) const;   // patterns_.current()
// Offline WAV bytes of a node, same path as exportWav (makePlayback + renderOffline + wav::encode).
Result<std::vector<std::uint8_t>> renderVariationWav(history::NodeId node, const ExportWavOptions& o) const;
Result<std::vector<std::uint8_t>> renderCurrentWav(const ExportWavOptions& o) const;
Result<ExportReport> exportVariationMidi(history::NodeId node, const std::string& path, const MidiExportOptions& o, bool overwrite) const;
// Slice WAV bytes + names for dragging the kit (same encode as exportKit: Pcm24, fades 32/64 frames).
struct KitFile { std::string name; std::vector<std::uint8_t> bytes; };
Result<std::vector<KitFile>> renderKitFiles(const KitOptions& o) const;
```
Private shared helper: `Result<midi::FileSpec> buildMidiSpec(const FlatEventList& expandedEvents, Ticks lengthTicks, TimeSignature ts, const ChopSnapshot& chops, const MidiExportOptions& o) const;` extracted from `exportKit` (the loop at `for (int loop...)`); `exportKit` calls it, so kit output stays byte-identical. Node path: `history_.getSnapshot(node)` -> `pattern::deserialize` -> `pattern::sanitize(p, *chops_)` (as `applyActivation` does; count of dropped events returned via `skippedEvents`) -> `pattern::flatten(p, *chops_)` -> `render::expandRetriggers` -> `buildMidiSpec` -> `midi::encode`. Time signature from `p.settings.timeSignature`, length from `pattern::lengthTicks(p)`. Errors reuse `ErrorCode` (`OutOfRange` loops/baseNote/velocity, `LimitExceeded` > `midi::kMaxNotes` 65536, `NotFound` unknown node, `Corrupt` bad payload hash, `InvalidArgument` no source/pattern).
Plugin (`plugin/src/`, JUCE only): `class DragFileSet` (RAII, creates the unique directory, `File add(name, bytes)`, `StringArray paths()`), `void sweepOldDragFiles()` (called from the `ChopFractalProcessor` constructor and before each new drag; deletes `drag/*` sub-directories whose modification time is older than `kDragFileRetentionDays`), `class DragOutHandle : public juce::Component` with `std::function<std::unique_ptr<DragFileSet>()> makeFiles` and `void mouseDrag(const MouseEvent&)`. The editor (or `FeaturePanel`/`HistoryPanel` from F00) owns the handles and implements `makeFiles` via `proc_.withSession(...)`. `ChopFractalEditor` need not derive from `DragAndDropContainer` for external file drags (the call is static). Threading: all message thread; WAV render runs synchronously inside `mouseDrag` (see 6) and never touches the audio thread; no new real-time code.

## 6. Behavior details and edge cases
**MIDI content:** identical rules to `exportKit`: format 0, 960 ticks/quarter, tempo + time-signature events, one note per played hit and per retrigger sub-hit, note = `baseNote + chopIndex` where `chopIndex` is the chop's position in the **current** `chops()->chops` order, `start = loop*lengthTicks + e.start`, `duration = e.duration`, same-pitch overlaps clipped by `midi::encode`. Notes outside `0..127` (baseNote + n > 127) fail with the existing message `"the chops do not fit into MIDI notes 0 to 127 from this base note"` and `hint = 127 - n + 1` (UI clamps the slider's max to `127 - chops + 1` live). Variation events whose chop no longer exists are skipped and counted; chops added later never appear (they have no events).
**WAV:** `ExportWavOptions` built from the existing export combos (`exportFormat_`, `exportRate_`, `exportLoops_`) with `bpm = activeBpm`, `tailSeconds = 0.5`, `normalize=false`; a drag whose estimated length `loops*lengthSeconds + tail` exceeds 120 s is refused (message above). Render is synchronous in `mouseDrag`; budget: a 4-bar, 120 BPM, 4-loop 24-bit/48 kHz render must finish in <= 400 ms on the CI runner (test). F01's worker/progress export applies to the Export menu items, not to drag.
**Files and lifetime:** per drag a new `DragFileSet` directory is created; names are `chopfractal_<sanitizeFileName(label)>_v<nodeId|current>_<bpm rounded>bpm.mid|.wav` (`wav::sanitizeFileName`, <= 48 chars); kit files keep `slice_NN_<role>.wav`, `chopfractal.mid`. Files are written with `wav::writeFileAtomic(path, bytes, true)`. The drag callback only clears the status text; **files are not deleted when the drop callback fires** (hosts copy or reference asynchronously). They are deleted by `sweepOldDragFiles()` when older than 7 days, at plugin construction and before each drag. Processor destruction deletes nothing. Collisions are impossible (unique directory). A failed write removes the directory and shows the "Could not create a temporary file" message; partial sets are never offered to the OS.
**Platform behavior:** Windows and macOS: supported by JUCE 8.0.15. Linux/X11 (XDND): supported; Wayland-only hosts run the plugin through XWayland, where drops may be refused by the host; the manual matrix records this, the Export menu is the fallback. A host that does not accept file drops simply ignores the drag; there is no error signal, which is why the persistent hint exists.
**Interactions:** drag never mutates the session (no history node, no undo entry, no notices except the skipped-events Info). Locks, Evolve and A/B do not matter (the node's saved pattern is exported). While Evolve is running the "current" pattern can change between mouse down and drag: `renderCurrentMidi` snapshots `patterns_.current()` under the session lock at drag start, so the file matches what the user saw at the threshold crossing. A pending (quantized) activation does not affect export. `sourceMissing()`: MIDI works (it needs only the chop map and pattern); audio paths return `InvalidArgument` and the handles are disabled. History node corruption (`getSnapshot` hash mismatch) -> `"Could not render the MIDI file: <ProjectSession error>"`.
**Determinism:** `renderVariationMidi` is a pure function of (node payload, chops, options); two calls give identical bytes, and `renderCurrentMidi` for the active node equals `renderVariationMidi(active)` when the live pattern is the committed one.

## 7. Test plan
Unit:
- `modules/audio_renderer/tests`: `expandRetriggers_matches_makePlayback_events` (retrigger 3 and 8, levels x0.8, starts `start + duration*k/n`, durations sum to the original).
- `modules/midi_export/tests`: no module change; add `velocity_boundaries_clamped` only if encode semantics are touched (they are not).
Composition (`composition/tests`):
- `kit_pattern_mid_is_byte_identical_after_refactor`: fixed seed session, pin FNV-1a of `pattern.mid` captured BEFORE the refactor in step 1 of section 11.
- `variation_midi_matches_activated_pattern_midi`: generate 3 variations, for each node N: `renderVariationMidi(N)` bytes == bytes after `activate(N, Immediate)` + `renderCurrentMidi`.
- `variation_midi_decodes_with_expected_notes`: `midi::decode` of the bytes: note count == retrigger-expanded flat event count x loops; every note in `[baseNote, baseNote+chops)`; `start` multiples consistent; tempo == options.bpm.
- `velocity_curves`: level 0.5 -> Linear 64 (round 63.5), Soft 32 (127*0.25 = 31.75 -> 32), Hard 90 (127*0.7071 = 89.8 -> 90), Fixed 100; level 4.0 -> 127 for all but Fixed; level 0.001 -> 1.
- `deleted_chop_events_are_skipped_and_counted`: delete a marker after variation creation, `skippedEvents > 0`, still ok.
- `base_note_overflow_is_reported_with_hint`; `loops_17_rejected`; `unknown_node_is_not_found`; `corrupt_payload_returns_corrupt`.
- `variation_wav_equals_exportWav_for_active_node`: bytes of `renderCurrentWav` equal the file written by `exportWav` (same options).
- `renderKitFiles_equal_exportKit_files`: byte equality per slice name.
Plugin shell/GUI (`plugin/tests/test_plugin_shell.cpp`; `performExternalDragDropOfFiles` needs a real window system, so the test seam is `makeFiles`):
- `drag_handles_are_disabled_without_a_pattern_and_enable_after_generate` (click Load, Detect, Generate buttons; assert `isEnabled()`).
- `drag_midi_makefiles_creates_a_valid_mid_in_the_temp_set`: call the handle's `makeFiles()`, assert one file, name matches `chopfractal_*_v*_*bpm.mid`, `midi::decode` ok, directory under `tempDirectory/ChopFractal/drag`.
- `drag_kit_refuses_more_than_63_chops` (65 markers).
- `history_click_activates_but_drag_does_not`: simulate mouse down/up at a node with 0 px movement -> `history().active()` changes; with 10 px movement -> unchanged.
- `sweep_old_drag_files_deletes_only_expired_sets`: create sets with `setLastModificationTime` 8 days and 1 day old; sweep; first removed, second kept.
- `midi_settings_roundtrip_in_state_and_missing_properties_default`.
Determinism/sanitizers: golden tests unchanged; ASan/UBSan over new composition tests; no audio-thread code changes (TSan job unchanged).
Manual QA (REAPER, Ableton Live, FL Studio, Bitwig):
1) Load a drum loop, Smart Setup, Generate. 2) Drag MIDI onto an empty MIDI track: item appears at the drop position with notes 36.. matching the pattern lane order; tempo matches. 3) Set Base note 60 and Velocity Fixed 90; drag again: notes start at C3, all velocity 90. 4) Drag a node from the history tree (not the active one): the notes equal that variation (compare with Compare text); the active variation did not change. 5) Alt-drag the node onto an audio track: WAV plays the variation. 6) Drag Kit onto Ableton Drum Rack/Simpler: slices and MIDI import. 7) Delete the temp folder, reopen the plugin: no error; sweep leaves newer sets. 8) Right-click node > Export MIDI...: file written, overwrite prompt works. 9) Linux/Bitwig: record success or the "host refused drop" outcome in the matrix.

## 8. Acceptance criteria
- [ ] Drag of MIDI/Audio/Kit/variation lands as a usable file in REAPER and Bitwig (Windows or macOS) and in Ableton Live and FL Studio where the host accepts file drops; outcomes recorded in the F15 test matrix.
- [ ] Default-option `pattern.mid` from `exportKit` is byte-identical before and after the refactor (pinned hash).
- [ ] `renderVariationMidi(N)` equals the MIDI of the activated node for every node in a 10-node tree.
- [ ] Dragging never changes the session: `saveState` bytes identical before and after a drag (test).
- [ ] 4-bar x 4-loop WAV drag render completes in <= 400 ms; MIDI render <= 20 ms for 512 events x 16 loops.
- [ ] Temp files older than 7 days are removed; younger files and other instances' files are never removed; no temp file is deleted by the drop callback.
- [ ] No new host parameters; manifest version unchanged; golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 unchanged; old projects/state load unchanged.
- [ ] `tools/check_modules.py` passes (no JUCE in `modules/` or `composition/`).

## 9. Files touched
| path | new/modified | change |
|---|---|---|
| modules/audio_renderer/{include/.../renderer.hpp,src/renderer.cpp,tests/test_renderer.cpp} | modified | `expandRetriggers` extraction |
| composition/include/.../project_session.hpp, composition/src/project_session.cpp | **HOT** modified | node-based render API declarations, `MidiExportOptions`, snapshot access helper |
| composition/src/features.cpp | modified | `buildMidiSpec`, `exportKit` refactor, new render/export functions, velocity curves |
| plugin/src/DragOut.h, plugin/src/DragOut.cpp | new | `DragFileSet`, `sweepOldDragFiles`, `DragOutHandle` |
| plugin/src/FeaturePanel.cpp/.h, plugin/src/HistoryPanel.cpp/.h (from F00; else plugin/src/PluginEditor.cpp **HOT**) | modified | handles, MIDI options, node drag, context-menu items, click-vs-drag activation |
| plugin/src/PluginProcessor.cpp/.h | **HOT** modified | MIDI settings properties in get/setStateInformation, sweep at construction |
| plugin/CMakeLists.txt | **HOT** modified | add DragOut.cpp to both targets (plugin and ChopFractalShellTests) |
| plugin/tests/test_plugin_shell.cpp | modified | new tests |
| docs/specs/export-wav-and-producer-kit.md | modified | velocity curves, channel, variation export |

## 10. Risks and mitigations
- Host does not accept or later cannot find dragged files (reference-by-path hosts): 7-day retention, persistent hint, Export menu fallback; matrix test per DAW.
- macOS needs a live mouse event for `performExternalDragDropOfFiles`; calling it from `mouseDrag` only (never a timer) avoids the failure; render synchronously to stay inside the gesture.
- Synchronous WAV render stalls the UI for long patterns: 120 s cap + 400 ms budget test; fall back to MIDI-only for longer.
- Activation behavior change in HistoryPanel (mouseDown -> mouseUp) could break F02/F06 flows: covered by `history_click_activates_but_drag_does_not` and existing GUI test updates.
- Wayland refusal: documented, not fixable in the plugin.

## 11. Implementation steps
1. Pin the current `pattern.mid` hash test (against unchanged code) and the byte-equality tests for `exportKit`; commit.
2. `expandRetriggers` extraction + test (no behavior change).
3. `buildMidiSpec` extraction, `MidiExportOptions`, velocity curves, `KitOptions` additions; step-1 tests still pass.
4. `renderVariationMidi`/`renderCurrentMidi`/`exportVariationMidi` + composition tests.
5. `renderVariationWav`/`renderCurrentWav`/`renderKitFiles` + equality tests.
6. `DragOut.cpp` (`DragFileSet`, sweep) + tests.
7. FeaturePanel handles and MIDI options, state properties; shell tests.
8. HistoryPanel node drag, click-vs-drag, context-menu exports; GUI tests.
9. Manual QA across the DAW matrix; update docs.
