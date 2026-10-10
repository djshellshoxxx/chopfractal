# F01 State v2 and persistence

**Status:** Ready for development  **Size:** L (9 engineer-days)  **Depends on:** F00 for the editor wiring (steps 1-5 are editor-free and can start now)  **Blocks:** F02, F03, F04 (they store prefs here), F05 (export progress UI), F09, F10

## 1. Summary and user value
Today a saved project loses the seed, the Fractal motif/depth, the role selector, export choices, A/B snapshots and every UI choice; restoring a reference-only project decodes its audio file on the host's calling thread; export blocks the message thread; and `MigrationRegistry` (modules/state_codec) has no registered step. F01 makes one coordinated version bump (project session schema 1 to 2, plugin wrapper 1 to 2), registers the first real migration, persists the missing items, restores audio on a worker thread, and runs exports on a worker with progress and cancel. Producers get projects that reopen exactly as left and a DAW that does not freeze on load or export.

## 2. User stories and scope
- As a producer I reopen a project and my seed, motif, A/B snapshots and view choices are as I left them.
- As a producer loading a big project in my DAW, the host UI stays responsive while the audio file is decoded.
- As a producer I can export a long WAV or kit, watch progress, and cancel without leaving partial files.
- As a developer I add a persisted UI preference by adding a field, not by designing a format.

**In scope:** the persisted list in section 4; `kSessionSchemaVersion` 1 to 2 with an identity migration registered; plugin wrapper version 1 to 2 (lazy); `ProjectSession` extras API; `PatternSession` slot accessors; worker restore; `ExportJob` + worker + cancel; `ErrorCode::Cancelled`; `renderOffline` progress callback.
**Non-goals:** persisting `evolve.enabled` (a project never starts changing by itself; unchanged rule), saving the waveform zoom window, saving selection, preset files (F03), notice log (F05), moving `manualBpm`/`embedSource` out of the APVTS XML (they already persist there; this spec only pins them with tests), MIDI export settings (F13), downgrade compatibility (an older build rejects v2 files with its existing message).

## 3. UX
Strings (all new, exact):
- While restoring audio: `Restoring audio from <file name>...`; success `Restored audio: <file name>`; wrong file `The file for "<name>" is not the audio this project was saved with. Markers and pattern are kept; relink the original file.` (the existing text); missing file `The source "<name>" was not found at <path>. Markers and pattern are kept; relink the file to hear playback.` (existing text).
- Export start: `Exporting <file name>...`; running: `Exporting <file name>: 37%` (integer percent, updated at the 10 Hz editor tick); done: `Exported <file name>` (+ existing clipped suffix); kit done: `Producer Kit written to <dir> (<n> files)` (existing); cancelled: `Export cancelled.`; second request: `An export is already running.`; failure: the error message.
- StatusBar (F00 panel) gains, only while an export runs, a `TextButton` titled and labelled `Cancel Export` (110 x 20, right aligned, `reduced(2)`), whose click calls `proc.cancelExport()`. `Export WAV` and `Export Kit` are disabled while running (FeaturePanel reads `model.exportRunning()`).
- Opening a v2 project in an older build: that build shows its existing `Could not restore the project: ...` message (documented, not fixed).
No other visible change; restored values appear in the existing widgets (seed field, Motif, Depth, role combo, Format/rate/Loops combos, detection-mode combo).

## 4. Data model and state
### 4.1 Newly persisted items (the complete list)
| Item | Field | Type / range | Default (omitted from file when equal) |
|---|---|---|---|
| Seed field | `ProjectExtras::seed` | u64, 0..999999999999 | 1 |
| Fractal motif | `motif` | 1..8 chars of `x a .` | `"x.xx"` |
| Fractal depth | `fractalDepth` | 1..3 | 2 |
| Role combo | `roleIndex` | 0..8 into `roles::builtinRoles()` | 0 |
| Detection mode | `detectMode` | 0..3 (= `DetectChoice` - 1) | 0 |
| Generation grid | `gridDivision`, `gridTriplet` | {1,2,4,8,16,32}, bool | 16, false |
| Export UI | `exportUi.formatIndex/rateIndex/loopsIndex` | 0..2 / 0..3 / 0..4 | 1 / 1 / 1 |
| View mode | `ui.viewMode` | 0 Simple, 1 Advanced (F04) | 1 |
| UI scale | `ui.scalePercent` | 50..200 (F04) | 100 |
| Editor size | `ui.width`, `ui.height` | 640..4096, 480..4096 (F04) | 940, 860 |
| Marker snap | `ui.markerSnap` | 0 Off, 1 Transient, 2 ZeroCrossing (F02) | 1 |
| Grid snap | `ui.gridSnap`, `ui.snapDivision` | bool; 0 = pattern grid else {1,2,4,8,16,32} (F02) | true, 0 |
| Tree filter | `ui.favoritesOnly`, `ui.search` | bool; <= 32 bytes (F03) | false, "" |
| A/B snapshots | slots A and B | complete `pattern::Pattern` each, <= 1 MiB serialized | empty |
Already persisted and only pinned by tests: Evolve `everyLoops/amount/ramp/rampCeiling/startSeed` (session block), `manualBpm`, `embedSource` (APVTS XML properties), embedded flag, markers, roles/rules, history, favorites.

### 4.2 Types (composition/include/chopfractal/composition/project_session.hpp, **HOT**)
```cpp
constexpr std::uint32_t kSessionSchemaV1 = 1;
constexpr std::uint32_t kSessionSchemaVersion = 2;   // was 1
struct ExportUiSettings { int formatIndex = 1; int rateIndex = 1; int loopsIndex = 1; };
struct UiPrefs {
  int viewMode = 1; int scalePercent = 100; int width = 940; int height = 860;
  int markerSnap = 1; bool gridSnap = true; int snapDivision = 0;
  bool favoritesOnly = false; std::string search;
};
struct ProjectExtras {
  std::uint64_t seed = 1; std::string motif = "x.xx"; int fractalDepth = 2; int roleIndex = 0; int detectMode = 0;
  int gridDivision = 16; bool gridTriplet = false; ExportUiSettings exportUi; UiPrefs ui;
};
bool operator==(const ProjectExtras&, const ProjectExtras&);
Status validate(const ProjectExtras&);               // OutOfRange / InvalidArgument per field, message names the field
```
### 4.3 Wire format
Container `codec::kContainerVersion` stays 1. Module `"composition"`:
- **v1 payload (unchanged bytes):** `u8 embedded` then `u8 1` + `i32 everyLoops, f64 amount, f64 ramp, f64 rampCeiling, u64 startSeed` (38 bytes).
- **v2 payload = the 38 v1 bytes followed by an optional extras section:** `u8 0xE2`, `u16 blockCount` (<= 16), then blocks `u8 tag, u32 length, bytes[length]`. Tags: 1 Seed `u64`; 2 Fractal `u8 depth, u8 len, chars`; 3 Role `u8`; 4 Export `u8 fmt, u8 rate, u8 loops`; 5 Detect `u8`; 6 Grid `u8 divisionCode (log2 of the division: 1->0 ... 32->5), u8 triplet`; 7 Ui `u8 viewMode, u16 scale, u16 w, u16 h, u8 markerSnap, u8 gridSnap, u8 snapDivisionCode, u8 favoritesOnly, u8 searchLen, chars`; 8 AB `u8 mask (bit0 A, bit1 B)` then per set bit `u32 len, pattern::serialize bytes`. Only blocks that differ from the default are written. Unknown tags are skipped by length; a duplicated tag, a length past the end, or total extras > 4 MiB makes the whole extras section invalid.
- **Lazy versioning (this is how "feature-free state serializes unchanged" holds):** `saveState` writes schema 2 only when extras differ from defaults or an A/B slot is occupied; otherwise it writes schema 1 and the exact old bytes. The plugin wrapper (`kStateMagic "CFPL"`, `PluginProcessor.cpp`) writes `kStateVersion` 2 under the same condition, else 1, and reads 1..2. The pattern, source, roles, history, audio module payloads and their schema versions (`pattern::kSchemaVersion`=1 etc.) do not change, so the four pattern_engine golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 are untouched.
- **Migration wiring:** in `ProjectSession::ProjectSession()` register `migrations_.add("composition", 1, identity)` where `identity` returns its input (a v1 payload is a valid v2 payload with no extras). `loadState` already calls `take(kModSession, kSessionSchemaVersion, ...)`, which now targets 2 and runs the registered step for v1 files. Policy for every future bump: add the module's `kSchemaVersion`, register one step per version, add a fixture test.
- **Failure policy:** a malformed extras section never fails the project load: extras fall back to defaults and a `Notice::Level::Warning` `Some editor settings could not be restored.` is queued. A/B slot that fails `pattern::deserialize` or `flatten` against the loaded chops is dropped individually with `Snapshot A could not be restored and was dropped.` (or `B`). Out-of-range individual values are clamped by `validate` fallback to the field default.
- **Host parameters:** none added; manifest version stays 2.

## 5. Public API
`modules/pattern_engine/include/chopfractal/pattern_engine/session.hpp` (portable):
```cpp
std::shared_ptr<const Pattern> slot(std::size_t slot) const;                         // nullptr if empty or slot >= 2
void restoreSlots(std::array<std::shared_ptr<const Pattern>, 2> slots);              // after restore(); keeps current pattern/undo
```
`modules/chop_contracts/.../result.hpp` (portable): append `Cancelled` to `ErrorCode` (last value; do not reorder). `modules/audio_renderer/.../renderer.hpp`: `using ProgressFn = std::function<bool(double)>; Result<...> renderOffline(std::shared_ptr<const Playback>, const OfflineSettings&, const ProgressFn& = {});` calls the function every 64 blocks and once at the end with 1.0; returning false aborts with `makeError(ErrorCode::Cancelled, "export cancelled")`.
`composition/include/chopfractal/composition/project_session.hpp` (message thread, under the session lock):
```cpp
const ProjectExtras& extras() const;
Status setExtras(const ProjectExtras& e);                      // validate; store; no history node, no undo step, no republish
bool hasSnapshot(std::size_t slot) const;                      // delegates to patterns_
// loadState gains options; the old 3-argument overload forwards with defaults.
struct LoadOptions { bool deferSourceResolve = false; };       // true: never call the resolver, no "not found" notice
Status loadState(const std::uint8_t*, std::size_t, const SourceResolver&, const LoadOptions&);
void reportSourceUnavailable(bool wrongAudio);                 // queues the two existing warning notices
```
`composition/include/chopfractal/composition/export_job.hpp` (new, portable, no JUCE, no threads inside):
```cpp
struct ExportProgress { std::atomic<float> fraction{0.f}; std::atomic<bool> cancel{false}; };
class ExportJob {                                              // immutable after planning; safe to run on any thread
 public:
  static Result<ExportJob> planWav(const ProjectSession&, std::string path, const ExportWavOptions&);   // same validation, same order and messages as exportWav today
  static Result<ExportJob> planKit(const ProjectSession&, std::string directory, const KitOptions&);    // same as exportKit
  Result<ExportReport> run(ExportProgress&) const;             // worker thread; never touches the session
  const std::string& displayName() const;                      // file name or directory name
};
```
`ProjectSession::exportWav/exportKit` stay (existing tests keep passing) and become `plan` + `run` with a throwaway `ExportProgress`. `run` for a WAV renders through `renderOffline(..., progress callback)` (fraction 0 to 0.9), encodes (0.95), `wav::writeFileAtomic` (1.0); cancel before the write leaves no file. Kit: fraction = files written / (n+2); cancel is checked before each file and deletes the files this run created (`std::filesystem::remove`, errors ignored) then returns `Cancelled`.
`plugin/src/PluginProcessor.h` (**HOT**, message thread unless noted):
```cpp
struct ExportRequest { enum class Kind { Wav, Kit } kind = Kind::Wav; std::string path; composition::ExportWavOptions wav; composition::KitOptions kit; };
struct ExportStatus { enum class State { Idle, Running, Done, Failed, Cancelled } state = State::Idle; float fraction = 0.f; };
cf::Status startExport(const ExportRequest&);   // plans under the lock, runs on a juce::Thread; Conflict "An export is already running."
void cancelExport();                            // sets progress.cancel
ExportStatus exportStatus() const;              // any thread (atomics)
bool restoreInFlight() const;                   // a reference-only audio restore is decoding
```
`EditorCommands::exportWav/exportKit` (F00) call `startExport`; the FileChooser flow is unchanged. EditorModel gains `exportRunning()`, `exportFraction()` and `setExtras(mutator)`; seed/motif/depth/role/detect/export combos write extras through it (a keystroke or selection change calls `ProjectSession::setExtras` and `updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true))`). `EditorModel::Change::Extras` is emitted when `session.extras()` differs from the last published copy (project load, preset apply); panels re-sync widgets with `dontSendNotification`.
Real-time rule: none of this runs on the audio thread. `loadState` still publishes only through the Mailbox.

## 6. Behavior details and edge cases
- `getStateInformation` captures extras and A/B under `sessionLock_` in the same critical section as the rest of `saveState` (no torn state). `embedSource` fallback logic unchanged.
- `setStateInformation` order: parse wrapper (accept 1..2; anything else ignored as today), replace APVTS state, `loadState(..., {deferSourceResolve = true})` when the project has no embedded audio, else normal. If `sourceMissing()` and `source.path` is non-empty, bump `restoreEpoch_` and start `Thread::launch`: status `Restoring audio from <name>...`; `decodeFile(path)`; `MessageManager::callAsync`: if the epoch changed (another load, a user `loadFileAsync`, project replaced) or the session's `chopMap()->info().id` differs, drop the result; else `relinkSource(data)`; on `Conflict` call `reportSourceUnavailable(true)`, on decode failure `reportSourceUnavailable(false)`; on success status `Restored audio: <name>`. The session is already fully usable (markers, pattern, silent pass-through/missing-source state) before the worker finishes; `Locate Source` is enabled during the decode (clicking it bumps the epoch).
- Weak reference to the processor guards destruction (`juce::WeakReference`, same pattern as `loadFileAsync`); the destructor sets a stop flag and waits for the thread.
- Export worker: one at a time; captured data is immutable so the user may edit markers/patterns while it runs (the export reflects the project at click time). Plugin destruction sets cancel and calls `stopThread(5000)` on the worker; `renderOffline` polls cancel every 64 blocks (about 0.7 s of audio at 48 kHz / 512 frames), so the join normally takes well under a second. Writing to an existing file still requires the FileChooser overwrite confirmation (`o.overwrite`), and `run` re-checks existence immediately before the atomic write when `overwrite` is false.
- Seed text can exceed `seed_` input restriction on load (clamped to 999999999999 by `validate` fallback). `roleIndex` beyond the combo count falls back to 0.
- Undo, Evolve, locks, roles: extras and A/B are outside history; recalling a slot after restore uses the existing `recallSnapshot` (undoable, settles against current chops). `PatternSession::restore()` clears slots, so `restoreSlots` is called after it.
- Size: A/B adds at most 2 MiB; `codec::CodecLimits` unchanged (256 MiB total). `saveState` returns `LimitExceeded` as before if the total exceeds limits.
- Determinism: saving then loading then saving produces identical bytes for the same session (no timestamps, maps ordered).

## 7. Test plan
Unit (modules):
- `pattern_engine`: `pattern_session_slots_can_be_read_and_restored` (store A, `slot(0)` equals current by `serialize`; `restoreSlots` after `restore` makes `hasSnapshot(0)` true, `recallSnapshot(0)` returns the same bytes). Golden test file unchanged and passing.
- `audio_renderer`: `render_offline_reports_progress_and_can_be_cancelled` (callback sees non-decreasing fractions ending at 1.0; returning false at the 2nd call gives `ErrorCode::Cancelled`; output of an uncancelled call with and without a callback is byte-identical).
- `chop_contracts`/`state_codec`: `migration_registry_runs_identity_step_from_v1_to_v2`, `migration_registry_rejects_newer_schema_and_missing_step`.
Integration (`composition/tests/test_state_v2.cpp`, new, globbed by CMake):
- `default_session_state_bytes_are_unchanged_from_v1`: deterministic session (drum fixture, seed 42), `saveState(false)` bytes hash equals the constant captured from the pre-change build in step 1 (`kV1FixtureHash`) and the composition module payload schema is 1.
- `v1_state_loads_through_the_registered_migration`: embed a v1 fixture (hex array captured in step 1) -> loads, pattern/markers/history equal, `extras()` equals defaults.
- `extras_round_trip_and_force_schema_2`: set seed 987654321, motif "xa.x", depth 3, roleIndex 4, detectMode 3, grid 8/triplet, export 0/3/4, ui all non-default; save -> schema 2 payload; load into a fresh session -> `extras()==saved`; second save is byte-identical.
- `ab_snapshots_round_trip_and_are_dropped_individually_when_invalid`: store A and B, save/load, `hasSnapshot(0/1)`, `recallSnapshot(0)` bytes equal the stored; then corrupt slot B's bytes (patch length inside a copy) -> slot A restored, B dropped, one Warning containing `Snapshot B`.
- `malformed_extras_never_fail_the_load`: truncated section, duplicate tag, oversize length, unknown tag 200 (skipped, rest honoured), 5 MiB extras -> loads, extras default or partial as specified, one Warning, no crash (also run under ASan).
- `evolve_settings_manual_bpm_and_embed_flag_round_trip` (Evolve fields via session; `manualBpm`/`embedSource` in the plugin shell test below).
- `export_job_output_equals_the_synchronous_export`: `planWav`+`run` file bytes == `exportWav` bytes; kit same file set; `cancel` pre-set returns `Cancelled` and leaves no file; cancel set from a callback during a kit leaves no partial files.
- `deferred_source_resolve_keeps_a_usable_session_and_relink_restores_audio`.
Plugin shell tests (`plugin/tests/test_state_persistence.cpp`, helper `waitFor(pred, 5000ms)` pumping `runDispatchLoopUntil(20)`):
- `state_round_trip_restores_extras_ab_manual_bpm_and_embed_flag`.
- `reference_only_restore_decodes_on_a_worker`: write the drum loop to a temp WAV, load, save (embed off), new processor `setStateInformation`; immediately after the call `restoreInFlight()` is true and `playback()->passThrough` or `sourceMissing()` is true (the call returned without decoding); `waitFor(!restoreInFlight())`; session has audio and `statusMessage()` starts with `Restored audio`.
- `stale_restore_result_is_dropped`: start a restore, call `setStateInformation` with a different project, finish -> second project unaffected.
- `export_runs_on_a_worker_reports_progress_and_cancels`: `startExport` of a 16-loop WAV: `exportStatus().state==Running` immediately, fraction rises monotonically across `waitFor` polls, a second `startExport` returns Conflict, `cancelExport()` ends in `Cancelled`, the target file does not exist, a following export succeeds.
- `processor_destruction_while_exporting_does_not_hang` (start export, destroy processor, returns < 6 s, ASan clean).
GUI (xvfb, F00 helpers): `editor_restores_widgets_from_state`: set motif field text "xa.x" via `findChildWithID("FeaturePanel")` descendants, depth combo 3, press Generate; save state; new processor+editor with that state -> motif text `xa.x`, depth id 3, seed field text equals the saved seed.
Real-time / sanitizer: ASan+UBSan job runs all of the above; `grep` check that `processBlock` does not reference `extras`, `ExportJob` or `restoreEpoch_`; allocation-counter test in audio_renderer (existing) still passes (renderOffline change is offline only).
Manual QA: (1) Set seed 123, motif `xa.x`, Store A, mutate, Store B, save the DAW project, reopen: values and `Recall A/B` work. (2) Project with a 55 MB reference-only WAV: host stays responsive, status shows `Restoring audio...` then `Restored audio`. (3) Start a long Kit export, click `Cancel Export`: directory contains no new files. (4) Open a v1 project saved by the previous release: loads, no warnings.

## 8. Acceptance criteria
- [ ] `default_session_state_bytes_are_unchanged_from_v1` passes with a constant captured before any production change.
- [ ] A v1 project (fixture) loads; an extras-bearing project written by this build loads back equal; a malformed extras section loads with defaults and one warning.
- [ ] `MigrationRegistry` has exactly one registered step (`composition` 1 to 2) and `state_schema_versions_have_migration_paths` asserts that for each module id in {composition, source_chop, pattern_engine, chop_roles_grammar, variation_history, source_audio} a payload at schema 1 migrates to that module's current constant (so bumping a constant without registering a step fails the test).
- [ ] pattern_engine golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 unchanged; `pattern::kSchemaVersion` unchanged.
- [ ] `setStateInformation` performs no file I/O or decoding on the calling thread (test hook counts `decodeFile` calls on the caller thread id: 0).
- [ ] Export of a 16-loop WAV never blocks the message thread for more than 50 ms in the shell test (measured around `startExport`).
- [ ] After `cancelExport()` the worker stops within 1 s and no partial file remains.
- [ ] Audio thread unchanged: no new references in `processBlock`; allocation test green; TSan job on `audio_renderer` green.
- [ ] Old-build rejection documented in `docs/BUILD_STATUS.md`.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| composition/include/chopfractal/composition/project_session.hpp | Mod, **HOT** | extras types, `setExtras`, `LoadOptions`, `reportSourceUnavailable`, schema constants |
| composition/src/project_session.cpp | Mod, **HOT** | migration registration, save/load of extras and A/B, deferred resolve |
| composition/src/features.cpp | Mod | `exportWav/exportKit` delegate to `ExportJob` |
| composition/include/.../export_job.hpp, composition/src/export_job.cpp | New | job planning/running (CMake globs `src/*.cpp`) |
| composition/tests/test_state_v2.cpp | New | section 7 |
| modules/pattern_engine/include/.../session.hpp, src/serialize.cpp, tests/test_pattern_engine.cpp | Mod | slot accessors (no serialize change) |
| modules/chop_contracts/include/.../result.hpp | Mod | `ErrorCode::Cancelled` appended |
| modules/audio_renderer/include/.../renderer.hpp, src/renderer.cpp, tests/test_renderer.cpp | Mod | progress callback |
| plugin/src/PluginProcessor.h/.cpp | Mod, **HOT** | wrapper v1..2 lazy write, worker restore + epoch, `startExport/cancelExport/exportStatus`, thread join in destructor |
| plugin/src/EditorModel.*, EditorCommands.*, ControlsPanel.*, FeaturePanel.*, StatusBar.* | Mod | extras wiring, Cancel Export button, disabled export buttons |
| plugin/tests/test_state_persistence.cpp | New | shell tests |
| plugin/tests/test_plugin_shell.cpp | Mod, **HOT** | existing state tests call `waitFor(!restoreInFlight())` after `setStateInformation` |
| docs/BUILD_STATUS.md | Mod | remove the three fixed gaps; document v2 |

## 10. Risks and mitigations
- Existing shell tests assume synchronous restore: add `waitFor` in the same commit that makes restore async; the failure mode is a clear assertion, not a hang (5 s cap).
- Lazy versioning confuses readers who expect "always v2": document in `project_session.hpp` next to the constants and test both paths; fallback is to always write v2 (golden-bytes test is then replaced by a v1-fixture-load test).
- Concurrent edit during async restore: epoch + source-id check; worst case the decoded buffer is dropped and `Locate Source` remains available.
- `ErrorCode::Cancelled` is a new value beyond the module-private Error enums that `convert<E>` casts from: those modules never produce it, so no mapping is needed; a test asserts `convert` still maps every existing private value to the same shared value.
- Joining a thread in the plugin destructor can stall a host: cancel flag is polled every ~0.7 s of rendered audio; 5 s hard cap.

## 11. Implementation steps
1. Capture fixtures from the unmodified build (`kV1FixtureHash`, v1 hex fixture) and commit them with the failing-by-design tests disabled behind the fixture only.
2. `PatternSession` slot accessors + test; `ErrorCode::Cancelled`; `renderOffline` progress + tests.
3. `ProjectExtras`, `setExtras`, wire format, lazy versioning, migration registration, composition tests.
4. `ExportJob` + delegation + tests; `LoadOptions`/`reportSourceUnavailable`.
5. Processor: wrapper lazy version, worker restore, `startExport`, destructor join; shell tests; update existing shell tests with `waitFor`.
6. Editor wiring (needs F00): model `setExtras`, widgets sync on `Change::Extras`, Cancel Export button, `doGenerate` reads `gridDivision/gridTriplet` (defaults leave Generate output identical), GUI test.
7. Docs: BUILD_STATUS gaps, v2 note.
