# F12 Input capture

**Status:** Ready for development  **Size:** L (8 engineer-days)  **Depends on:** F00 (WavePanel, StatusBar), F01 only for the shared UI-pref trailing block (optional)  **Blocks:** F14 (capture + stretch workflow is a documented combination, not a code dependency)

## 1. Summary and user value
Record the track's audio input straight into ChopFractal and chop it, instead of exporting a loop to disk and importing it. A four-state flow (Arm / Capture / Stop / Keep) records either free-running or synced to the host's bar grid, using a preallocated buffer so the audio thread never allocates. This closes the last gap in BUILD_STATUS stage 3 ("Not done: input capture") and removes the file-only workflow for the main use case (sample a loop from another track or instrument).

## 2. User stories and scope
- As a producer I can arm capture, press play in my DAW, and have exactly 4 bars recorded from the next bar line so that the loop is already on the grid.
- As a producer I can press Capture and Stop at any time (Free mode) and keep the last N seconds so that I do not need to time my click.
- As a producer I can Keep the capture, which replaces the source, auto-detects chops and suggests roles, and I can Undo the Keep.
- As a producer I am told when the input clipped, when the capture was cut short, and how big it is.
- **In scope:** portable `input_capture` module (state machine, ring/linear buffer, bar sync), processor wiring, WavePanel capture strip, Keep path into `ProjectSession::loadSource`, forced embedding of captured audio, undo of Keep, limits and messages.
- **Non-goals:** recording the plugin's own output, retro-capture of audio played before Arm, multi-take management, capturing more than stereo, resampling on capture (the source keeps the host rate), crash-recovery files, saving the capture as a WAV (use the existing Producer Kit/WAV exports after chopping), persisting an unkept capture in project state.

## 3. UX
Location: WavePanel header strip. A **"Capture"** toggle button (next to "Load") expands a capture strip above the waveform; collapsed by default. When no source is loaded WavePanel's empty text becomes `"Drop an audio file, press Load, or press Capture to record the track input."`.

Controls (all with `setTitle` for accessibility):
- ComboBox **"Capture mode"**: `"Synced to host (bars)"` (default), `"Free (until Stop)"`.
- ComboBox **"Capture bars"** (Synced only; otherwise hidden): `"1"`, `"2"`, `"4"` (default), `"8"`. Label `"4 bars = 8.0 s at 120.0 BPM"` recomputed from `activeBpm` and the host meter on the 30 Hz timer.
- ComboBox **"Max length"** (Free only): `"10 s"`, `"30 s"` (default), `"60 s"`. Free mode is a ring: only the last Max length is kept.
- Buttons: **"Arm"**, **"Capture"**, **"Stop"**, **"Keep"**, **"Discard"**; checkbox **"Chop after Keep"** (default on).
- State label and timer: `"Idle"`, `"Armed: waiting for the host to play"` (Synced, transport stopped) / `"Armed: starts at the next bar"` (Synced, playing) / `"Armed: press Capture to start"` (Free), `"Capturing 00:03.2 / 00:08.0"` (Synced) or `"Capturing 00:03.2 (keeping the last 30 s)"` (Free), `"Captured 8.0 s, stereo, 48 kHz. Keep to chop it, or Discard."`.
- Input meter: 120x8 px bar fed by `CaptureEngine::status().peak`; turns red and shows `"Clipped"` after any sample with |x| >= 0.999 (count in tooltip `"N samples clipped"`).

Enable rules: Arm enabled in Idle/Captured; Capture enabled in Armed; Stop enabled in Armed/Capturing; Keep/Discard enabled only in Captured. Mode/bars/max-length combos disabled while Armed/Capturing.
Interactions: Arm in Captured with an unkept capture opens a `juce::AlertWindow` `"Discard the unsaved capture?"` buttons `"Discard"` / `"Cancel"`. Keep with an existing source that has a pattern opens `"Replace the current source?"` message `"Keeping this capture replaces the source. Your pattern and variations will be cleared. You can undo this with Undo Keep."` buttons `"Keep"` / `"Cancel"`. After Keep a button **"Undo Keep"** appears in the strip until the next Generate/Mutate/Load/Capture (hidden otherwise).
Messages (StatusBar via `setStatusMessage`, also `Notice`): 
- `"Capture needs an audio input. Your host gave this plugin no input channels."`
- `"Capture is not available during an offline render."`
- `"That length is 96.0 s; the maximum is 60 s."` / `"That capture would need 120 MB; the limit is 100 MB. Choose a shorter length."`
- `"Capture stopped: the host transport stopped or jumped. Kept 3.2 s."`
- `"Capture ended early: the tempo changed during capture."` (Warning)
- `"Kept the last 30 s of the recording."` (Info)
- `"Captured audio is saved inside the project."` after Keep.
- `"Capture cancelled: the audio settings changed."` when `prepareToPlay` changes rate/channels mid-capture.
Empty/loading: Keep shows `"Keeping capture..."` until the worker finishes. Keyboard: Tab order Mode, Bars, Arm, Capture, Stop, Keep, Discard; no global shortcuts in this feature (F04 may add).

## 4. Data model and state
New portable module `modules/input_capture` (`chopfractal::capture`), depends only on `chop_contracts` (Result/Status, limits). No JUCE, no renderer include.
```cpp
enum class CaptureState : std::uint8_t { Idle = 0, Armed = 1, Capturing = 2, Captured = 3 };
enum class CaptureMode : std::uint8_t { SyncedBars, Free };
enum class EndReason : std::uint8_t { None, Completed, UserStop, TransportStopped, Overflow, Reconfigured };
struct ArmSettings {
  CaptureMode mode = CaptureMode::SyncedBars;
  int channels = 2;                 // 1 or 2, the main input bus
  double sampleRate = 48000.0;      // 8000..384000
  int bars = 4;                     // SyncedBars: 1..8
  double barQuarters = 4.0;         // quarter notes per bar (numerator * 4 / denominator)
  double bpmHint = 120.0;           // sizes the buffer; the real length follows ppq
  double maxSeconds = 30.0;         // Free: ring length, 1..60
};
struct CaptureTransport { bool playing = false; bool positionValid = false; double ppq = 0.0; double bpm = 120.0; };
struct CaptureStatus { CaptureState state; EndReason ended; std::int64_t framesCaptured; std::int64_t capacityFrames; float peak; std::uint32_t clippedSamples; };
struct CapturedAudio { int channels; int sampleRate; std::int64_t frames; std::vector<float> samples; /* planar, same layout as render::SourceData */ EndReason ended; bool wrapped; };
constexpr double kMaxCaptureSeconds = 60.0;                       // == plugin kMaxSourceSeconds
constexpr std::int64_t kMaxCaptureBytes = 100ll * 1024 * 1024;    // == plugin kMaxDecodedBytes
constexpr std::int64_t kMaxCaptureFrames = 60ll * 192000;         // == source::kMaxSourceFrames
```
Plugin: move `kMaxSourceSeconds` (60.0) and `kMaxDecodedBytes` (100 MiB) from the anonymous namespace of `plugin/src/PluginProcessor.cpp` to `static constexpr` members of `ChopFractalProcessor` (declared in `PluginProcessor.h`) so `decodeFile` and capture share them; a `static_assert` in the plugin ties them to the module constants.
Capacity rule: `maxFrames = min(floor(60*sr), kMaxCaptureFrames, kMaxCaptureBytes / (channels*4))`. At 48 kHz stereo that is 2,880,000 frames (23.04 MB); at 192 kHz stereo 11,520,000 frames (92.16 MB); at 384 kHz stereo 11,520,000 (cap by frames). Synced capacity = `min(maxFrames, ceil(bars*barQuarters*60/bpmHint*sr*1.25) + 1)` (25% headroom for tempo drops). Free capacity = `min(maxFrames, ceil(maxSeconds*sr))`.
Persistence: **no new state.** A captured and kept source is an ordinary `SourceData` with `SourceInfo::name = "Capture YYYY-MM-DD HH:MM"` and `path = ""`. Because a pathless source cannot be re-decoded, `ProjectSession` gains `bool sourceNeedsEmbed() const` (`hasSource() && chopMap()->info().path.empty()`), and `getStateInformation` uses `embed = embedSource.load() || session_.sourceNeedsEmbed()`. The existing `saveState`, schema versions (`kSessionSchemaVersion`, `source::kSchemaVersion`, `kModAudio` v1) and the state v2 of F01 are unchanged; old projects load as before. Size: embedding is bounded by the existing `embeddedCap_` (128 MiB) and the capture bound (<= 100 MiB), so a capture always fits the default cap; if the user lowered the cap, `saveState(true)` fails and `getStateInformation` shows `"The captured audio is too large to embed (N MB). Raise the embed limit or keep a shorter capture."` and saves the project without audio rather than silently dropping it (existing fallback path, message differs for pathless sources). An unkept capture is not saved (documented in the strip tooltip: `"An unkept capture is lost when the project closes."`).
Host parameters: none added; manifest version unchanged.

## 5. Public API
Portable `modules/input_capture/include/chopfractal/input_capture/capture.hpp`:
```cpp
class CaptureEngine {
 public:
  CaptureEngine(); ~CaptureEngine();
  // ---- message thread (allocate here, never on the audio thread) ----
  Status arm(const ArmSettings& s);          // Idle/Captured(after take or discard) -> Armed; allocates the buffer; LimitExceeded/OutOfRange/Conflict (not Idle)
  Status startNow();                         // Armed -> Capturing at the next block (Free mode and manual Synced start)
  void requestStop();                        // Armed -> Idle (buffer released by reap()), Capturing -> Captured at the next block start
  void discard();                            // Captured -> Idle (buffer released by reap())
  Result<CapturedAudio> take();              // Captured only: unwrap the ring, compact, move out, -> Idle. O(frames) memmove, no allocation beyond the move
  CaptureStatus status() const;              // lock-free atomics, 30 Hz polling
  void reap();                               // frees buffers whose state is Idle; called from the 30 Hz timer
  // ---- prepareToPlay (audio stopped) ----
  void onPrepare(double sampleRate, int channels);   // aborts Armed/Capturing with EndReason::Reconfigured (keeps partial data as Captured)
  // ---- audio thread: no allocation, no locks, no logging ----
  void process(const float* const* in, int numChannels, int frames, const CaptureTransport& t);
};
```
Composition (`project_session.hpp`):
```cpp
bool sourceNeedsEmbed() const;
Status ProjectSession::loadCapturedSource(std::shared_ptr<const render::SourceData> data, std::string name); // = loadSource(data, name, "")
```
(`loadCapturedSource` is a named wrapper so call sites read clearly; no new logic.) Plugin (`PluginProcessor.h`): `capture::CaptureEngine& captureEngine()`, `void keepCaptureAsync(bool chopAfter)`, `bool undoKeep()`, `bool canUndoKeep() const`, `std::atomic<bool> captureBlocked_` (offline render).
Threading: `process` audio thread only; all other methods message thread (or the Keep worker for `take()` only while state is Captured, which the audio thread never touches).

## 6. Behavior details and edge cases
**Memory handoff and safety.** The buffer is a `std::unique_ptr<float[]>` owned by the engine, planar with stride `capacityFrames`. `arm` allocates it, writes the plain fields, then `state.store(Armed, release)`. The audio thread `process()` does `acquire` on `state`; it touches the buffer only in Armed/Capturing. The message thread never frees or reads the buffer unless `state` is Idle or Captured (it observes this with acquire). `requestStop`/`discard` only set an atomic request; the audio thread acts on it at the next block start (Capturing -> Captured, release) so a block is never cut mid-write. Armed -> Idle on stop is performed by the audio thread too; `reap()` frees only when state == Idle. If no audio callback runs (host stopped processing), `reap()` cannot free an Armed buffer; `onPrepare`/destructor (audio stopped by contract) force Idle.
**Sync algorithm (SyncedBars).** In Armed with `t.playing && t.positionValid`: `p0 = t.ppq`, `p1 = p0 + frames*bpm/(60*sr)`, `Q = barQuarters`. Boundary `b = ceil(p0/Q - 1e-9)*Q`; if `b < p1` start at frame offset `o = ceil((b - p0)*60/bpm*sr - 1e-6)` (same rounding as `Renderer::schedule`), clamped to `[0, frames-1]`; `endPpq = b + bars*Q`. While Capturing each block: if `p1 >= endPpq` the end offset is `ceil((endPpq - p0)*60/bpm*sr - 1e-6)`; copy up to it, state -> Captured, `ended = Completed`. Ending by ppq (not by a precomputed frame count) keeps tempo changes bar-exact; a 4-bar capture at 120 BPM / 48 kHz / 4/4 yields 384,000 frames +/- 1. If capacity is reached first: stop, `ended = Overflow` (UI: tempo-changed warning). If `!t.playing`, or `t.ppq` is outside `[expected-2 blocks, expected+2 blocks]` (seek/loop wrap) while Capturing: stop, `ended = TransportStopped`, keep partial. If the host never supplies `positionValid`, Synced stays Armed; the label tells the user to use Free mode.
**Free mode (ring).** `startNow()` -> Capturing at the next block start. Writes advance `writePos = (writePos + n) % capacity`; `total += n`; `wrapped = total > capacity`. On stop `take()` returns the last `min(total, capacity)` frames in time order using `std::rotate` per channel on the planar array (in place), then compacts channels to stride `frames` (ascending `memmove`), shrinks logical size, and moves the vector out. Auto-stop never happens in Free mode (the ring overwrites oldest); EndReason::UserStop.
**Input handling.** `process` is called from `ChopFractalProcessor::processBlock` after building `HostTimeInfo` and **before** `renderer_.process`, because the renderer's `out` aliases `in` and overwrites it. Channels: copies `min(numChannels, armedChannels)` channels; armed with 2 and receiving 1 duplicates the mono input to both; armed with 1 and receiving 2 records channel 0 only (never mixes). Non-finite samples are stored as 0.f. Peak and clipped count are updated per block with relaxed atomics (peak via compare-exchange max). Sample rate: recorded at the host rate; `SourceData::sampleRate = lround(sr)`; `makePlayback` accepts 8000..384000. If the host sample rate later changes the renderer's existing `src.sampleRate / cfg.sampleRate` ratio handles playback; markers stay integer frames in the source.
**Offline renders.** `processBlock` skips `process` when `isNonRealtime()`; `arm` is refused while `captureBlocked_`. **Bypass:** when `RenderParams::enabled == false` capture still records (it reads input, not output).
**Keep.** `keepCaptureAsync`: (1) message thread: if `canUndoKeep` rules need it, snapshot `preKeepState_ = session.saveState(true)` (if it fails because of size, skip and the confirm text omits "You can undo this with Undo Keep."), keep `preKeepSource_` (shared_ptr to current `SourceData`); (2) `juce::Thread::launch` worker calls `engine.take()` (the only O(n) work; ~25 ms for 23 MB) and moves `samples` into a `SourceData` (`channels`, `sampleRate`, `frames`, planar; no extra copy); (3) `juce::MessageManager::callAsync` -> `withSession(loadCapturedSource(data, name))`, set `embedSource = true`, status `"Captured audio is saved inside the project."`; (4) if `Chop after Keep`: `s.smartSetup(usedFallbackTempo ? 0.0 : activeBpm)` (detect transients with `MergeMode::Merge`, then `suggestSetup`), identical to the Smart Setup button; role suggestions are shown, not auto-applied. Failure at any step leaves the previous project untouched (loadSource builds the new map before mutating). **Undo Keep:** `loadState(preKeepState_, resolver)` where the resolver returns `preKeepSource_` when the blob was not embedded; clears both buffers afterwards. It is the only undo (a capture is not a history node; `loadSource` already resets patterns and history by design).
**Locks / history / Evolve:** Keep stops Evolve (`loadSource` calls `evolve_.stop()`); pattern locks and variations are lost on replace, which is why the confirm exists. Capture itself never changes the session while Armed/Capturing, so Generate/Mutate/audition keep working during a capture.
**Determinism:** capture is input-dependent; given identical input blocks and transport, output frames are identical regardless of block size (tested). No effect on pattern golden hashes.
**Limits summary:** 60 s, 100 MB (`kMaxCaptureBytes`), 192000*60 frames; 1 or 2 channels; Arm refuses with the section 3 messages. Memory is allocated per Arm and freed on Keep/Discard/stop-from-Armed, so an idle plugin holds 0 bytes of capture memory.

## 7. Test plan
Unit, `modules/input_capture/tests/test_input_capture.cpp` (uses `modules/chop_contracts/tests/chop_test.hpp` style):
- `synced_capture_starts_on_bar_line_with_sample_accuracy`: sr 48000, 120 BPM, 4/4, input `x[n] = n` encoded in float via a counter, blocks of 512 starting at ppq 0.3; assert first captured value == absolute index `ceil((4.0-0.3)*0.5*48000 - 1e-6)` = 88800 and `frames == 384000 +/- 1` for 4 bars.
- `capture_is_block_size_independent`: block sizes 1, 7, 64, 333, 2048 produce byte-identical `samples`.
- `free_ring_keeps_the_last_capacity_frames_in_order`: capacity 1000, write ramp 0..2499, `take()` -> frames 1000, `samples[i] == 1500 + i`, `wrapped == true`.
- `mono_input_duplicated_to_stereo_and_stereo_to_mono_uses_channel_0`.
- `transport_stop_or_seek_ends_capture_with_partial_audio` (EndReason::TransportStopped, frames == written before stop).
- `tempo_drop_beyond_headroom_ends_with_overflow`.
- `arm_rejects_oversize_requests`: 8 bars at 20 BPM -> OutOfRange (96 s); 384 kHz stereo 60 s clamps to 11,520,000 frames; capacity*ch*4 > 100 MiB -> LimitExceeded with `hint` = max frames; arm while Capturing -> Conflict.
- `state_machine_transitions_table`: every (state, call) pair asserted (Idle+startNow = Conflict, Captured+take ok, Idle+take = NotFound).
- `non_finite_input_stored_as_zero_and_clip_counted`.
- `process_never_allocates`: global `operator new` hook as in the renderer test; Armed, Capturing, wrapped and idle blocks.
- `threaded_status_polling` (also in the TSan CI step: add the new test target next to `test_audio_renderer`): audio thread loops `process`, second thread calls `status()`, `requestStop()`, `take()` after Captured.
Integration (`composition/tests`): `captured_source_loads_chops_and_roundtrips_embedded` (build SourceData from a synthetic 4-beat click loop, `loadCapturedSource`, `smartSetup(120)` yields >= 4 markers, `saveState(true)` then `loadState` restores audio with a resolver returning nullptr; `sourceNeedsEmbed()` true); `pathless_source_without_embed_reports_missing` (documents why the processor forces embed).
Shell/GUI (`plugin/tests/test_plugin_shell.cpp`): `capture_flow_through_the_real_processor`: set mode Free via the ComboBox, click "Arm" -> `captureEngine().status().state == Armed`; process 20 blocks x 512 of a 1 kHz sine via `processBlock`; click "Capture", process 100 blocks, click "Stop", process one block -> Captured with `framesCaptured == 51200`; click "Keep" and pump the message loop until `statusMessage()` contains `"Captured audio is saved"`; assert `session.hasSource()`, name starts `"Capture "`, `chopMap()->info().frames == 51200`, `embedSource == true`; save/restore with `getStateInformation`/`setStateInformation` into a second processor and assert `!sourceMissing()`. `capture_undo_keep_restores_previous_source`. `capture_is_refused_with_no_input_channels` (processor configured 0 inputs). `input_passes_through_unchanged_before_a_source_is_loaded` must still pass (capture reads before the renderer and never writes input).
Real-time: allocation test above; run under ASan/UBSan and TSan CI jobs.
Manual QA: 1) REAPER, plugin on a track fed by a drum loop item, 4/4 at 120 BPM. 2) Capture > mode Synced, bars 4, press Arm: label says waiting. 3) Press play at bar 1.3: label changes to "starts at the next bar", capture begins at bar 2 and shows 00:00.0 / 00:08.0. 4) After 8.0 s: state becomes Captured; label "Captured 8.0 s, stereo, 48 kHz." 5) Keep: waveform appears, markers detected, status "Captured audio is saved inside the project." 6) Save, close, reopen project: audio present without any file. 7) Undo Keep before step 5's save returns to the previous empty/old source. 8) Mono track: capture is "mono" and plays back centered. 9) Free mode with Max length 10 s: record 25 s, Stop: info "Kept the last 10 s of the recording." 10) Change the audio device sample rate mid-capture: "Capture cancelled: the audio settings changed."

## 8. Acceptance criteria
- [ ] Synced 4-bar capture at 120 BPM/48 kHz is 384,000 frames +/- 1 and starts within 1 frame of the bar line (test above).
- [ ] No allocation, lock, or I/O in `CaptureEngine::process` (instrumented test passes; TSan clean).
- [ ] Output of `processBlock` is bit-identical with the engine Idle vs. the pre-F12 build (capture adds no work when Idle beyond one relaxed atomic load).
- [ ] Capture memory is 0 B when Idle and never exceeds 100 MiB; Arm beyond the limits is refused with the exact messages.
- [ ] Keep of a 60 s stereo 48 kHz capture completes the audio-thread-free copy in <= 250 ms on the CI runner and the audio thread never blocks (no xruns in the REAPER run, measured by REAPER's performance meter staying within 10% of baseline).
- [ ] A kept capture survives save/reload without any file on disk; project saved with embed off still contains the audio.
- [ ] Undo Keep restores the previous source, markers and pattern.
- [ ] Existing golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 unchanged; existing state files load unchanged; no new host parameters.
- [ ] `tools/check_modules.py` and `tools/smoke_transfer.sh` pass with the new module (module.json, README, MIGRATION.md, CMake deps exactly `chop_contracts`).

## 9. Files touched
| path | new/modified | change |
|---|---|---|
| modules/input_capture/{CMakeLists.txt,module.json,README.md,MIGRATION.md,include/chopfractal/input_capture/capture.hpp,src/capture.cpp,tests/*} | new | portable engine; README/docs generated by tools/gen_module_docs.py |
| CMakeLists.txt (root) | **HOT** modified | add `input_capture` to the module foreach list before `audio_renderer`'s dependents |
| composition/CMakeLists.txt, composition/include/.../project_session.hpp, composition/src/project_session.cpp | **HOT** modified | `sourceNeedsEmbed`, `loadCapturedSource` |
| plugin/src/PluginProcessor.cpp/.h | **HOT** modified | engine member, `processBlock` hook, `onPrepare`, Keep/undo, constants moved, forced embed, `reap()` in `pollAudioFlags` |
| plugin/src/WavePanel.cpp/.h (from F00; else PluginEditor.cpp **HOT**) | modified | capture strip |
| plugin/CMakeLists.txt | **HOT** modified | link `chopfractal::input_capture` |
| plugin/tests/test_plugin_shell.cpp | modified | new tests |
| .github/workflows/ci.yml | **HOT** modified | add input_capture test to the TSan step |
| docs/BUILD_STATUS.md, docs/specs/source-and-chop-editor.md | modified | remove "input capture not implemented", describe the flow |

## 10. Risks and mitigations
- Host variance in `getPlayHead()` position (some hosts omit ppq at stop): Synced simply waits; Free mode always works; both covered in manual QA.
- Input buffer aliasing with output: capture must run before `renderer_.process`; guarded by a unit test in the shell that feeds a distinct input and asserts the captured frames equal the input, not the rendered output.
- 92 MB allocation latency/OOM in 32-bit-address hosts: allocate on Arm, catch `std::bad_alloc` and return `LimitExceeded` ("Not enough memory for that capture length."); not an audio-thread concern.
- Loss of an unkept capture on editor/project close: tooltip warns; capture stays in the processor while the plugin instance lives (closing the editor does not discard).
- Silent loss of pathless audio on save: forced embed + size message; tested.

## 11. Implementation steps
1. Scaffold `modules/input_capture` (module.json, CMake, README via gen_module_docs, empty engine), add to root CMake and `check_modules`; CI green.
2. Engine: state machine, Free ring, `take()`, status; unit tests incl. allocation and threaded tests; add to TSan CI step.
3. Engine: bar sync, end-by-ppq, transport/seek handling, tempo-drop overflow; sync tests.
4. Composition: `sourceNeedsEmbed`, `loadCapturedSource`, tests.
5. Processor: constants move, `processBlock` hook before renderer, `onPrepare`, offline guard, forced embed + messages; shell tests (headless).
6. Keep worker, Undo Keep, Smart Setup chaining; shell tests.
7. WavePanel capture strip, enable rules, dialogs, meters; GUI test under xvfb.
8. Docs update, REAPER manual QA, record results in BUILD_STATUS.md.
