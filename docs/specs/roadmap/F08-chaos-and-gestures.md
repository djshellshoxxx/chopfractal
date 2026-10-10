# F08 Chaos and gestures

**Status:** Ready for development  **Size:** M (5 engineer-days)  **Depends on:** F00 (ControlsPanel, new GesturePanel), F01 (the `chaos` StateSection)  **Blocks:** none (F07 feeds MIDI
CC into the gesture mapper defined here; F10 and F09 feed their patterns through the same `installPlayback` path)

## 1. Summary and user value
One **Chaos** knob (an automatable host parameter) makes the playing beat progressively wilder, deterministically: events thin out, ghost hits appear, chops swap, timing and level
wobble, and the effects already in the engine (reverse, pitch, retrigger, filter, glide, crunch) fire more often and harder, always respecting the Allow toggles and locks. Three hold
gestures, **Stutter**, **Reverse** and **Tape Stop**, act on the audio live (buttons now, MIDI mod wheel/CC through F07), for the classic fill, scratch and power-down moves. Chaos is a
performance overlay (not saved into the pattern) that can be frozen into a normal variation with one button.

## 2. User stories and scope
- As a producer I can automate or ride one Chaos knob from 0 (my pattern exactly) to 100 % so that a loop builds tension without me editing events.
- As a producer I can hold Stutter to freeze the groove into a retriggered slice on the grid, hold Reverse to play everything backwards, and hold Tape Stop to slow the music to a halt
  and spin back up on release.
- As a producer I can press "Freeze Chaos" to turn what I hear into a normal, undoable variation, and "Re-roll" to get a different (but again deterministic) chaos.
- As a producer I can rely on locked bars, hand-placed hits and my Allow toggles being respected.
- **In scope:** `chaos` parameter (manifest v3), `pattern::applyChaos` (pure, in pattern_engine), session integration in `installPlayback`, Mutate/Evolve bias, `render::LiveInput` +
  gesture engine in the renderer, `render::SpscQueue`, CC mapper, GesturePanel and Chaos slider, `chaos` state section, tests.
- **Non-goals:** the host MIDI input plumbing (F07 flips `acceptsMidi` and calls the CC mapper; without F07 the gestures work from the GUI only), per-effect chaos sliders, a chaos
  "amount per bar" lane, gesture recording to host automation (gestures are not host parameters in this release), time-stretch based stutter, chaos on Generate (Generate is never
  affected).

## 3. UX
**ControlsPanel:** slider **"Chaos"** (title "Chaos amount", `SliderAttachment` to parameter `chaos`, 0 to 1 shown as `0 %` to `100 %`, default 0, double-click resets to 0) next to
Density/Swing. **GesturePanel** (new `plugin/src/panels/GesturePanel.{h,cpp}`, a single row docked under ControlsPanel):
- Hold buttons (subclass of `juce::TextButton`, momentary: down = on, release or mouse-exit = off; Shift+click latches until the next plain click, the latched button shows the text
  `"Stutter (latched)"` etc.): **"Stutter"**, **"Reverse"**, **"Tape Stop"** (titles `"Hold to stutter the last hit"`, `"Hold to play backwards"`, `"Hold to slow to a stop"`).
- ComboBox **"Stutter rate"**: `"1/8"`, `"1/16"` (default), `"1/32"`.
- Sliders **"Tape stop time"** (50 to 2000 ms, default 400, suffix ` ms`) and **"Tape spin-up"** (20 to 1000 ms, default 150).
- Buttons **"Freeze Chaos"** (enabled when Chaos > 0 and a pattern exists) and **"Re-roll"**, ToggleButton **"Chaos affects edited hits"** (default off).

Enable rules: gesture buttons are enabled whenever a source with chops is loaded; Stutter does nothing without a previously played hit (see 6).

Messages (StatusBar): `"Chaos applied to the playback; Freeze Chaos keeps it as a variation."` (first time Chaos > 0 in a session, Info), `"Frozen: Chaos 63 % is now variation 'Chaos
63%'."`, `"Chaos could not be applied: <module message>"` (the value snaps back to its previous step), `"Nothing to freeze: Chaos is 0."`. Tab order: Chaos, Stutter, Reverse, Tape Stop,
Stutter rate, Tape stop time, Tape spin-up, Freeze Chaos, Re-roll, toggle.

## 4. Data model and state
**Host parameter (manifest version 3, appended after `fx_intensity`):** `{"chaos", "Chaos", "%", ParamKind::Float, 0.0, 1.0, 0.0, 0.0, 20.0, 3, nullptr}`, new `ParamIndex kChaos` (= 14
when merged after v2). The permanent identity is the string ID `chaos`; the enum position follows merge order with F07's `perf_mode`/`perf_velocity_curve` (README section 5).
`kManifestVersion` becomes 3 (a later feature of the same wave only appends). Default 0 means feature-off: chaos 0 returns the pattern unchanged, so old projects and automation-free
sessions behave exactly as before.

**Project state, StateSection `chaos` (schema 1, F01 API), written only when any field differs from default, so default projects contain no section:** `f64 depth` (0 to 1, default 1.0),
`bool affectEdited` (default false), `u32 rerolls` (default 0), `f64 tapeStopMs` (50..2000, default 400), `f64 spinUpMs` (20..1000, default 150), `u8 stutterRate` (0 = 1/8, 1 = 1/16
default, 2 = 1/32). Loader rejects out-of-range values with `ErrorCode::Corrupt` (all-or-nothing like the other modules). The Chaos amount itself lives in the APVTS parameter state.
```cpp
// modules/pattern_engine/include/chopfractal/pattern_engine/chaos.hpp
namespace chopfractal::pattern {
struct ChaosParams {
  double amount = 0.0;        // k in [0, 1], already curved and scaled by depth; <= 0 means identity
  std::uint64_t seed = 0;     // hashCombine(pattern.settings.seed, rerolls)
  bool affectEdited = false;  // include userOwned events/beats; locked scopes are never touched
  bool erase = false;         // true: thinned events are removed (Freeze); false: enabled = false (live overlay)
};
double chaosCurve(double c);   // clamp(c, 0, 1) ^ 1.5
}
// modules/audio_renderer/include/chopfractal/audio_renderer/gesture.hpp
namespace chopfractal::render {
enum class GestureKind : std::uint8_t { Stutter, Reverse, TapeStop };
struct GestureEvent { std::int32_t frame = 0; GestureKind kind = GestureKind::Stutter; bool on = false; std::uint8_t rate = 1; };  // rate only for Stutter on: 0=1/8 1=1/16 2=1/32
struct GestureParams { float tapeStopMs = 400.f; float spinUpMs = 150.f; };   // clamped 50..2000 / 20..1000
struct MidiCcState { bool stutter = false; std::uint8_t zone = 1; bool reverse = false; bool tape = false; };
// CC1 (mod wheel): on at >= 40, off at <= 24 (hysteresis); zone 40-63 -> 1/8, 64-95 -> 1/16, 96-127 -> 1/32 (a zone change while on emits another Stutter-on with the new rate).
// CC16: Reverse on at >= 64, off below. CC17: Tape Stop on at >= 64, off below. Other CCs: nullopt. Returns at most one event.
std::optional<GestureEvent> gestureFromCc(MidiCcState& state, std::uint8_t cc, std::uint8_t value, std::int32_t frame);
}
```
`render::SpscQueue<T, N>` (new header `audio_renderer/spsc_queue.hpp`, F08 creates it; F07 reuses): `static_assert(std::is_trivially_copyable_v<T> && N >= 2 && (N & (N - 1)) == 0)`;
fixed `T slots_[N]`; `alignas(64) std::atomic<std::uint32_t> head_, tail_`; `bool tryPush(const T&)` (producer only; false when full), `bool tryPop(T&)` (consumer only), `std::size_t
sizeApprox() const`. Never allocates, never blocks. The existing preview queue is left unchanged.

## 5. Public API
- `Result<Pattern> applyChaos(const Pattern& p, const ChopSnapshot& chops, const ChaosParams& cp)` (pattern_engine, pure, any thread). `amount <= 0` returns `p` unchanged (byte-equal
  `serialize`). Output always passes `pattern::validate` and `pattern::flatten` or the call returns the error; never exceeds `settings.maxEvents`.
- Renderer (`audio_renderer`, hot file): new overload `void Renderer::process(const TransportBlock&, const RenderParams&, const float* const* in, float* const* out, int numChannels, int
  frames, const LiveInput& live)` with `struct LiveInput { const GestureEvent* gestures = nullptr; int numGestures = 0; GestureParams gestureParams; /* F07 appends: const PadEvent*
  pads; int numPads; PadParams padParams; */ };` The existing 6-argument `process` stays and forwards `LiveInput{}`. `bool Renderer::gestureActive(GestureKind) const` mirrors state for
  the UI (relaxed atomics written once per block). Events must be sorted by `frame` in `[0, frames)`; the oversize-block chunking in `Renderer::process` re-bases their frames per chunk.
  Real-time rules: no allocation, locks or logging; all gesture state is preallocated in `Impl` (`tapeRateBuf`/`tapeGainBuf` sized `maxBlock`, one `FlatEvent lastEvent`).
- Composition (`composition/src/chaos.cpp`, declarations in `project_session.hpp` (hot)): `Status setChaos(double amount01)` (quantizes to steps of 0.01; if the step changed rebuilds
  the playback via `installPlayback`; on failure restores the previous step and returns the error), `double chaos() const`, `Status setChaosSettings(const ChaosSettings&)`, `const
  ChaosSettings& chaosSettings()`, `Status rerollChaos()`, `Status freezeChaos(const std::string& label = {})`. `ChaosSettings{double depth=1.0; bool affectEdited=false; std::uint32_t
  rerolls=0; double tapeStopMs=400, spinUpMs=150; std::uint8_t stutterRate=1;}`. `installPlayback(const Pattern&)` gains the overlay step (see 6); `evolveStep()` and `mutate()` use the
  biased amount. All on the message thread under `withSession`.
- Plugin: `void ChopFractalProcessor::setGesture(render::GestureKind, bool on, std::uint8_t rate = 1)` (any thread; pushes to `SpscQueue<GestureEvent, 64> gestureQueue_`), `void
  releaseAllGestures()`. `processBlock` drains the queue into a preallocated `std::array<GestureEvent, 64>` (frame 0), appends F07's MIDI-derived events when present, and calls the new
  `process` overload. `pollAudioFlags()` (30 Hz timer) reads `paramPtrs_[kChaos]`, quantizes to 0.01 and calls `withSession(setChaos)` only on change. The editor's `~GesturePanel` and
  `setVisible(false)` call `releaseAllGestures()`; `prepareToPlay`/`Renderer::reset()` clear gesture state so no gesture can stick.

## 6. Behavior details and edge cases
**Chaos curve and depth.** `c` = parameter quantized to 0.01 (0..100 steps). `k = chaosCurve(c) * depth` where `chaosCurve(c) = c^1.5` (so 50 % -> 0.3536, 25 % -> 0.125, 100 % -> 1).
Every effect below fires when its own draw `u < P(k)` with `P` linear in `k` and a magnitude that scales linearly with `k`, hence raising Chaos only ever adds changes (monotone) and
never reshuffles earlier ones.

**Determinism and seeding.** `seedC = hashCombine(cp.seed, 0x4348414F53ull)` ("CHAOS"), with `cp.seed = hashCombine(pattern.settings.seed, chaosSettings.rerolls)`. For an event and
stream `s`: `Rng r(deriveKey(seedC, event.id.value, s)); u = r.uniform01(); r1 = r.uniform01(); r2 = r.uniform01(); r3 = r.uniform01();` (first four draws in that order). Ghost slots
use `deriveKey(seedC, barScopeId.value, slotIndex, 12)`. Same pattern + seed + amount gives byte-identical output on every platform (integer RNG; the float math is plain `+ - *` and
`std::round`, tested within a platform; cross-platform bit-identity of the overlay is not required because nothing derived from it is stored except via Freeze).

**Mapping (events processed in bar, beat, array order; per event the steps run in the listed order; step 1 ends the pipeline for a thinned event).** An event is skipped entirely when it
is effectively locked (`phraseLocked`, bar, beat or event lock), when it or its beat is userOwned and `affectEdited` is false, or when it has an active child tree (`child &&
childActive`; the child plays instead).
| # | Stream | Setting it expresses | Gate | Probability P | Effect and magnitude |
|---|---|---|---|---|---|
| 1 | 1 | density down | none | `0.35 k` | `enabled = false` (Freeze: erase the event) and stop |
| 2 | 3 | variation (chop swap) | none | `0.50 k` | pool = other chops with the same non-zero `ChopInfo::group`; if empty, the previous and next chop in snapshot order (wrapping); new chop = `pool[floor(r1 * |pool|)]`; `region` cleared (whole chop) |
| 3 | 4 | micro timing | none | `0.25 k` | `start += round((2 r1 - 1) * (gridTicks(grid) / 8) * k)` clamped to `[0, barTicks - duration]` |
| 4 | 5 | accent | none | `0.30 k` | `level = clamp(level * (1 + (2 r1 - 1) * 0.5 * k), 0, kMaxLevel)` |
| 5 | 6 | reverse | `allowReverse` | `0.30 k` | `reverse = !reverse` |
| 6 | 7 | pitch | `allowPitch` | `0.40 k` | `pitch += round((2 r1 - 1) * pitchRange * k)`, clamp +-24 |
| 7 | 8 | retrigger | `allowRetrigger` | `0.30 k` | `nmax = min(maxRetrigger, 2 + floor(6 k))`; `retrigger = 2 + floor(r1 * (nmax - 1))` |
| 8 | 9 | filter | `allowFilter` and `!fx.active()` | `0.35 k` | `m = fxIntensity * k * (0.5 + 0.5 r2)`; type = LowPass if `r1 < 0.5` else HighPass; LowPass `cutoff = 1 - 0.85 m`, HighPass `cutoff = 0.7 m`; `resonance = clamp(0.6 m r3, 0, 1)` |
| 9 | 10 | glide | `allowGlide` and `!fx.active()` | `0.20 k` | `glideSemitones = (r1 < 0.5 ? -1 : 1) * 12 * fxIntensity * k * (0.5 + 0.5 r2)` |
| 10 | 11 | crunch | `allowCrunch` and `!fx.active()` | `0.20 k` | `crush = clamp(0.8 * fxIntensity * k * (0.5 + 0.5 r1), 0, 1)` |
Each fx step tests `!fx.active()` against the event as it is when the step runs, so at most one of filter, glide, crunch is added to an event.

**Ghost hits (density up):** after the
pass, for every grid slot (`gridTicks`, swing ignored) of every unlocked beat (not hand-placed unless `affectEdited`) that has no event starting inside it: if `u < 0.25 * k * (1
- settings.density)` add a new event (id from `nextId`, ascending bar/beat/slot order): chop = the chop of the nearest earlier event in the phrase (else the first chop), `level = 0.5`,
  `duration = min(gridTicks, barTicks - start)`, not userOwned, no effects. The total never exceeds `settings.maxEvents` (excess ghosts are skipped in order). Unaffected by Chaos:
  `settings`, bar/beat structure, locks, `Bar.copyOf`, nested children, probability, `userOwned` flags (unless `affectEdited`).

**Where it plugs in.** `ProjectSession::installPlayback(p)`: if `chaos_.k > 0`, `q = applyChaos(p, *chops_, params)` and the playback is built from `q`; `patterns_`, history, A/B slots,
saved state and `pattern()` always hold the un-chaosed pattern. Every existing publish path (finalize, republish, applyActivation, loadState, settle after undo) goes through
`installPlayback`, so Chaos follows all of them. WAV export renders `playback_` and therefore includes Chaos (what you hear); Producer Kit MIDI uses `pattern()` and does not (Freeze
first). Order with other features: `base pattern (F10 morph preview if active, else patterns_.current())` -> Chaos -> `flatten` -> `makePlayback`. Performance budget: `applyChaos` on
512 events under 1 ms; whole rebuild under 10 ms on the message thread (logged by a benchmark test, not asserted). Updates are coalesced by the 30 Hz poll and the 0.01 quantization (at
most 100 distinct rebuilds across the full range).

**Mutate/Evolve bias.** `amountEff = min(1.0, amount + 0.5 * k)` is applied to the local copy of `MutateOptions.amount` inside
`ProjectSession::mutate()` and to each `Step.amount` in `evolveStep()`, so a high Chaos makes Evolve change more per step. Generate ignores Chaos. With Chaos 0, `amountEff == amount`
exactly (no float drift: the add is skipped), keeping Evolve's documented replay property.

**Freeze.** `freezeChaos(label)`: if `chaos_.k <= 0` -> `ErrorCode::InvalidArgument` "Nothing
to freeze: Chaos is 0."; else `q = applyChaos(current, ..., erase = true)`, temporarily set `chaos_.k = 0`, `finalize(q, label.empty() ? "Chaos NN%" : label)` (history node + one undo
step), and the editor then sets the parameter to 0 with `setValueNotifyingHost`. If `finalize` fails the previous `k` is restored. **Re-roll** increments `rerolls`, rebuilds, and is not
an undo step (it changes only the overlay seed). **Gestures (audio thread, all sample-accurate at the event frame or at the block start for queued GUI events).** State lives in
`Renderer::Impl`: `stutterOn, stutterDiv, reverseOn, tapeOn, tapeRate (double, 1.0 idle), lastEvent/haveLast, nextStutterTick, idleFrames (int64)`. Reset by `Renderer::reset()`,
`hardKillAll` on source change, and when `params.enabled` is false.
- *Stutter.* `lastEvent` is a by-value copy of the last `FlatEvent` started from the pattern schedule. On Stutter-on at frame `f` (ignored if `!haveLast`): pattern triggers from the
  schedule are discarded while on (the groove freezes; `lastEvent` stops updating); the slice retriggers immediately at `f`, then on host-grid positions: `g = kTicksPerWhole /
  {8,16,32}` ticks (480, 240, 120); `tick(f) = playing ? pos0 + f*tpf : idleFrames_at_f * tpf`; next retrigger at `T = (floor(tick(f)/g) + 1) * g`, and if `T - tick(f) < g/2` then `T +=
  g`; then every `g`. Each retrigger releases the previous stutter voice (64-frame fade, `releaseVoice`) and starts `lastEvent` via `startFromEvent` with `duration =
  min(lastEvent.duration, g)` and unchanged level/transform/fx. Off: the current stutter voice rings out its gate; pattern scheduling resumes from the host position (no catch-up).
- *Reverse.* On: every active non-fading voice gets `step = -step` immediately (`pos` unchanged, so it plays back what it just played), and voices started while on use `tx.reverse =
  !tx.reverse`. Off: voices flipped by the gesture are flipped back; new voices are normal. A `Voice::flipped` flag makes the restore exact.
- *Tape Stop.* On: `tapeRate` falls linearly to 0 over `tapeStopMs` (`-1/(ms*sr/1000)` per frame); off: rises linearly to 1 over `spinUpMs`. Per-frame `tapeRateBuf[i]` multiplies the
  read increment (`pos += step * rateScale * tapeRate`) and `tapeGainBuf[i] = min(1, 4 * tapeRate)` multiplies the voice envelope so a stalled voice cannot output DC. When idle
  (`tapeRate == 1.0`, no ramp) the buffers are not refilled and the factors are exactly 1.0, so output is bit-identical to the pre-F08 renderer. The pattern keeps scheduling during a
  tape stop (new voices start at the current slow rate).
- Events whose state does not change (on while on) are ignored. Two simultaneous gestures combine: Stutter + Reverse retriggers reversed; Tape Stop slows everything including stutter
  voices. **CC mapper** is pure and unit-tested here; F07 calls `gestureFromCc` for `juce::MidiMessage::isController()` messages and adds the result to `LiveInput` at the message's
  sample position.

**Interactions.** Locks: locked scopes are bit-identical at Chaos 100 %. Undo/A-B/history are unaffected by Chaos. Evolve and Chaos run together (Chaos is applied to each new evolved
pattern). Scenes (F09) arm playbacks built by `installPlayback`/its builder, so Chaos applies to scenes. Offline render (`renderOffline`) takes no gestures. Host bypass (`effect_enable`
off) clears gesture state.

## 7. Test plan
**Unit, `modules/pattern_engine/tests/test_chaos.cpp`:** `chaos_curve_endpoints_and_midpoint` (0 -> 0, 1 -> 1, 0.5 -> 0.35355339 +-1e-9, input clamped);
`amount_zero_returns_the_pattern_byte_identical`; `chaos_is_deterministic_for_seed_and_differs_for_another_seed`; `effects_grow_monotonically_with_amount` (for k in {0.1..1.0}: set of
changed event ids at k1 is a subset at k2 > k1, per effect class: thinned, swapped, reversed, retriggered, filtered); `locked_scopes_and_hand_placed_events_are_untouched_by_default`
(lock bar 0, userOwned beat; k = 1; those events byte-equal; with `affectEdited` the userOwned ones may change, locked still not); `allow_flags_gate_effects` (all flags off: only
thin/ghost/swap/shift/accent can occur; no reverse/pitch/retrigger/fx anywhere); `fx_not_stacked_on_events_that_already_have_effects`; `ghosts_respect_density_event_cap_and_locks`
(density 1.0 -> zero ghosts; cap 16 -> total <= 16); `output_validates_and_flattens` (200 seeds x k in {0.25, 0.5, 1.0}); `erase_mode_removes_instead_of_disabling`;
`children_are_not_modified`. The four pattern_engine golden hashes must pass untouched (applyChaos is not called by generate/mutate). **Unit,
`modules/audio_renderer/tests/test_gestures.cpp`:** `no_gesture_output_is_bit_identical_to_the_six_argument_process` (FNV hash of a 4-cycle offline-style render with `LiveInput{}`
equals the hash recorded from `main` before this change; step 1 of the implementation commits that hash); `stutter_retriggers_the_last_event_on_the_grid` (single click-source event at
tick 0, 120 BPM, 48 kHz so 1 tick = 25 frames; engage 1/16 at frame 2000: onsets at 2000, 6000, 12000, 18000 +-1; an extra pattern event at tick 1080 produces no onset while held and
resumes after release); `stutter_ignored_without_a_previous_hit`; `stutter_rates_use_480_240_120_ticks`; `reverse_flips_sounding_and_new_voices_and_restores_on_release` (compare against
a reverse event render sample by sample for new voices); `tape_stop_ramps_to_zero_over_the_configured_time` (rate at frame `ms*sr/1000/2` is 0.5 +-0.01; after full time output magnitude
< 1e-4 and not a constant nonzero run; spin-up restores 1.0 within `spinUpMs`); `gestures_are_block_size_independent` (block sizes 64, 333, 512, 2048 give identical output with the same
event times); `oversize_host_blocks_rebase_gesture_frames`; `gesture_path_performs_no_heap_allocation` (extends the replaced-operator-new harness of
`process_performs_no_heap_allocation`); `cc_mapper_hysteresis_zones_and_unmapped_ccs`; `spsc_queue_preserves_order_and_never_loses_or_duplicates_across_threads` (producer/consumer, 1e6
items, run under TSan); `spsc_queue_reports_full_and_empty`.

**Host adapter, `test_host_adapter.cpp`:** golden manifest list gains `chaos` (id, range 0..1, default 0, step 0, smoothing
20, sinceVersion 3); `kManifestVersion == 3`; existing IDs and ranges unchanged.

**Integration, `composition/tests/test_chaos.cpp`:**
`chaos_overlay_changes_playback_but_not_the_stored_pattern` (pattern bytes identical; `playback()->events` differ at 0.8, equal at 0);
`chaos_zero_playback_is_identical_to_the_pre_feature_playback`; `mutate_and_evolve_use_the_biased_amount` (spy via equal seed: amount 0.2 + chaos 1.0 -> 0.7 produces the same pattern as
`mutate({seed, 0.7})`); `evolve_replay_is_unchanged_with_chaos_zero`; `freeze_creates_a_node_one_undo_step_and_resets_chaos`; `freeze_with_zero_chaos_is_refused`;
`reroll_changes_the_overlay_deterministically`; `failure_in_applychaos_keeps_the_previous_step` (inject a pattern at the event cap with all ghosts blocked: still valid; use a corrupt
chop snapshot to force the error); `chaos_section_roundtrip_range_checks_and_absent_for_defaults`; `export_wav_includes_chaos_and_kit_does_not`. **Plugin shell,
`plugin/tests/test_plugin_shell.cpp` / `test_gestures_gui.cpp`:** `chaos_parameter_changes_the_audio_after_a_poll` (setParam "chaos" 0.8, `pollAudioFlags()`, render 100 blocks: hash
differs from chaos 0 and is repeatable; back to 0: hash equals baseline); `gesture_queue_reaches_the_renderer` (`setGesture(Stutter,true)` before a block, block output differs from
baseline, `setGesture(...,false)` restores); `released_editor_clears_stuck_gestures`. GUI (xvfb, F00 recursive helpers): `gesture_buttons_are_momentary`: `b = findButton("Stutter");
b->setState(juce::Button::buttonDown); pump; CHECK(proc.gestureActive(Stutter)); b->setState(juce::Button::buttonNormal); pump; CHECK(!...)`; Shift-latch: simulate `mouseDown` with
shift, assert stays on after `buttonNormal`, plain click clears; `freeze_chaos_button`: set Chaos slider 0.5, pump 100 ms, click "Freeze Chaos", assert history node count +1 and slider
value 0 and StatusBar contains `"Frozen: Chaos 35 %"`; all new controls have titles.

**Real-time / sanitizer:** TSan on `test_gestures` (queue + process); ASan/UBSan on all; process()
allocation test passes; the hostile-state sweep covers the `chaos` section.

**Manual QA:** (1) Generate a 4-bar groove, play. (2) Raise Chaos slowly to 100 %: events thin and ghost
notes appear, effects appear only for the Allow toggles that are on. (3) Lock bar 1 (Lock Bar) and repeat: bar 1 never changes. (4) Return to 0: the original pattern, exactly. (5)
Automate Chaos in the DAW, bounce twice: identical audio. (6) Hold Stutter on the pad of your choice: the last hit repeats on 1/16, release: groove resumes in time. (7) Hold Tape Stop:
pitch falls and stops in 0.4 s, no DC thump; release: spins up in 0.15 s. (8) Freeze Chaos at 60 %, press Undo: previous pattern returns.

## 8. Acceptance criteria
- [ ] Chaos 0 leaves `serialize(pattern)`, flattened events and rendered audio bit-identical to before; the four golden hashes `2397844821793184813`, `12084884237330071892`,
      `13710596758976548913`, `18237151833431327128` are unchanged.
- [ ] Same pattern, seed, Chaos step, depth and re-roll count give identical overlays across runs; raising Chaos is monotone (test above).
- [ ] No event in a locked scope, and no hand-placed event (default setting), is modified at any Chaos value; Allow toggles are respected.
- [ ] Total events never exceed `maxEvents`; output always validates and flattens.
- [ ] `installPlayback` is the only place that applies the overlay; saved state, history and A/B never contain chaos.
- [ ] Manifest version 3 contains `chaos` exactly as specified; all version 1/2 IDs and ranges unchanged; a v2 project loads with `chaos` = 0.
- [ ] Stutter retriggers at 480/240/120-tick grid positions within +-1 frame, mutes scheduled hits while held, resumes after; Reverse and Tape Stop behave as specified; no gesture can
      stay stuck after editor close, `prepareToPlay`, bypass or `Renderer::reset()`.
- [ ] Tape Stop never outputs DC and fully recovers; output without gestures is bit-identical to the pre-F08 renderer; output is independent of host block size with gestures active.
- [ ] `process()` stays allocation- and lock-free with gestures (test); `SpscQueue` passes TSan.
- [ ] Freeze creates one family-tree node and one undo step and resets Chaos to 0.
- [ ] `chaos` section absent for default settings; present data round-trips; corrupt data is rejected without touching the live session.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/pattern_engine/include/.../chaos.hpp`, `src/chaos.cpp`, `tests/test_chaos.cpp` | New | overlay |
| `modules/audio_renderer/include/.../spsc_queue.hpp`, `gesture.hpp`, `src/gesture.cpp` | New | queue, CC mapper, event types |
| `modules/audio_renderer/include/.../renderer.hpp`, `src/renderer.cpp` | Mod **(hot)** | `LiveInput` overload, gesture state, tape buffers, stutter scheduling; forwarding 6-arg `process` |
| `modules/audio_renderer/tests/test_gestures.cpp` | New | |
| `modules/plugin_host_adapter/include/.../parameters.hpp`, `src/parameters.cpp` | Mod **(hot)** | `kChaos`, manifest row, `kManifestVersion = 3` |
| `modules/plugin_host_adapter/tests/test_host_adapter.cpp` | Mod | golden manifest |
| `composition/src/chaos.cpp` | New | session methods, section codec |
| `composition/include/.../project_session.hpp`, `composition/src/project_session.cpp` | Mod **(hot)** | declarations; overlay call in `installPlayback`; biased amount in `mutate`; clear in `reset` |
| `composition/src/features.cpp` | Mod | `evolveStep` uses `amountEff` |
| `plugin/src/PluginProcessor.h/.cpp` | Mod **(hot)** | `setGesture`, queue drain, `process` overload call, chaos poll |
| `plugin/src/panels/GesturePanel.h/.cpp`, `ControlsPanel.cpp` | New/Mod | gesture row, Chaos slider |
| `plugin/CMakeLists.txt`, module CMake/`module.json` files | Mod **(hot)** | new sources/tests, regenerate module docs |
| `plugin/tests/test_gestures_gui.cpp`, `test_plugin_shell.cpp` | New/Mod | |

## 10. Risks and mitigations
- **Renderer regression (bit-exactness).** Mitigate with the recorded pre-change hash test and idle-path factors of exactly 1.0; if any existing renderer test changes, stop and fix,
  never edit expectations.
- **Playback rebuild cost on every Chaos step.** 0.01 quantization + 30 Hz poll bound it to <= 100 rebuilds per sweep; measure in the benchmark test; fallback: poll at 15 Hz.
- **Stutter timing drift.** Idle clock uses integer frames; host-grid math uses the same epsilon as `schedule()`; covered by the block-size test.
- **Chaos surprises (taste).** README marks the mapping "decision needed": all constants are named constants in `chaos.cpp` and the table above is the single source of truth, so tuning
  is a one-file change plus updating this table; any change to the formulas needs a new test expectation but no format change.
- **Overlay hides the real pattern from the views.** The views show the stored pattern while the playback differs; StatusBar shows `"Chaos 35 %"` while active (F05 may add a badge).
  Freeze makes it visible.
- **Gesture stuck on host automation oddities.** All clear paths listed in 5; add an on-screen "All gestures off" fallback only if QA finds a case.

## 11. Implementation steps
1. Record the pre-change renderer hash test on unmodified code (commit alone). 2. `SpscQueue` + tests. 3. `applyChaos` + `chaosCurve` + unit tests (not yet wired). 4. Manifest v3
   `chaos` + host adapter tests. 5. Session integration (`installPlayback`, `setChaos`, bias, freeze, re-roll, `chaos` section) + composition tests. 6. Processor poll + ControlsPanel
   slider + shell test. 7. Renderer gesture engine, `LiveInput` overload, CC mapper + unit tests. 8. Processor gesture queue, GesturePanel, GUI tests. 9. Docs, module manifests, manual
   QA.
