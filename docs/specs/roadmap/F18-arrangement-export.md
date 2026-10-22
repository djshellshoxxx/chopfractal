# F18 Arrangement Export

**Status:** Implemented (Arrange drawer; the Export drawer no longer carries a second arrangement section)  **Size:** L (9 engineer-days)  **Depends on:** F09 (scenes and their chain), F01 (extras tag 16), F13 (MIDI file helper and export folder). Soft: F16 (level match and limiter in the render), F17 (groove), E01/E02 (render through the same path)  **Blocks:** none
**Category:** value. A finished, DAW-ready arrangement from stored scenes.

## 0. Implementation status and decisions (2026-10-21)
Built and tested: `composition/{arrangement,arrangement_job}.hpp`, `src/arrangement.cpp`, `src/arrangement_job.cpp`, `src/arrangement_session.cpp`, the `wav_export` cue writer (`encodeWithCues`), the `midi_export` markers (`FileSpec::markers`, `endTick`), tag 16, and the view model in `plugin_ui_adapter/session_views.hpp`. Not built: the dialog and the FeaturePanel button (GUI worker, section 12). Where the body and the code differ, **the code listed here is the contract**:
- **Files.** The job lives in `arrangement_job.hpp` (model and state stay in `arrangement.hpp`, which has no audio types). A `ProjectSessionSnapshot` is the smaller `ArrangementPlan`: the scene `Playback`s (immutable, sharing the project's source), the MIDI notes of each scene pattern (taken with the F13 note mapping and velocity settings of the project), the output stage and the file settings. `ProjectSession::planArrangementExport(arrangement, name, settings)` builds it (message thread, const); `startArrangementExport(arrangement, name, settings)` plans and starts the worker. `ArrangementJob::start(plan)` is the spec's `start(snapshot, ...)`.
- **MIDI grid.** 960 ticks per quarter (the `midi_export` module), not 480: bars are 3840 ticks. Format 0, tempo and 4/4 meta, one `0xFF 0x06` marker per section at `startBar * 3840`, end of track at the last bar boundary. Notes that start inside a section are clipped to the end of their repeat (or of the section when whole pattern lengths fill it). The project's meter is assumed 4/4 for bar maths.
- **Bars and repeats.** `bars` is in {1,2,4,8}, `repeats` 1..8; the section is `bars * repeats` bars. When whole pattern lengths fill `bars` (a 2-bar pattern over 4 bars) the section is one continuous render of `bars * repeats / patternBars` cycles. Otherwise (a 4-bar pattern over 2 bars, a 3-bar pattern over 4) the render uses a copy of the playback that holds exactly `bars` bars of the pattern looped or cut, rendered `repeats` times in a row, so the pattern restarts at every repeat. Voices ring across repeat boundaries as in live playback.
- **Render path.** Each section is one `render::renderOffline` call (512-frame blocks, the same `Renderer::process` as playback), so a single-scene arrangement equals `renderOffline` of that scene sample for sample except for the last 50 ms of the tail (test `export_matches_live_render_of_one_scene`). Cancel and progress are checked by its callback every 64 blocks (about 0.7 s of audio; far below 100 ms of wall time).
- **Crossfade.** Overlap `N` = 10 ms or 50 ms of frames. The first `N` frames of section k+1 are scaled by `(i+1)/(N+1)` and the previous section's ring-out past its end by `1 - that`; the pair sums to 1 for every `i`. Total length is unchanged; `Cut` drops the ring-out.
- **Tail and fade out.** With `tail` the file is `totalBars * barFrames + ceil(2.0 s * rate)` frames, the last 50 ms a linear fade to exactly 0. `fadeOut` applies a cosine fade over the last bar (exactly 0 on the last frame) and **replaces the tail** (no tail frames are added), because a ring-out after a fade would jump back up.
- **Level match and limiter.** With `levelMatch` the job measures the source loop and a one-cycle render of every distinct scene (no gain, no limiter) and applies `levelmatch::computeMatch` per section (the F16 functions; silent scenes get 0 dB). The limiter runs once over the assembled mix: primed, `latencyFor(rate)` zero frames appended, the same number dropped from the start, so the length is unchanged. Sections themselves render with the limiter and match gain of the live stage switched off and the per-scene gain set.
- **Files.** `<name>_arrangement.wav`, `.mid`, `_arrangement_cues.txt` (`bar,seconds,name`, 1-based bar, `%.3f` seconds, LF); on a collision all three get `_2`, `_3`, ... (`<name>_arrangement_2.wav`). Everything is written as `*.partial` and renamed WAV, MIDI, cue list; on any failure or cancel the partials and already renamed files of this run are removed. `Write markers` off also drops the WAV cue chunk, the MIDI markers and the cue file. Labels are `Scene N <name>` (just the name if it already starts with `Scene N`).
- **Rates.** The render rate is `wavOptions.sampleRate`; the tempo is rounded to a whole BPM; the frame cap and the other limits give the section 3 messages.
- **State.** Tag 16 is registered with feature name `arrangement` (decoder failure drops only that section: `Some arrangement settings could not be restored.`). Loading a source or a project clears the list; deleting a scene keeps its row (flagged).
- **Test names.** Section 7 names are kept; additions: `stored_arrangements_are_validated_field_by_field`, `session_stores_section_16_only_when_not_default_and_restores_it`, `a_bad_section_16_drops_only_itself_with_the_warning`, `a_new_source_or_a_loaded_project_never_keeps_a_stale_arrangement`, `tail_ends_in_a_linear_fade_to_zero`, `notes_equal_the_scene_notes_repeated`, `level_match_gain_is_recomputed_per_scene`, `job_runs_on_a_worker_writes_three_files_and_suffixes_collisions`, `destroying_a_session_with_a_running_job_stops_it`; the view-model tests are in `plugin_ui_adapter/tests/test_session_views.cpp`; the GUI (xvfb) tests move with the dialog.

## 1. Summary and user value
Turn the scenes recorded in F09 (pattern snapshots with seeds and per-scene loop counts) into a **single arrangement** of 8 to 32 bars: one WAV of the whole arrangement, one MIDI file with the same notes, and per-section markers (WAV cue chunks and MIDI marker meta events, plus a sidecar text list). The render is deterministic, runs on a worker thread with a progress bar and cancel, and uses the same render path as normal playback so what you export is what you hear. Producers drop one file pair on a DAW timeline and have a verse/hook/break structure already cut.

## 2. User stories and scope
- Producer: open **Arrange...**, see my scenes as sections, set bars per section (1, 2, 4, 8) and repeat counts, reorder, remove, and see the total (8 to 32 bars) live.
- Producer: choose options (include tail, fade out, crossfade, MIDI on/off, markers on/off) and press **Export**.
- Producer: watch progress, cancel safely, and find `<name>_arrangement.wav`, `.mid`, `.cue.txt` in the export folder.
- Mixer: get stems? Not in scope (F11 bus rendering is separate).
**In scope:** arrangement model, validation (8..32 bars), offline render by sections, WAV cue chunk writing, MIDI markers, sidecar list, worker job with progress, UI dialog, state (arrangement list persists), tests. **Non-goals:** live arrangement playback in the plugin, per-section tempo changes, time-signature changes (uses the project meter, 4/4 assumed else bars count in the pattern's own bars), automation export, stems, section-specific effects settings (sections use the scene's stored pattern only), MIDI type-1 multi-track (single track like F13; markers as meta events).

## 3. UX
**Entry:** `TextButton` "Arrange..." (FeaturePanel row E, 90 px; title `Arrangement export`; tooltip `Build an 8 to 32 bar arrangement from your scenes and export WAV, MIDI and markers.`). Disabled with tooltip `Record at least one scene first (Evolve scenes).` until F09 has one or more scenes.
**Dialog (modal call-out window 560 x 420, `juce::DialogWindow`):**
- Left list (`ListBox`, title `Arrangement sections`): rows `1  Scene 3 "Verse"  4 bars x 2   8 bars`. Per row: scene `ComboBox` (title `Section scene`), bars `ComboBox` `1|2|4|8` (title `Section bars`, only up to the scene's pattern length multiple; a scene whose pattern is 2 bars offers `2, 4, 8`), repeats `Slider` 1..8 (title `Section repeats`), remove `TextButton` `X` (title `Remove section`). Buttons below: `Add section` (title `Add section`), `Move up`, `Move down`, `Use scene chain` (loads the F09 chain with its loop counts, trimmed to fit 32 bars), `Auto fill 16 bars` (distributes scenes round-robin, 4 bars each).
- Right column: `Label` `Total: 16 bars (32.0 s at 120 BPM)` (red-free; invalid state is text: `Total: 6 bars (needs 8 to 32)`), options: `ToggleButton` `Fade out last bar` (default off), `ToggleButton` `Add tail` (default on, renders the 2.0 s decay of the last hit), `ComboBox` `Section transition` `Cut|Crossfade 10 ms|Crossfade 50 ms` (default `Crossfade 10 ms`), `ToggleButton` `Write MIDI` (default on), `ToggleButton` `Write markers` (default on), `TextEditor` `Name` (default project name, sanitized as F13).
- Bottom: `ProgressBar` (title `Export progress`), `TextButton` `Export` (enabled when 8 <= total <= 32), `TextButton` `Cancel`/`Close`.
**Messages (dialog status label and StatusBar):** `Exporting arrangement: section 3 of 6 (42%).`; `Arrangement exported: verse_arrangement.wav (16 bars, 32.0 s).` (Info); `Export cancelled. No files were written.` (Info; partial files are deleted); `Arrangement needs 8 to 32 bars (now 6).` (Warning, button disabled); `Scene 4 was deleted. Section removed.` (Warning); `Could not write <path>: <reason>` (Error; temp files cleaned).
**Keyboard:** the list is navigable by arrows, Delete removes, Alt+Up/Down moves; Enter exports when enabled; Esc cancels or closes. Dialog focus trap and titles for screen readers.

## 4. Data model and state
```cpp
// composition/include/.../arrangement.hpp (portable-style struct; no JUCE)
struct ArrangementSection { std::uint32_t sceneId; std::uint8_t bars = 4; std::uint8_t repeats = 1; };
enum class Transition : std::uint8_t { Cut, Crossfade10, Crossfade50 };
struct Arrangement {
  std::vector<ArrangementSection> sections;      // 1..32 entries
  Transition transition = Transition::Crossfade10;
  bool fadeOut = false, tail = true, writeMidi = true, writeMarkers = true;
  int totalBars() const;                         // sum(bars * repeats)
  bool valid() const;                            // 8 <= totalBars <= 32 && sections non-empty
};
```
Project state (F01 StateSection tag 16 `arrangement`, schema 1): `u8 version=1, u8 flags (bit0 fadeOut, bit1 tail, bit2 writeMidi, bit3 writeMarkers), u8 transition, u8 count, then count x {u32 sceneId, u8 bars, u8 repeats}`; absent when the list is empty and defaults are untouched. A `sceneId` that no longer exists on load is kept and flagged invalid in the dialog (row text `Scene deleted`), never silently dropped.
No host parameters (export-only).
**Export file naming:** `<exportRoot>/<name>_arrangement.wav`, `.mid`, `_arrangement_cues.txt`; collisions get `_2`, `_3` (F13 rule). Cue list format: `bar,seconds,name` lines, UTF-8, LF.
**WAV cue chunk:** standard `cue ` chunk with one cue point per section start (sample offset) plus `LIST/adtl` `labl` sub-chunks holding the section names (`Scene 3 Verse`); chunk order after `data`; byte layout documented in `wav_export/README.md`.
**MIDI:** format 0, single track, 480 PPQ as F13, tempo meta (bpm), time signature meta, `0xFF 0x06` marker events at each section start with the same labels, notes from each scene pattern (groove baked when F17 groove has `bakeIntoMidi`), end of track at the last bar boundary.

## 5. Public API
```cpp
// composition
struct ArrangementProgress { int section = 0, sections = 0; double fraction = 0.0; };
struct ArrangementResult { Status status; std::string wavPath, midiPath, cuePath; int bars = 0; double seconds = 0.0; };
class ArrangementJob {                                 // owns one std::thread
 public:
  static std::unique_ptr<ArrangementJob> start(ProjectSessionSnapshot, Arrangement, std::string exportRoot, std::string name);
  ArrangementProgress progress() const;                // atomics, any thread
  bool done() const;  ArrangementResult result();      // after done
  void cancel();                                       // sets token; thread checks between blocks
};
Status ProjectSession::startArrangementExport(const Arrangement&, std::string name);   // message thread
const ArrangementJob* ProjectSession::arrangementJob() const;
```
`ProjectSessionSnapshot` is an immutable copy (source buffer shared_ptr, scene patterns by value, `RenderParams` from the same state the audio thread uses, tempo, groove, F16/E01/E02 settings, seeds). The job never touches live session objects. `wav_export` gains `writeWavWithCues(path, samples, rate, std::vector<CuePoint>)`; `midi_export` gains `writeMarkers` via `Options::markers`.

## 6. Behavior details and edge cases
**Timeline.** Bars are the pattern's own bars; section length in frames is `bars * barFrames`, `barFrames = round(60/bpm * 4 * rate)` computed once from the BPM (rounded to an integer BPM as playback does, with the manual/host tempo). A scene pattern shorter than the section is looped; longer is truncated at the section end (an Info line appears in the dialog: `Scene 2 has 4 bars; using the first 2`). Repeats multiply the section.
**Render.** Per section, `renderOffline` with the scene's `Playback` (built with the snapshot's source, groove, E01/E02 settings and F16 match gain, **match gain recomputed per scene using the same level_match functions** so each section is leveled to the loop), `cycles = repeats * ceil(bars / patternBars)`, then sliced to exactly the section frames. Voice state is reset at section boundaries (no tails carried) except when `Crossfade10/50`: the last N ms of section k (tail rendered past its end) is overlap-added with a linear equal-gain fade to the start of section k+1; total length unchanged. The final limiter (F16) runs once over the assembled stereo mix when enabled, then the file is latency-compensated. The `tail` option appends 2.0 s of the last section's decay (rendered with `scheduleEvents=false`) with a 50 ms fade to zero at the end; `fadeOut` applies a cosine fade over the last bar (before the tail).
**Determinism.** Everything is a pure function of the snapshot: scene patterns, seeds, BPM, settings. The WAV bytes are identical across runs, block sizes (the job renders in fixed 512 frame blocks) and machines of the same platform; the test hash is platform-specific only through floating-point libm in existing renderer code (the existing cross-platform policy applies; the test compares frames within 1e-6 across platforms in CI).
**Worker thread & progress.** One job at a time (second request returns `An arrangement export is already running.`). Progress fraction updates per 512-frame block through a relaxed atomic; the UI polls at 20 Hz. Cancel is checked per block; on cancel or error the temp files (`*.partial`) are removed. Files are written to temp names and renamed together at the end (WAV, then MIDI, then cue file), so a failure never leaves a mismatched pair. The audio thread is not involved (no change to `process()`); the job allocates freely.
**Size & limits.** 32 bars at 60 BPM is 128 s; stereo float32 at 96 kHz = 98 MB; the cap is `kMaxArrangementFrames = 96000 * 140` per channel; larger returns `Arrangement is too long for this sample rate.`. Output format follows F13/`wav_export` (default 24-bit PCM, dither none, same as the existing exporter options).
**Edge cases.** Scene with no events (silence): renders silence for its length with a marker; Level match reports silent and uses 0 dB. Deleted scene: row flagged, export blocked until fixed. Source removed: button disabled. Project sample rate change mid-dialog: totals recompute. Host transport is ignored; export works while the host plays; it uses a snapshot so live edits during export do not affect it. Name sanitizing as F13 (`[A-Za-z0-9._ -]`, trimmed, max 80). Export root missing: created; not writable: Error status.
**Interaction with others.** F11 stems are not part of the arrangement; F12 capture irrelevant; F14 stretch: uses the already stretched source; Level match caches are not used (the job recomputes per scene with the snapshot).

## 7. Test plan
Unit (`composition/tests/test_arrangement.cpp`): `total_bars_and_validity_8_to_32` (table cases 7, 8, 32, 33); `serialize_roundtrip_tag_16_and_absent_when_empty`; `deleted_scene_is_flagged_not_dropped`; `auto_fill_and_use_chain_trim_to_32`.
Render (`composition/tests/test_arrangement_render.cpp`): `length_equals_total_bars_times_bar_frames_plus_tail`; `section_boundaries_are_exact_at_integer_bar_frames`; `render_is_deterministic_two_runs_equal_bytes`; `render_independent_of_snapshot_copy_and_live_edits`; `crossfade_keeps_length_and_is_equal_gain` (sum of fade gains within 1e-6); `fade_out_reaches_zero_at_end`; `limiter_applies_once_over_the_final_mix_and_peak_below_ceiling`; `silent_scene_renders_silence_with_marker`; `shorter_scene_loops_longer_truncates`; `cancel_removes_partial_files_and_returns_cancelled`; `progress_is_monotonic_and_reaches_1`; `second_job_is_rejected_while_one_runs`.
Format (`modules/wav_export/tests/test_cues.cpp`, `modules/midi_export/tests/test_markers.cpp`): `cue_chunk_layout_is_valid_riff` (parsed by an independent reader in the test), `cue_offsets_equal_section_starts`, `labl_text_roundtrip_with_padding_to_even_length`; `midi_markers_at_section_ticks_with_meta_ff06`, `notes_equal_scene_notes_with_groove_baked_or_raw`, `midi_parses_back_with_midi_import` if present else with the project's smf parser.
GUI (xvfb): `arrange_button_disabled_without_scenes`; `list_edit_updates_total_label_and_export_enabled_only_8_to_32`; `keyboard_move_and_delete`; `export_flow_with_stub_job_shows_progress_and_final_message`; `titles_and_tooltips_exist`.
Sanitizers: ASan/UBSan on all; TSan on progress atomics and cancel. Manual QA: 16-bar arrangement of 4 scenes imported to a DAW lines up with the grid (marker positions at bars 1, 5, 9, 13); MIDI and WAV align to the sample at the host tempo; cancel mid-way leaves no files.

## 8. Acceptance criteria
- [x] Export is allowed only for 8 to 32 total bars and produces exactly three files (WAV, MIDI, cue list) or none.
- [x] WAV length is `totalBars * barFrames` (+ tail when on); cue points sit at exact section starts; MIDI markers at the same bar positions.
- [x] Output bytes are deterministic for equal snapshots; live edits during export have no effect.
- [x] The job runs on a worker thread, reports monotonic progress, cancels within 100 ms of the next block, and removes partial files; the audio thread and `process()` are untouched.
- [x] Per-scene level match and the final limiter behave as in F16; groove/E01/E02 are applied as in playback.
- [x] State tag 16 round-trips and is absent when default; a project without arrangement saves byte-identically; no manifest changes.
- [x] Exact status strings of section 3 exist as tested functions (`ui::arrangement*Message`) and session errors; docs updated.
- [ ] All dialog controls have titles and tooltips (the dialog is the GUI worker's).

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `composition/include/.../arrangement.hpp`, `src/arrangement.cpp`, `src/arrangement_job.cpp`, tests | New | model, snapshot, job |
| `composition/include/.../project_session.hpp`, `src/project_session.cpp`, `src/state_sections.cpp` | Mod **(hot)** | start/job accessors, tag 16 |
| `modules/wav_export/**` | Mod | cue chunk writer |
| `modules/midi_export/**` | Mod | markers |
| `plugin/src/ArrangementDialog.h/.cpp`, FeaturePanel button | New/Mod **(hot)** | UI |
| `composition/CMakeLists.txt` | Mod **(hot)** | sources |
| docs: `BUILD_STATUS.md`, `wav_export` and `midi_export` READMEs, roadmap README | Mod | docs, registries |

## 10. Risks and mitigations
- **Memory for long, high-rate arrangements.** Hard frame cap and clear error; stream the WAV to disk in 1 s chunks after assembling (assembly buffer capped).
- **Scenes recorded at another tempo/length.** Documented behavior: loops or truncates with an explicit Info line.
- **Cue chunk compatibility.** Verified against independent readers in tests and manual import in two DAWs (F15 matrix).
- **Divergence between playback and export.** Single render path, shared snapshot builder, parity test (`export_matches_live_render_of_one_scene` compares frames to a direct `renderOffline` of the same scene within 1e-6).
- **Cancel during file move.** The rename phase is not cancellable and takes milliseconds.

## 11. Implementation steps
1. Model, validation, state tag, tests. 2. WAV cue writer and MIDI markers with format tests. 3. Snapshot builder shared with playback. 4. Section render, crossfades, limiter, tail, fade, determinism tests. 5. Job thread, progress, cancel, atomic file finish. 6. Dialog UI and wiring. 7. Parity test, docs, registries, QA.

## 12. Commands the GUI must call
All calls run on the message thread under `proc.withSession(...)`. Row and button state comes from `ui::buildArrangementView(...)` (fill `ArrangementViewInput` from `arrangementRows()`, `arrangement().totalBars()`, the project tempo, `sceneList().scenes.size()`, `hasSource()`, `arrangementJob()` and its `progress()`); texts from `plugin_ui_adapter/session_views.hpp`. Poll `arrangementJob()->progress()` at 20 Hz while a job runs.
| Control | Call |
|---|---|
| "Arrange..." button, enabled state and tooltip | `ui::arrangeButtonEnabled(sceneCount, hasSource)`, `ui::arrangeButtonTooltip(sceneCount)` |
| Section list rows, Total label, status label, notes, enable rules | `ui::buildArrangementView(input)` |
| "Section scene" combo | `setArrangement(a)` with `a = arrangement()` and `a.sections[i].sceneId` changed |
| "Section bars" combo | choices from `ui::arrangementBarChoices(row.patternBars)`; then `setArrangement(a)` with `sections[i].bars` |
| "Section repeats" slider (1..8) | `setArrangement(a)` with `sections[i].repeats` |
| "Add section" | `setArrangement(a)` after `a.sections.push_back({sceneId, defaultBars(patternBars), 1})` (at most 32) |
| "Remove section" / Delete key | `setArrangement(a)` after erasing the row; when the row was `deleted`, also show `ui::arrangementSceneDeletedMessage(number)` |
| "Move up" / "Move down" / Alt+Up, Alt+Down | `setArrangement(a)` after swapping two rows |
| "Use scene chain" | `fillArrangementFromChain()` |
| "Auto fill 16 bars" | `autoFillArrangement()` |
| "Fade out last bar", "Add tail", "Section transition", "Write MIDI", "Write markers" | `setArrangement(a)` with `fadeOut`, `tail`, `transition`, `writeMidi`, `writeMarkers` |
| "Name" editor | text kept by the dialog; passed to `startArrangementExport` (empty means the project name) |
| "Export" (Enter) | `startArrangementExport(arrangement(), name, ArrangementExportSettings{root, bpm, wavOptions, levelMatch, matchTrimDb})`; `root` is the F13 export folder, `levelMatch`/`matchTrimDb` the F16 parameters; on error show `error().message` |
| Progress bar and status text | `arrangementJob()->progress()` and `ui::arrangementProgressMessage(section, sections, fraction)` |
| "Cancel" while running (Esc) | `cancelArrangementExport()`; the job ends with `ErrorCode::Cancelled` and `ui::arrangementCancelledMessage()` |
| Job finished | `arrangementJob()->done()` then `result()`: on success `ui::arrangementExportedMessage(filename(result.wavPath), result.bars, result.seconds)`, otherwise `result.status.error().message`; then `releaseArrangementJob()` |
| "Close" (Esc when idle) | close the dialog; a running job keeps running unless cancelled; closing the project or the plugin cancels and joins it |
| Project change | the list follows `arrangement()`; nothing to do for load, new source or reset (the session clears it) |
