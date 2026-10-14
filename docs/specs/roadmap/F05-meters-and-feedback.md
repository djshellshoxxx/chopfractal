# F05 Meters and feedback

**Status:** Ready for development  **Size:** M (6 engineer-days)  **Depends on:** F00 (StatusBar, HistoryPanel, WavePanel, EditorCommands), F04 for final StatusBar widths (can land before it; see section 3)  **Blocks:** F07 (voice display for pads), F11 (per-bus meters reuse `OutputMeter`)

## 1. Summary and user value
The plugin gives almost no feedback: no output level or clip warning, no view of active voices, tempo/meter fallbacks are buried in the status text, a queued variation switch is invisible, only the last notice of each action is shown, and loops can only be loaded through a file chooser. F05 adds a lock-free output peak meter with a latched clip indicator, an active-voice display, a tempo/meter/sync box, a pending-activation indicator on the family tree and status bar, a severity-tagged notice log, and drag-and-drop of WAV/AIFF onto the editor. Producers see what the plugin is doing in the DAW and trust it more.

## 2. User stories and scope
- As a producer I see the output peak and a clip warning that stays lit until I clear it, so I can lower the gain before a render.
- As a producer I see how many voices are sounding and whether the plugin follows the host tempo or my manual fallback.
- As a producer who clicked a variation during playback I see that it is queued and when it starts.
- As a producer I can review every warning the plugin raised during the session, not only the last one.
- As a producer I can drag a WAV onto the plugin to load it.

**In scope:** `OutputMeter` in `audio_renderer` (peak, clip count, voices) written by the audio thread; UI ballistics; StatusBar widgets; `NoticeLog` with severity and a log popup; processor tempo/meter atomics; `ProjectSession::pendingActivationNode()`; tree ring + status messages; file drag-and-drop with replace confirmation.
**Non-goals:** true-peak/LUFS/RMS metering, per-chop or per-bus meters (F11), a spectrum/scope, persisting the log, sound alerts, clip protection (limiter), host-side meter exposure, meter for the input.

## 3. UX
**StatusBar (920 x 24 at default; F00 panel) right-aligned cluster**, right to left with 4 px gaps: [F04 `View mode` 84][F04 `Interface scale` 72] (absent until F04 lands) then F01's `Cancel Export` (110, only while exporting), then F05: `MeterStrip` (128 x 20), `VoiceBox` (52 x 20), `SyncBox` (170 x 20; 96 x 20 compact when the bar is narrower than 900 px). Status text fills the remaining left area, elided with `...`; clicking it (or pressing Enter when StatusBar has focus) opens the notice log.
- **MeterStrip:** two horizontal bars (L at y=3, R at y=12, each 90 x 7), scale -60 dB to +6 dB mapped linearly to the width, a 1 px tick at 0 dB. Fill colours by level: <= -12 dB `0xff7cf29a`, -12..-3 dB `0xffffc857`, > -3 dB `0xffff6b6b`; a 2 px peak-hold line stays 1.5 s, then falls with the bar. Right of the bars (4 px gap) a 30 x 14 chip labelled `CLIP`: unlit = dim outline `0xff9aa4b2`; latched = filled `0xffff6b6b` with white text. Clicking the chip clears the latch. Tooltip: `Output peak: L -12.3 dB, R -11.8 dB. CLIP lights when the output exceeds 0 dBFS; click it to reset.`
- **VoiceBox:** text `V 3/8` (sounding or fading voices / `limits::kMaxVoices`), tooltip `Active voices: 3 of 8 (sounding or fading).`
- **SyncBox** text (exact): `<bpm 1 decimal> BPM[ (manual)]  <num>/<den>[ (assumed)]  <Playing|Stopped>`; e.g. `120.0 BPM  4/4  Playing` (host tempo and meter), `98.0 BPM (manual)  4/4 (assumed)  Stopped` (both fallbacks). Compact form: `120.0 | 4/4 | Playing` (suffixes dropped). Tooltips keep the old status wording: `No host tempo: using 98.0 BPM` and `Assuming 4/4`, or `Following the host tempo and time signature.` The old status line no longer appends those two hints; `EditorModel::statusLine()` returns the message only.
- **Notice severity in the status text:** Info has no prefix, Warning `[!] `, Error `[x] `, so severity is not colour only; colours: info white 0.8 alpha, warning `0xffffc857`, error `0xffff6b6b`.
- **Notice log popup** (`juce::CallOutBox` 460 x 240 anchored to the status text): newest first, rows 18 px `HH:MM:SS  [!] text`, a `Clear` button (title `Clear notices`) and a `Copy` button (title `Copy notices`; copies all rows as text to the clipboard). Empty state `No notices yet.` Capacity 100 entries; older ones are dropped.
- **Pending activation:** HistoryPanel draws a dashed 2 px ring (radius 11, `0xffffc857`, 4 px dash / 3 px gap, static, no animation) around the queued node. Status messages (exact): when a switch is queued `Variation "<label>" will start at the next loop.` (use `#<id>` when the label is empty); when it takes effect `Switched to "<label>".` If not playing the switch is immediate and only the second message appears.
- **Drag and drop:** while a droppable file hovers over the editor, WavePanel draws a 2 px `0xff7cf29a` border and the centered text `Drop a WAV or AIFF to load it` (replaces the empty-state text if shown). Accepted extensions (case-insensitive): `.wav`, `.wave`, `.aif`, `.aiff`. Dropping several files loads the first accepted one and logs (Info) `Only the first file was loaded.` When the project already has a pattern or history, a confirmation precedes loading, for drops and for `Load Loop`: title `Replace the current loop?`, message `Loading a new loop resets the markers, pattern and family tree of this project.`, buttons `Replace` and `Cancel`. (This is a deliberate behavior change for `Load Loop`, which replaces silently today; spec `gui-and-interaction.md` requires the confirmation.)
Idle rule: no widget repaints while nothing changes (silent output, no hover, no new notice).

## 4. Data model and state
No persisted state, no host parameters, no schema change (the log and meter are transient). New transient types:
```cpp
// modules/audio_renderer/include/chopfractal/audio_renderer/meter.hpp  (portable, header only)
struct MeterReading { float peak[2] = {0.f, 0.f}; std::uint32_t clipSamples = 0; int activeVoices = 0; };
class OutputMeter {                       // one audio-thread writer (push), one UI-thread reader (take)
 public:
  void push(const float* const* out, int channels, int frames, int activeVoices) noexcept;   // O(frames), no alloc/lock
  MeterReading take() noexcept;           // returns the max since the last take and resets peak and clip count; voices = last pushed
 private:
  std::atomic<float> peak_[2]{0.f, 0.f}; std::atomic<std::uint32_t> clip_{0}; std::atomic<int> voices_{0};
};
static_assert(std::atomic<float>::is_always_lock_free);
```
Rules: peak is `max(|x|)` over the block per channel (mono: channel 1 = channel 0); a sample clips when `std::fabs(x) > 1.0f` (NaN never clips; NaN is ignored by the max); `push` does `if (blockPeak > peak_.load(relaxed)) peak_.store(blockPeak, relaxed)`, `clip_.fetch_add(n, relaxed)`, `voices_.store(...)`; `take` uses `exchange(0)`. A peak or clip count lost to the load/exchange race is reported at most once late.
```cpp
// plugin/src/NoticeLog.h (message thread only)
enum class Severity : std::uint8_t { Info, Warning, Error };
struct LogEntry { juce::Time time; Severity severity = Severity::Info; juce::String text; };
class NoticeLog { public: void add(Severity, const juce::String&); const std::deque<LogEntry>& entries() const; void clear();
                  std::uint64_t revision() const; const LogEntry* latest() const; static constexpr std::size_t kCapacity = 100; };
```
`Notice::Level` maps Info/Warning/Error to the same names.

## 5. Public API
`modules/audio_renderer` (**HOT**, shared with F01/F07/F08/F11): `OutputMeter& Renderer::meter();` (const-correct reference to a member in `Impl`; `Renderer::process` calls `meter.push(out, numChannels, frames, activeVoices())` as its last statement, including the bypass path). `activeVoices()` stays but is documented as non-RT-safe for UI use; the UI uses `MeterReading::activeVoices`.
`modules/plugin_ui_adapter` (portable, new `status_text.hpp`):
```cpp
std::string describeSync(double bpm, int numerator, int denominator, bool fallbackTempo, bool fallbackMeter, bool playing, bool compact);
std::string severityPrefix(int level);           // 0 -> "", 1 -> "[!] ", 2 -> "[x] "
float peakToDb(float linear);                    // 20*log10, <= 0 or NaN -> -60.f, clamped to [-60, +6]
float dbToFraction(float db);                    // linear map -60..+6 -> 0..1, clamped
struct MeterState { float db[2] = {-60.f, -60.f}; float holdDb[2] = {-60.f, -60.f}; double holdAge[2] = {0, 0}; bool clipLatched = false; };
bool updateMeter(MeterState&, const MeterReading&, double dtSeconds);   // returns true if anything visible changed
```
`updateMeter`: attack instant (`db = max(db, peakDb)`), release 20 dB per second, hold 1.5 s then fall at 20 dB/s, `clipLatched |= clipSamples > 0`; "visible change" = any quantity moved by >= 0.25 dB (about 1 px) or the latch changed.
`composition` (**HOT**): `history::NodeId ProjectSession::pendingActivationNode() const { return pendingActivation_; }`.
`plugin/src/PluginProcessor.h` (**HOT**): `std::atomic<int> activeNumerator{4}, activeDenominator{4};` set in `processBlock` from `tt.timeSignature` (relaxed stores, two integers, no allocation); `chopfractal::render::MeterReading takeMeter() { return renderer_.meter().take(); }`; `NoticeLog& noticeLog();` (owned by the processor, message thread); `void setStatusMessage(const juce::String&, Severity = Severity::Info);` (also appends non-empty text to the log; the old one-argument calls compile unchanged); `pollAudioFlags()` drains `s.takeNotices()` unconditionally at its end (inside the same `withSession` call as the boundary/Evolve work when one ran) and logs them (today notices raised by Evolve steps or loop-boundary switches accumulate unseen until the next button press).
`EditorModel` (F00, **HOT**): `pendingNode()`, `activeVoicesMax()`, `dropHover()/setDropHover(bool)`, `syncText(bool compact)`, `takeMeter()` pass-through. `Change::Flags` is emitted when `pendingNode` changes or `dropHover` toggles.
`EditorCommands` (**HOT**): `run()` logs every notice of the action (Info/Warning/Error by `Notice::Level`) instead of only the last, keeps the last text as the status as today, logs `status.error().message` as Error; `requestLoad(const juce::File&)` (confirm-then-`loadFileAsync`; `std::function<void(std::function<void(bool)>)> confirmReplace` hook, default shows the AlertWindow); `activateNode` sets the queued/switched messages above.
`ChopFractalEditor` (**HOT**) additionally derives `juce::FileDragAndDropTarget`: `isInterestedInFileDrag(files)` (true if any accepted extension), `fileDragEnter/Exit`, `filesDropped(files, x, y)`.
Threading: `OutputMeter::push` runs on the audio thread (lock-free, allocation-free); everything else is message thread. The audio thread gains: one pass over the output buffer, three relaxed atomic stores, two integer stores in `processBlock`.

## 6. Behavior details and edge cases
- **Timing:** `MeterStrip` owns a 30 Hz `juce::Timer`; each tick calls `proc.takeMeter()`, `updateMeter`, and repaints only when it returns true (so a silent, idle plugin repaints 0 times per second). Tick interval is capped by `dt = max(0.001, elapsed)` to survive host throttling.
- **Clip semantics:** the meter is post-gain, post-dry-mix output of `Renderer::process`. The latch persists across stops; it is cleared by clicking the chip and when the editor is closed and reopened (the latch lives in the editor). `ExportReport::clipped` (offline) is unrelated and unchanged.
- **Voices:** value is the renderer's `activeVoices()` computed on the audio thread at the end of `process` (preview voices excluded, as `activeVoices()` documents). Max shown is `limits::kMaxVoices` = 8 (equals `Config::maxVoices`).
- **Mono hosts:** channels = 1: `peak[1] = peak[0]`.
- **Pending activation:** `ProjectSession::activate(node, Quantize::LoopBoundary)` stores the node; the model reads `pendingActivationNode()` each 10 Hz tick under the session lock. If `onLoopBoundary()` or a history deletion clears it, the ring disappears on the next tick and the `Switched to` message is posted only when the active node equals the queued one. A second click on another node replaces the queued one (message updates). When no pattern is playing, `pollAudioFlags()` applies the switch at once (existing).
- **Notices:** `Notice` text is never altered; length limit for display 240 characters (longer text is elided in the status line, full in the log). Duplicate consecutive identical entries within 2 s are collapsed (counter suffix `(x2)`) to prevent flooding from Evolve failures. The log is cleared on `Clear` only, not on project load.
- **Drag and drop:** only the message thread receives drops; the confirmation hook is invoked only if `session.pattern() != nullptr || session.history().size() > 0`; `Cancel` leaves everything untouched. Decoding still uses `loadFileAsync` (worker), so the editor stays responsive; limits (60 s / 100 MB) and error strings are unchanged. Linux hosts that do not forward OS drags simply never call the target (documented).
- **Accessibility:** MeterStrip has title `Output level` and its description (`Left -12.3 dB, right -11.8 dB`) is refreshed at 2 Hz (not per frame) so screen readers are not flooded; VoiceBox title `Active voices`; SyncBox title `Tempo and meter`; the CLIP chip is a focusable button titled `Clear clip indicator` and the first transition to latched posts one announcement `Output clipped` (`AnnouncementPriority::medium`).
- **Determinism:** nothing here touches generation. pattern_engine golden hashes (2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128) cannot change; renderer output is byte-identical with and without the meter (test).
- **Real-time:** `OutputMeter::push` reads only the output buffer already owned by the callback; denormals handled by the existing `ScopedNoDenormals`; no branch on UI state.

## 7. Test plan
**Unit, audio_renderer (`test_renderer.cpp`)**
- `meter_reports_peak_clip_and_voices`: pass-through input sine 0.5 -> `take().peak[0]` within 1e-4 of 0.5; second `take()` -> 0 (reset); input 1.5 with `outputGainDb = 0` -> `clipSamples == number of samples with |x| > 1`; NaN input sample does not set clip or peak; generated playback running -> `activeVoices == renderer.activeVoices()` and >= 1 at the first event; mono -> `peak[1] == peak[0]`.
- `meter_does_not_change_the_audio`: render the existing fixture twice (current build output hash pinned from the pre-change run in step 1) -> byte-identical.
- Existing allocation-counter test (`operator new` counter around `process`) stays green and now includes `push`; `static_assert` lock-free atomics compile on all CI targets.
- TSan (`audio_renderer` job): `meter_is_race_free` runs `process` on one thread and `take()` on another for 200 ms; no report.
**Unit, plugin_ui_adapter (`test_status_text.cpp`)**
- `describe_sync_covers_every_combination` (the four fallback combos x playing/stopped x compact; exact strings from section 3); `severity_prefix_values`; `peak_to_db_and_fraction` (0.5 -> -6.02 dB, 0 -> -60, 2.0 clamps to +6, NaN -> -60, fraction 0/1 at the ends, 0 dB -> 60/66); `meter_ballistics_attack_release_hold_and_latch` (attack instant; after 1.0 s of zero input from 0 dB the bar is -20 +/- 0.5 dB; hold line stays 1.5 s then falls; `clipLatched` stays true until the test resets it; `updateMeter` returns false for 100 consecutive silent calls once decayed).
**Unit, plugin (`test_notice_log.cpp`, no DISPLAY)**: `ring_keeps_the_newest_100_entries_and_bumps_the_revision`, `duplicates_within_two_seconds_collapse_with_a_counter`, `clear_empties_and_bumps_the_revision`, `severity_mapping_from_notice_level`.
**Integration (shell, `test_feedback.cpp`)**
- `processor_publishes_tempo_meter_and_meter_readings`: FakePlayHead 100 BPM, 3/4, playing; run 20 blocks; `activeBpm==100`, `activeNumerator==3`, `activeDenominator==4`, `takeMeter().peak[0] > 0` with a pattern, `activeVoices` in [0,8]; without host tempo: `usedFallbackTempo` true and SyncBox text for 120 BPM matches `120.0 BPM (manual)  4/4 (assumed)  Stopped`.
- `session_notices_reach_the_log_without_a_button_press`: queue a warning through the public API (`setRules` with `templates::preserveFirst(1, "kick")` + `templates::excludeRole(2, "kick", 0, -1)`; the test first asserts `roles::validateRules` returns at least one issue), call `pollAudioFlags()` -> `noticeLog()` contains `Rule ...` with Warning severity and `session.notices()` is empty afterwards.
- `run_logs_every_notice_not_just_the_last`: an action producing two notices (`history cap` + `Some events were adjusted`) -> 2 log entries, status shows the last.
**GUI (xvfb, `test_feedback_gui.cpp`)**
- `statusbar_shows_severity_prefix`: `setStatusMessage("a", Warning)` -> `StatusBar::displayedText()` == `[!] a`; Error `[x] a`; Info `a`.
- `clip_chip_latches_and_click_resets`: output gain +12 dB, dry mix 1, loud input block via `processBlock`; pump 100 ms; `MeterStrip::clipLatched()`; mouse down on the chip -> false.
- `meter_strip_does_not_repaint_when_idle`: debug counter `MeterStrip::repaintCount()` unchanged over 500 ms of silence.
- `pending_activation_is_shown_on_the_tree_and_status`: build 2 nodes; `hostPlaying=true`; `activateNode(first)`; `HistoryPanel::pendingNode()==first`, status `Variation "<label>" will start at the next loop.`; `onLoopBoundary()`; pump -> `pendingNode()==kNoNode`, status `Switched to "<label>".`.
- `notice_log_popup_lists_entries_newest_first_and_clear_empties`: open via `StatusBar::showLog()` (test hook returns the popup component), row count equals entries, first row is the newest; `Clear` button -> `No notices yet.`.
- `drag_and_drop_loads_a_wav_after_confirmation`: write the drum loop to a temp WAV; `isInterestedInFileDrag({wav})` true, `{"a.txt"}` false, `{"A.AIFF"}` true; with a pattern present and the `confirmReplace` hook returning false -> session source unchanged; returning true -> after `waitFor` the source name equals the file stem and history is empty; two files -> status/log contains `Only the first file was loaded.`; `fileDragEnter` -> `WavePanel::isDropHighlighted()`, `fileDragExit` -> false.
- Existing `editor_buttons_drive_the_session` stays green (it never calls `Load Loop`).
**Manual QA:** (1) Play a loud loop with +12 dB gain: CLIP latches red, click clears. (2) Move the output gain: bars follow within one frame; stop transport: bars fall at ~20 dB/s and stop repainting (check CPU 0 % in the plugin window). (3) Disable host tempo (standalone-like host): SyncBox shows `(manual)` and `(assumed)`. (4) While playing, click another variation: dashed ring and `will start at the next loop`; message flips at the loop point. (5) Trigger several warnings (Mutate with no pattern, Lock Bar with no selection, Evolve failure): the log lists all with prefixes. (6) Drag a WAV from Explorer/Finder onto the plugin: highlight, confirm dialog if a pattern exists, then it loads. (7) Resize to 720 px: SyncBox compact, text elided, no overlap.

## 8. Acceptance criteria
- [ ] `OutputMeter::push` adds no allocation or lock (allocation test green; TSan test green); `processBlock` diff is limited to the tempo/meter atomic stores.
- [ ] Renderer output is byte-identical with the meter present (pinned hash).
- [ ] Peak readout is within 0.1 dB of the true block peak for the test signals; clip count equals the exact number of samples with `|x| > 1`.
- [ ] CLIP stays latched until clicked; reopening the editor clears it.
- [ ] SyncBox strings match section 3 exactly for all combinations; the old status-line fallback suffixes are gone.
- [ ] The tree ring and both pending-activation messages appear/disappear exactly with `hasPendingActivation()`.
- [ ] The log keeps exactly the newest 100 entries, shows severity prefixes, and every notice of a multi-notice action is present; notices from `pollAudioFlags` are logged and the session notice queue is drained.
- [ ] Dropping a supported file loads it (after confirmation when a pattern exists); unsupported extensions are not accepted; `Load Loop` asks the same confirmation.
- [ ] Idle plugin repaints 0 times per second (meter and status); playing repaints only the meter, Orbit playhead and changed widgets.
- [ ] pattern_engine golden hashes unchanged; no state schema or manifest change.
- [ ] `docs/BUILD_STATUS.md` "no meters" gap removed.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| modules/audio_renderer/include/.../meter.hpp | New | `OutputMeter`, `MeterReading` |
| modules/audio_renderer/include/.../renderer.hpp, src/renderer.cpp, tests/test_renderer.cpp | Mod, **HOT** | `meter()` accessor, `push` call, tests |
| modules/plugin_ui_adapter/include/.../status_text.hpp, src/status_text.cpp, tests/test_status_text.cpp | New | text and ballistics |
| composition/include/.../project_session.hpp | Mod, **HOT** | `pendingActivationNode()` |
| plugin/src/PluginProcessor.h/.cpp | Mod, **HOT** | atomics, `takeMeter`, log, `setStatusMessage` severity, notice drain |
| plugin/src/NoticeLog.h/.cpp, NoticeLogPanel.h/.cpp, MeterStrip.h/.cpp | New | log, popup, meter widget |
| plugin/src/StatusBar.*, HistoryPanel.*, WavePanel.* | Mod | widgets, ring, drop highlight |
| plugin/src/EditorModel.*, EditorCommands.* | Mod, **HOT** | pending, drop hover, sync text, notice logging, `requestLoad` |
| plugin/src/PluginEditor.h/.cpp | Mod, **HOT** | `FileDragAndDropTarget` |
| plugin/tests/test_notice_log.cpp, test_feedback.cpp, test_feedback_gui.cpp | New | tests |
| docs/BUILD_STATUS.md | Mod | gaps |

## 10. Risks and mitigations
- StatusBar width pressure with F01/F04 widgets: compact SyncBox below 900 px and text elision; verify at 720 px (QA 7). Fallback: move the view/scale controls into a popup menu.
- Meter cost on large blocks: one pass over <= 2 channels; measured in the renderer benchmark (budget < 0.5 % of one core at 48 kHz, 64-frame blocks); fallback is `push` every second block.
- False "pending" ring after a deleted branch: the model compares against `history().contains` through the existing clear in `deleteBranch`; covered by a test deleting the queued branch.
- Hosts that do not forward OS drags (some Linux): documented; Load Loop remains.
- Log noise from Evolve failures: duplicate collapse plus the capacity cap.
- Latched clip fatigue on tracks that clip constantly: the chip is a single click to clear and never blocks audio; a later preference could auto-clear after N seconds (not in scope).

## 11. Implementation steps
1. `OutputMeter` + renderer hook + unit/TSan/allocation tests (audio_renderer only).
2. `status_text` (sync text, ballistics, severity prefix) + tests.
3. `NoticeLog`, processor `setStatusMessage(severity)`, notice drain in `pollAudioFlags`, `run()` logging every notice + tests.
4. Processor tempo/meter atomics, `takeMeter`; MeterStrip, VoiceBox, SyncBox in StatusBar; log popup; remove the old fallback suffixes.
5. `pendingActivationNode`, tree ring, queued/switched messages + test.
6. Drag-and-drop, drop highlight, `requestLoad` confirmation for drops and `Load Loop` + tests.
7. QA pass at 720 and 1920 px, docs update.
