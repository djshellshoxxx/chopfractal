# F09 Evolve scenes

**Status:** Ready for development  **Size:** L (6 engineer-days; README budgets 4 for the list/UI, the extra 2 are the sample-accurate loop-boundary switch in the renderer)  **Depends
on:** F00 (ScenePanel, FeaturePanel, command layer), F01 (the `scenes` StateSection)  **Blocks:** none

## 1. Summary and user value
Evolve makes a beat wander, but a good wander is lost the moment it passes. F09 records an Evolve run as numbered **scenes** (each a pattern snapshot plus its seed), lets the producer
also capture scenes by hand, then **plays the scenes back in sequence, N loops each, switching exactly on the loop boundary**, with simple chaining (next, repeat, jump, stop, wrap). The
scene list can be exported (list, MIDI per scene, one WAV of the whole sequence), so a wander becomes an arrangement. This turns Evolve from a toy into a song-building tool, and the
sample-accurate switch fixes the audible late-downbeat of today's quantized variation switch for the scene path.

## 2. User stories and scope
- As a producer I can press "Rec Scenes" while Evolve runs and get one scene per Evolve step (plus the starting pattern), so I never lose a good moment.
- As a producer I can capture the current pattern as a scene at any time, rename it, set how many loops it plays and what follows it.
- As a producer I can press "Play Scenes" and hear the scenes in order, each switch landing exactly on the first sample of a loop, while the host plays.
- As a producer I can keep a scene as a normal variation in the family tree, or export the whole list.
- **In scope:** portable module `scenes` (list, validation, sequencer, serialization), renderer armed switch (`ArmedSwitch`, `armMailbox`), session integration (`captureScene`, record,
  play, keep, export), ScenePanel, `scenes` StateSection, tests.
- **Non-goals:** live editing of a scene while it plays (any pattern-changing command stops scene playback), per-scene Chaos/Morph/mixer settings (Chaos applies globally), undo/redo of
  scene-list edits (delete asks no confirmation but Clear All does), tempo or meter changes between scenes (all scenes of one project share the pattern grid; a scene whose length
  differs plays at its own length), scene automation lanes in the DAW, MIDI program-change scene recall, converting the existing `activate(node, Quantize::LoopBoundary)` to the armed
  switch (possible follow-up; left unchanged here).

## 3. UX
**Entry:** FeaturePanel toggle **"Scenes"** (title "Show the scene list") shows/hides **ScenePanel** (`plugin/src/panels/ScenePanel.{h,cpp}`, a 200 px row docked under PatternPanel;
hidden by default).

**ScenePanel (component name `"ScenePanel"`):**
- `ListBox` **"Scene list"** (max 32 rows), row text `"3  Evolve step 2   4 loops   > next"` (number = position + 1; the sequence marker `>` is followed by `next`, `repeat`, `stop`, or
  `jump to 5`); the playing row ends with `"  [Playing, loop 2 of 4]"`, the armed row with `"  [Next]"` (text, not colour only). Rows whose pattern no longer fits the chops end with `"
  (does not fit the chops)"`.
- Detail form for the selected row: TextEditor **"Scene name"** (1 to 32 characters), Slider **"Scene loops"** (1 to 64 integer, default 4), ComboBox **"After this scene"** (`"Next
  scene"` default, `"Repeat this scene"`, `"Stop"`, `"Jump to..."`), ComboBox **"Jump target"** (visible for Jump: `"Scene N: name"` items), label with `seed`, `events`, `bars` (read
  only).
- Buttons: **"Capture Scene"** (always enabled with a pattern), **"Update Scene"** (replaces the selected scene's pattern with the current one), **"Keep Scene"** (adopt as the current
  pattern and add a family-tree node), **"Move Up"**, **"Move Down"**, **"Delete Scene"**, **"Clear All"**, **"Export Scenes..."**.
- ToggleButtons **"Rec Scenes"** (title "Record scenes while Evolve runs"), **"Play Scenes"** (title "Play the scenes in sequence"), **"Wrap"** (default on: after the last scene
  continue with the first). Button **"Stop Scenes"**.
- Status label (10 Hz): `"Scene 3 of 7, loop 2 of 4"`, `"Scenes stopped"`, `"Waiting for the host to play"` (armed but transport stopped), `"Recording scenes: waiting for Evolve"`.

**Enable rules:** Capture/Rec need a pattern; Play needs at least one scene, a source and the transport not necessarily running (see 6); Move/Delete/Update/Keep need a selection;
Delete, Clear All, Move are disabled while scenes play (tooltip `"Stop scene playback first."`); Update and Keep are allowed while playing and stop playback first (they change the
current pattern). Keyboard: list supports Up/Down; Tab order list, form fields, buttons in the order above.

**Messages (StatusBar, exact):** `"Captured Scene 3."`; `"The scene list is full (32). Delete a scene to capture more."`; `"Scene recording stopped: the list is full (32)."`; `"Capture
at least one scene first."`; `"Scenes need a pattern and a source."`; `"Playing scenes from Scene 1."`; `"Scene sequence finished at Scene 7; it keeps looping."`; `"Scene 3 no longer
fits the chops and was skipped."` (Warning); `"No scene fits the current chops; scene playback stopped."`; `"Scene playback stopped because you changed the pattern."`; `"Evolve was
switched off: scenes and Evolve cannot run together."`; `"Scene playback stopped: Evolve was switched on."`; `"Cancel the morph before playing scenes."`; `"Kept Scene 3 as a new
variation."`; `"Exported 7 scenes to <folder>."`; `"Delete all scenes?"` / `"This cannot be undone."` (AlertWindow, buttons `"Delete All"` / `"Cancel"`). Empty state of the list: `"No
scenes yet. Press Capture Scene, or turn on Rec Scenes and start Evolve."`

## 4. Data model and state
New portable module `modules/scenes` (`chopfractal::scenes`, header `include/chopfractal/scenes/scenes.hpp`, depends only on `chop_contracts` for `Result/Status` and `bytes.hpp`;
manifest `module.json`, `MIGRATION.md`, tests).
```cpp
namespace chopfractal::scenes {
constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::size_t kMaxScenes = 32;
constexpr int kMaxLoops = 64;
constexpr std::size_t kMaxNameLength = 32;
constexpr std::size_t kMaxPatternBytes = 1u << 20;       // per scene, serialized pattern
using SceneId = std::uint32_t;                           // stable, never reused in a list; display number = position + 1
enum class Next : std::uint8_t { Advance = 0, Repeat = 1, Stop = 2, Jump = 3 };
struct Scene {
  SceneId id = 0; std::string name; int loops = 4; Next next = Next::Advance; SceneId jumpTo = 0;
  std::uint64_t seed = 0;                  // pattern.settings.seed at capture
  std::uint64_t evolveStep = kManual;      // Evolve step index when recorded, else kManual
  std::uint32_t eventCount = 0; std::int32_t bars = 0;
  std::vector<std::uint8_t> pattern;       // pattern::serialize() bytes, validated on load
  static constexpr std::uint64_t kManual = ~0ull;
};
struct SceneList { std::vector<Scene> scenes; SceneId nextId = 1; bool wrap = true; };
Status validate(const SceneList&);         // counts, name length, loops 1..64, unique ids, Jump targets exist, pattern size <= kMaxPatternBytes
Result<SceneId> add(SceneList&, Scene);    // assigns id = nextId++, LimitExceeded at 32
Status remove(SceneList&, SceneId);        // also clears dangling Jump targets to Advance
Status move(SceneList&, SceneId, int newIndex);
const Scene* find(const SceneList&, SceneId);
std::vector<std::uint8_t> serialize(const SceneList&);
Result<SceneList> deserialize(const std::uint8_t* data, std::size_t size);
struct Advance { SceneId arm = 0; bool switched = false; SceneId playing = 0; bool finished = false; };
class Sequencer {                          // pure state machine, message thread, no allocation after construction
 public:
  Advance start(const SceneList&, SceneId first);   // current = first, loopIndex = 0; arms immediately if first.loops == 1
  void stop();
  bool running() const; SceneId current() const; int loopIndex() const; SceneId armed() const;
  Advance onBoundary(const SceneList&);             // call once per observed pattern-loop boundary
  void onTransportStopped();                        // loopIndex = 0, armed = 0 (caller cancels the audio-side arm)
  Advance onTransportStarted(const SceneList&);     // re-arms if current.loops == 1
};
}
```
`onBoundary` rules: (1) `++loopIndex`. (2) If `armed != 0 && loopIndex >= current.loops`: `current = armed; loopIndex = 0; armed = 0; switched = true`. (3) If `next == Repeat` and
`loopIndex >= current.loops`: `loopIndex = 0`. (4) If `loopIndex >= current.loops` and no armed scene exists (Stop was decided): `running = false; finished = true`. (5) After that, if
`armed == 0 && loopIndex == current.loops - 1 && !finishing`: decide the next scene: Advance -> following scene in list order (or the first if `wrap`, else decide Stop), Jump ->
`jumpTo`, Repeat -> none needed, Stop -> mark `finishing`; the chosen id is returned in `arm` (and remembered as `armed`). `loops == 1` therefore arms the following scene as soon as the
current one starts. **StateSection `scenes` (schema 1, F01 API; absent when the list is empty so projects without scenes are unchanged):** `u8 flags (bit0 wrap)`, `u32 nextId`, `u32
count (<= 32)`, per scene: `u32 id`, `str name (<= 32)`, `i32 loops`, `u8 next`, `u32 jumpTo`, `u64 seed`, `u64 evolveStep`, `u32 eventCount`, `i32 bars`, `u32 patternSize (<= 1 MiB)`,
`bytes pattern`. Whole section <= 32 MiB (about 1.3 MB typical). The loader validates every field and each pattern with `pattern::deserialize`; any error rejects the project load
all-or-nothing (same policy as the other modules). Runtime state (running, current, loopIndex, armed, recording flag) is never saved; a project always loads with scenes stopped. **Host
parameters:** none (manifest unchanged).

**Renderer types (`audio_renderer/renderer.hpp`):**
```cpp
struct ArmedSwitch { std::shared_ptr<const Playback> playback; std::uint64_t token = 0; };  // playback == nullptr cancels the pending switch
```
`Mailbox<ArmedSwitch>& Renderer::armMailbox();` `std::uint64_t Renderer::appliedSwitchToken() const;` (atomic, written with release by the RT thread each time a switch is applied).

## 5. Public API
- **Renderer (hot file `renderer.cpp`):** as above. RT rules unchanged: no allocation or locks; the RT thread only acquires from the arm mailbox and stores an atomic.
- **Composition (`composition/src/scenes.cpp`, declarations appended to `ProjectSession`, hot header):**
```cpp
struct SceneExportOptions { bool list = true; bool midi = true; bool wav = false; double bpm = 120.0; int baseNote = 36; wav::Options wavOptions; bool overwrite = false; };
struct SceneStatus { bool running = false; scenes::SceneId playing = 0; scenes::SceneId armed = 0; int loopIndex = 0; int loops = 0; bool recording = false; bool waitingForTransport = false; };
void attachArmMailbox(render::Mailbox<render::ArmedSwitch>* mailbox);
const scenes::SceneList& sceneList() const;
Result<scenes::SceneId> captureScene(const std::string& name = {});      // current patterns_.current(), not the Chaos/Morph overlay
Status updateScene(scenes::SceneId);   Status editScene(scenes::SceneId, std::function<void(scenes::Scene&)>);  // name/loops/next/jump; validated
Status removeScene(scenes::SceneId);   Status moveScene(scenes::SceneId, int newIndex);   Status clearScenes();   Status setSceneWrap(bool);
Status setRecordScenes(bool on);       bool recordingScenes() const;
Status playScenes(scenes::SceneId first = 0, Quantize when = Quantize::LoopBoundary);    Status stopScenes();
void onSceneBoundary(std::uint64_t appliedToken);   // processor calls it after onLoopBoundary(), same lock
void onTransportChanged(bool playing);              // processor calls on host play/stop edges
SceneStatus sceneStatus() const;
const pattern::Pattern* displayPattern() const;     // the scene (or F10 morph) override when active, else pattern(); panels draw this
Status keepScene(scenes::SceneId, const std::string& label = {});  // stops playback, finalize(scene pattern, "Scene N")
Result<ExportReport> exportScenes(const std::string& directory, const SceneExportOptions&) const;
```
`displayPattern()` is introduced by whichever of F09/F10 merges first with exactly this meaning; the second reuses it. Private helper `Result<std::shared_ptr<const render::Playback>>
buildPlayback(const pattern::Pattern&)` is extracted from `installPlayback` (flatten + `makePlayback`; `installPlayback = buildPlayback + publish`, behavior unchanged; with F08 the
Chaos overlay is applied inside it). Plugin: `pollAudioFlags()` calls `s.onSceneBoundary(renderer_.appliedSwitchToken())` after `onLoopBoundary()` when `boundaryFlag_` fires, and
`s.onTransportChanged(playing)` on edges of `hostPlaying`; the editor attaches `renderer_.armMailbox()` via `session_.attachArmMailbox` in the processor constructor. Threading: all
session calls on the message thread under `withSession`; the sequencer is only touched there.

## 6. Behavior details and edge cases
**Capture.** `captureScene` serializes `patterns_.current()`; name default `"Scene N"` (N = list size + 1) or `"Evolve step K"` when recorded (K = step index + 1); `loops` = Evolve
`everyLoops` if Evolve runs else 4; `evolveStep` = `evolve_.stepsTaken() - 1` while running else `kManual`; skipped (returns the last scene's id, no new scene, Info notice not shown) if
the bytes equal the last scene's pattern. Errors: no pattern `InvalidArgument`; full `LimitExceeded` with the message above; a pattern over 1 MiB `LimitExceeded`.

**Recording.**
`setRecordScenes(true)` immediately captures the starting pattern as `"Scene 1 (start)"` if the list is empty or the last scene differs, then `evolveStep()` (features.cpp, one added
call) captures after every successful step while recording. When the list reaches 32 recording turns itself off with the message. Recording does not create family-tree nodes (Evolve's
rule stands).

**Playback and the armed switch.** `playScenes(first, when)`: refuses (messages above) without pattern/source/scenes or during a morph; stops Evolve if running (notice);
builds `scenes::Sequencer::start`. With `Quantize::LoopBoundary` and the host playing, the first scene is **armed** like any later one (it begins at the next loop boundary); otherwise
(`Immediate`, or transport stopped) it is installed at once. Installing a scene = `sceneOverride_ = deserialize + sanitize` and `publish(buildPlayback(...))`; `patterns_`, history and
undo are **not touched** (scene playback is an override, so saved state always holds the user's real pattern and a 30-minute run cannot evict undo history). A scene that fails
`sanitize`/`buildPlayback` is skipped (warning) and the next one tried; all failing stops playback. *Arming:* when `Sequencer` returns `arm = id`, the session builds that scene's
`Playback` (same source pointer) and publishes `ArmedSwitch{pb, ++token}` to the arm mailbox, keeping a `shared_ptr` (`armedHold_`) alive. *Audio thread:* once per host block, before
any rendering, `Renderer::Impl::process` (a) acquires the main mailbox and adopts it only when the pointer differs from `mainSeen_` (new member; the old `pb != current` test would
revert a boundary switch), (b) acquires the arm mailbox; a new token sets `armed_ = playback.get()` (nullptr = cancel), (c) if `armed_ == current_` clears it, (d) if `armed_` is set,
the transport is playing, `current_` is a real (not pass-through) playback with the same `source` pointer and `armed_->lengthTicks > 0`, it computes `pos0` (host ppq in ticks, or
`freeRunTicks`), `L = current_->lengthTicks`, `B = ceil(pos0 / L - 1e-9) * L`, `off = ceil((B - pos0) / tpf - 1e-6)`; when `0 <= off < frames` the block is processed in two spans split
at `off` (each span still chunked by `maxBlock`; the second span's ppq is `pos0 + off*tpf`, so `expectedTicks` stays continuous and no voice is released), `current_ = armed_` between
the spans, `armed_ = nullptr`, `appliedToken_.store(token, release)`. Sounding voices keep ringing across the switch (same source buffer). Switch accuracy: the first event of the new
scene at tick `B` starts at frame `off` of the block (+-1 frame, the same rounding as `schedule()`). *Message thread:* on each observed boundary (`onSceneBoundary(appliedToken)`): call
`Sequencer::onBoundary`; if it reports `switched`, set `sceneOverride_` to the new scene's pattern and `publish` the same `Playback` pointer into the **main** mailbox (the RT thread
sees an unchanged `current_`, no voices are killed); if it reports `arm`, build and publish the next `ArmedSwitch`; if `finished`, post `"Scene sequence finished at Scene N; it keeps
looping."` and set `running = false` (the last scene simply keeps looping).

**Late arm fallback:** if a switch is due but `appliedToken < armedToken_` (the arm arrived after the
boundary), install the scene immediately through the main mailbox (up to one 30 Hz tick late, the existing behavior) and cancel the arm with `ArmedSwitch{nullptr, ++token}`.
`publish(pb)` (the private method, called by every other path) cancels any pending arm unless `pb == armedHold_`, so an edit or a loaded project can never be overwritten by a stale
scene. *Transport:* `onTransportChanged(false)` -> `Sequencer::onTransportStopped` + cancel token; scene stays the displayed one. `onTransportChanged(true)` -> `onTransportStarted`
(re-arms for `loops == 1`). Seeks/loop jumps are not tracked beyond the boundary flag; the worst case is one scene counted short or long, corrected at the next switch.

**Stopping.**
`stopScenes()` (button, or any command that changes the pattern: Generate, Mutate, Fractal, Zoom, Collapse, `edit`, `recallSnapshot`, `activate`, `undo`, `redo`, `loadState`,
`loadSource`, `reset`, `setEvolve(enabled=true)`) clears the override, cancels the arm and `republish()`es the real pattern (done in `finalize`/the listed entry points by one call
`stopScenesForEdit()` defined in scenes.cpp; hot-file diff is one line each).

**Evolve interaction.** Mutually exclusive: starting scenes switches Evolve off; switching Evolve on stops
scenes. Recording needs Evolve running to produce steps beyond the starting scene. Replaying a recorded wander reproduces the recorded patterns exactly (they are stored, not
regenerated).

**Variation tree.** Scenes are independent of the tree; `keepScene` stops playback, `finalize`s the scene's pattern (one history node labelled `"Scene N"`, child of the
active node, one undo step). Deleting tree nodes never affects scenes.

**A/B:** untouched.

**Locks/userOwned:** preserved inside the stored pattern bytes.

**Chaos (F08) / Morph (F10).**
Scenes are patterns; `buildPlayback` applies the Chaos overlay when F08 is present, so Chaos rides on scenes. Morph preview is refused while scenes play and vice versa. **Export
(`exportScenes`).** Files in `directory` (created if missing, atomic writes, refuse existing files unless `overwrite`): `scenes.tsv` (header
`number\tname\tloops\tnext\tjump_to\tseed\tevolve_step\tevents\tbars`, one row per scene, names with tabs/newlines replaced by spaces, `evolve_step` is `-` for manual, `jump_to` is a
number or empty, LF endings, trailing newline, deterministic bytes); `scene_NN_<sanitized name>.mid` per scene (NN = two-digit position) using the same note/velocity/duration rules as
the Producer Kit's `pattern.mid` (factor that loop in `features.cpp` into a shared `buildPatternMidi(pattern, chops, bpm, baseNote, loops)`; `loops` = the scene's loops); `scenes.wav`
when requested: each scene i is rendered with `render::renderOffline(playback_i, {cycles = loops_i, tailSeconds = 0.5})` and mixed (sum) into one buffer at offset `sum_{j<i}
ceil(loops_j * lengthTicks_j / tpf)` frames, so tails overlap the next scene like live ring-over; export order is list order and ignores `next`/`jump`; encoded with the same
`wav::encode`/`writeFileAtomic` path as `exportWav`. The WAV equals the live result only when tails do not overlap voices that would be stolen live; this is documented in the UI tooltip
of "Export Scenes...".

**Limits.** 32 scenes, 64 loops, 1 MiB per pattern.

**Determinism.** No randomness; the sequencer is a pure function of (list, boundary count); the audio switch
frame is a pure function of host position. The four pattern_engine golden hashes are untouched (this feature never calls generate/mutate itself).

## 7. Test plan
**Unit, `modules/scenes/tests/test_scenes.cpp`:** `add_remove_move_keep_stable_ids_and_the_32_scene_cap`; `validate_rejects_bad_loops_names_dangling_jumps_and_oversized_patterns`;
`remove_clears_dangling_jump_targets`; `serialize_roundtrips_and_rejects_truncation_and_bitflips` (every prefix length and 2000 random byte flips: either `Corrupt`/`UnsupportedVersion`
or a list that passes `validate`); `sequencer_trace_matches_the_table` (list A loops 2 Advance, B loops 1 Advance, C loops 3 Stop, wrap off; `start(A)` -> arm 0; boundary 1 -> arm B; 2
-> switched B and arm C; 3 -> switched C; 4 -> nothing; 5 -> finishing; 6 -> finished, running false); `single_loop_scenes_arm_immediately`; `wrap_repeat_and_jump`;
`transport_stop_resets_loop_index_and_clears_arm`; `sequencer_is_deterministic_for_a_boundary_count`.

**Unit, `modules/audio_renderer/tests/test_armed_switch.cpp`:**
`armed_switch_lands_exactly_on_the_loop_boundary_frame` (scene A: one click event at tick 0, scene B: a different click chop at tick 0; 120 BPM, 48 kHz, 1-bar loop = 96000 frames; arm B
before the boundary; a 512-frame block straddling frame 96000: A's click at frame 0 of loop 1 only, B's click first sample at the exact frame +-1; no A event after the boundary);
`switch_is_identical_for_block_sizes_1_7_64_333_512_2048`; `voices_ring_over_the_switch` (a long chop started before the boundary is still sounding after it);
`cancel_token_clears_a_pending_switch`; `switch_is_ignored_for_a_different_source_pointer_or_pass_through`; `main_mailbox_publish_of_the_same_playback_neither_reverts_nor_kills_voices`;
`main_mailbox_publish_of_a_new_playback_replaces_a_boundary_switch`; `no_arm_means_bit_identical_output_to_the_previous_renderer` (hash recorded from `main` before the change);
`armed_path_performs_no_heap_allocation` (extend the replaced-operator-new harness); `arm_mailbox_stress_under_tsan` (publisher spamming arm/cancel while RT processes). **Integration,
`composition/tests/test_scenes.cpp`:** `capture_stores_pattern_seed_and_dedupes_identical_captures`; `capture_fails_at_32_with_the_exact_message`;
`evolve_recording_captures_start_and_each_step_and_stops_when_full`; `recorded_scenes_replay_the_exact_patterns` (reload from state, play: serialized pattern of each active scene equals
the stored bytes); `playing_scenes_arms_the_next_scene_and_adopts_it_when_the_renderer_reports_the_switch` (real `Renderer` + mailboxes, drive blocks across boundaries; assert
`sceneStatus().playing` sequence and that `pattern()` bytes never change); `late_arm_falls_back_to_immediate_install_and_cancels_the_arm`;
`transport_stop_cancels_the_arm_and_start_resumes`; `any_pattern_change_stops_scenes_with_the_notice` (Generate, Mutate, `edit`, `undo`, `recallSnapshot`, `activate`);
`evolve_and_scenes_are_mutually_exclusive`; `scene_that_no_longer_fits_is_skipped_with_a_notice` (delete a chop used by scene 2); `saved_state_holds_the_real_pattern_while_scenes_play`;
`scenes_section_roundtrips_is_absent_when_empty_and_hostile_sweep_is_safe`; `keep_scene_adds_a_child_of_the_active_node_and_one_undo_step`; `delete_and_move_are_refused_while_playing`;
`export_scenes_writes_tsv_midi_and_wav_deterministically` (hash of `scenes.tsv` and the WAV for fixed inputs; second export without overwrite fails with the existing-file error);
`export_wav_places_scenes_at_cumulative_offsets_and_overlaps_tails`. **GUI, `plugin/tests/test_scenes_gui.cpp`** (xvfb, F00 helpers): `scene_panel_captures_plays_and_exports`: load
`drumLoop()`, Detect Chops, Generate; click "Scenes"; click "Capture Scene", click "Mutate", click "Capture Scene"; `findListBox("Scene list")->getNumRows() == 2`; select row 0, set
"Scene loops" to 1 via `setValue(1, sendNotificationSync)`; click "Play Scenes" with a `FakePlayHead` playing from ppq 0 and run blocks across two loop boundaries calling
`pollAudioFlags()` after each; assert `sceneStatus().playing` is scene 2 then (wrap) scene 1; click "Stop Scenes" and assert `pattern()` bytes equal the pre-play bytes; click "Clear
All" and answer the AlertWindow via `AlertWindow::getCurrentlyModalComponent` button "Delete All"; assert 0 rows and the empty-state text. Also `rec_scenes_with_evolve`: toggle "Evolve"
and "Rec Scenes", advance 8 loops of midpoints, assert >= 3 rows. All controls have titles.

**Real-time / sanitizer:** TSan on `test_armed_switch` and the module tests; ASan/UBSan on
all; the plugin shell `process()` allocation test unchanged and passing.

**Manual QA:** (1) Generate a 2-bar groove with density 0.7, play, switch Evolve on (every 2 loops, amount 0.3),
press Rec Scenes, wait for 6 steps, switch both off. (2) Scenes list shows 7 rows with sensible names. (3) Press Play Scenes: each scene lasts 2 loops and the downbeat of each new scene
is clean (no old-scene hit on beat 1; check by soloing a kick-only source). (4) Set scene 3 to Repeat for 4 loops, scene 5 "Jump to 2": sequence follows. (5) Stop the host transport and
start again: sequencing resumes. (6) Press Generate: scenes stop with the notice and your original pattern is unchanged. (7) Keep Scene 4, then Undo. (8) Export with WAV on: open
`scenes.wav`, compare with the live playback by ear. (9) Save, close, reopen: list identical, playback stopped.

## 8. Acceptance criteria
- [ ] Up to 32 scenes can be captured manually or recorded from Evolve steps (start scene included), each with name, loops 1..64, next/repeat/stop/jump, and survive save/reload
      byte-exactly.
- [ ] With the host playing, each scene switch happens on the exact loop-boundary frame (+-1) for every host block size tested, with no old-scene event after the boundary and sounding
      voices ringing over.
- [ ] A switch never reverts, kills voices or double-applies when the message thread later publishes the same playback; a stale arm can never overwrite an edit (`publish` cancels it).
- [ ] While scenes play, `pattern()`, history, A/B slots, undo depth and saved state are unchanged; any pattern-changing command stops playback with the specified notice.
- [ ] Evolve and scene playback are mutually exclusive with the specified messages; recording stops itself at 32.
- [ ] `Keep Scene` yields one history node and one undo step; deleting tree nodes never touches scenes.
- [ ] Export writes deterministic `scenes.tsv`, per-scene MIDI and optional `scenes.wav`, never overwriting without confirmation.
- [ ] Projects without scenes: no `scenes` section, bit-identical audio and state; golden hashes `2397844821793184813`, `12084884237330071892`, `13710596758976548913`,
      `18237151833431327128` unchanged; renderer output without an armed switch bit-identical to before.
- [ ] `process()` remains allocation- and lock-free with the armed path; TSan clean; module added with manifest and passing `check_modules.py`, `check_includes.py`, `smoke_transfer.sh`.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/scenes/` (`include/.../scenes.hpp`, `src/scenes.cpp`, `tests/`, `CMakeLists.txt`, `module.json`, `MIGRATION.md`, `README.md`, `examples/minimal.cpp`) | New | list, sequencer, codec |
| root `CMakeLists.txt` (module list), generated module docs | Mod **(hot)** | add `scenes` |
| `modules/audio_renderer/include/.../renderer.hpp`, `src/renderer.cpp` | Mod **(hot)** | `ArmedSwitch`, `armMailbox`, `appliedSwitchToken`, `mainSeen_`, two-span processing |
| `modules/audio_renderer/tests/test_armed_switch.cpp` | New | |
| `composition/src/scenes.cpp`, `composition/tests/test_scenes.cpp` | New | session methods, export, section codec |
| `composition/include/.../project_session.hpp`, `composition/src/project_session.cpp` | Mod **(hot)** | declarations; `buildPlayback` extraction; `publish` cancels stale arm; `stopScenesForEdit()` calls; `sceneOverride_`, `armedHold_`, `scenes_`, `sequencer_` members |
| `composition/src/features.cpp` | Mod | `evolveStep` capture hook; `buildPatternMidi` factored out of `exportKit` |
| `composition/CMakeLists.txt` | Mod | link `scenes`, add sources/tests |
| `plugin/src/PluginProcessor.h/.cpp` | Mod **(hot)** | `onSceneBoundary`/`onTransportChanged` calls, arm mailbox attach |
| `plugin/src/panels/ScenePanel.h/.cpp`, `FeaturePanel.cpp` | New/Mod | UI, "Scenes" toggle |
| `plugin/CMakeLists.txt` | Mod **(hot)** | sources/tests |
| `plugin/tests/test_scenes_gui.cpp` | New | |
| `docs/specs/evolve-mode.md`, `variation-family-tree.md`, `state-presets-automation-export.md`, `BUILD_STATUS.md` | Mod | document scenes |

## 10. Risks and mitigations
- **Renderer change is the riskiest part (hot file, all Wave 3 features follow).** Keep the diff to: new members, the `mainSeen_` adoption rule, and the two-span split; commit the
  "bit-identical without arm" hash test first; revert path = remove the arm acquire (the rest compiles away).
- **Boundary detection mismatch** (audio switches at host-ppq multiples of the playback length; the session counts boundaries from the 30 Hz flag). Mitigation: the `appliedToken`
  handshake and the late-arm fallback; test with block sizes up to 4096 and with free-run (no ppq).
- **Scenes of different lengths.** Allowed, each at its own length; boundary math uses the current playback's length; covered by a test with 1-bar then 2-bar scenes.
- **Memory:** 32 x serialized patterns; the 1 MiB cap and 32 MiB section cap bound it; decoding is done once per switch on the message thread (< 1 ms for 512 events).
- **User confusion about the override.** The panels draw `displayPattern()` and the status label shows `"Scene 3 of 7"`, so the grid matches what is heard; Stop restores the real
  pattern.
- **Export WAV differs from live in rare steal cases.** Documented in the tooltip; MIDI and TSV are exact.

## 11. Implementation steps
1. `modules/scenes`: types, validate, codec, `Sequencer` + tests, manifest/CMake registration. 2. Renderer: record the no-arm hash test, then `ArmedSwitch`, `mainSeen_`, two-span split
   + renderer tests (TSan, no-alloc). 3. Session: `buildPlayback` extraction, scenes state, capture/edit/remove/move, `displayPattern()`, `scenes` section + tests. 4. Playback:
   `playScenes`, `onSceneBoundary`, `onTransportChanged`, stop-on-edit hooks, processor wiring + integration tests. 5. Evolve recording hook + exclusivity. 6. Export (`buildPatternMidi`
   factor, TSV, MIDI, WAV) + tests. 7. ScenePanel + GUI tests. 8. Docs, QA matrix, snapshot review.
