# E01 Echo (per-event tempo-synced repeat)

**Status:** Implemented (renderer, host parameters, state, Sound drawer rows, tests)  **Size:** M (5 engineer-days)  **Depends on:** none hard. Soft: F01 (state section), F04 (focus order), F00 (row E placement)  **Blocks:** none. Conflicts to sequence: E02 (same event block, same `Allow` trailer, same renderer stage; E01 lands first and defines the extension layout)
**Family:** FX01 "Echo" of `docs/specs/advanced-section-effects.md`, built on the per-event model of `docs/specs/effects-filter-glide-crunch.md`.

## 1. Summary and user value
Some chops get a tempo-synced echo tail: 1 to 5 quieter, optionally filtered repeats of the chop that land on the grid (1/16, 1/8, dotted 1/8, 1/4), decaying by a feedback amount. It is decided per event by the generator-independent **effect pass** (so the base pattern and its golden hashes never change), stored in the event, played by the renderer as extra prepared events (cheap, no delay line, no feedback loop, no allocation), and exported to MIDI as nothing (the repeats are audio-only, noted in the export text). Musical use: dub-style throw on the last hit of a bar, ghost repeats on snares, tails that make sparse patterns feel full. Off by default.

## 2. User stories and scope
- Producer: turn on **Echo**, set Amount, and some snares and the last hit of each bar throw tempo-locked repeats.
- Producer: pick the repeat spacing (**Rate**) and how fast it dies (**Decay**), and darken the repeats (**Tone**).
- Producer: lock a bar (F09/F06 lock) so its echoes never change; hear the same result every time (seeded).
- Producer: hand-edit one hit and force Echo on or off for that hit only.
**In scope:** the per-event fields and flag bit 64, the effect pass `fxpass::applyEcho`, renderer expansion, host parameters, the pattern trailing block for the settings, UI, state, tests. **Non-goals:** a true feedback delay with ping-pong or modulation, an echo that rings past the pattern loop end (tails are cut at the cycle end, as retriggers are today), per-repeat pitch (E01 never pitches repeats), MIDI export of repeats, reverb.

## 3. UX
**Controls (FeaturePanel row E):** `ToggleButton` "Echo" (title `Allow echo`, tooltip `Let some hits throw tempo-synced repeats.`, parameter `allow_echo`), `Slider` rotary "Echo amt" (title `Echo amount`, 0 to 100 %, default 40, `echo_amount`, tooltip `How many hits get echoes.`), `ComboBox` "Echo rate" (title `Echo rate`: `1/16`, `1/8`, `Dotted 1/8`, `1/4`; default `1/8`; `echo_rate`), `Slider` rotary "Decay" (title `Echo decay`, 10 to 90 %, default 55, `echo_decay`; each repeat is this fraction of the previous level), `Slider` rotary "Tone" (title `Echo tone`, 0 to 100 %, default 50, `echo_tone`; 0 is no filtering, 100 darkens each repeat progressively). Row E budget: controls live in a call-out opened by one `TextButton` "Echo..." (80 px) next to the toggle, so row E spends 140 px; the call-out is 260 x 220. Controls in the call-out show their text values (`40 %`, `1/8`). When `allow_echo` is off the call-out controls are disabled and dimmed (still readable).
**Pattern grid:** events carrying echo show a small `e` glyph at the cell's lower right (and in the tooltip: `Echo: 3 repeats, 1/8, decay 55 %`); the glyph is not colour-only.
**Hit editor (direct hit edit):** the popup gains a three-state `ComboBox` "Echo" with `Auto`, `On`, `Off` (title `Hit echo`). `Auto` follows the generator pass, `On` forces 3 repeats at the global rate, `Off` suppresses. Stored in the event (bit 64 set + `echoMode` field, section 4).
**Messages:** `Echo: 6 of 24 hits.` (Info after Generate when the allow flag is on); `Echo needs room: 2 hits were skipped because the next hit is too close.` (Info, when `skipped` > 0).
**Accessibility/keys:** toggle and button in the focus order; call-out controls in reading order; text values announced.

## 4. Data model and state
### 4.1 Event layout
Flag bit 64 (`kEventHasEcho`) in the existing flags byte means "an echo block follows after the fx block" (blocks appear in ascending flag-bit order; bits 1 to 32 keep their current blocks). Block (3 bytes): `u8 repeats` (0..5; 0 is the explicit-off marker used by mode `Off`), `u8 rateIndex` (0..3 as the combo; 255 = use the global rate), `u8 level` (0..100, the per-event Decay override in percent; 255 = use global). In memory: `struct EchoFx { std::uint8_t repeats = 0, rateIndex = 255, level = 255; }` in `chop_contracts/event.hpp`, `std::optional<EchoFx> echo` in `Event`. An event without the optional serializes exactly as before (golden hashes and byte-compatibility unchanged; pattern hashing in `generate` never reads `echo`). A pattern file with bit 64 set is rejected by older builds as "unknown flag bits" (existing behavior), which is the desired failure mode.
### 4.2 Pattern trailing settings (Allow flags)
`PatternSettings` gains `bool allowEcho = false;` plus the global echo shape, which live in the **project extras** section `fx` (F01 StateSection tag 17, schema 1, 8 bytes: `u8 flags (bit0 allowEcho, bit1 allowTremolo reserved for E02)`, `u8 echoAmount`, `u8 echoRate`, `u8 echoDecay`, `u8 echoTone`, 3 bytes reserved zero). Tag 17 is shared with E02, which adds fields at the reserved bytes; schema stays 1 because reserved bytes read as zero. Defaults make the section absent. The **host parameters** below mirror the same values; parameter state wins on load order (the section seeds parameters when the host has no saved values, as other extras do).
### 4.3 Host parameters (manifest v3, appended after the last existing entry; versions stay 3)
```
{"allow_echo",   "Allow Echo",  "",  ParamKind::Bool,   0,   1,  0,  1, 0,  3, nullptr}
{"echo_amount",  "Echo Amount", "%", ParamKind::Float,  0, 100, 40,  0, 10, 3, nullptr}
{"echo_rate",    "Echo Rate",   "",  ParamKind::Choice, 0,   3,  1,  1, 0,  3, "1/16|1/8|Dotted 1/8|1/4"}
{"echo_decay",   "Echo Decay",  "%", ParamKind::Float, 10,  90, 55,  0, 10, 3, nullptr}
{"echo_tone",    "Echo Tone",   "%", ParamKind::Float,  0, 100, 50,  0, 10, 3, nullptr}
```
Append rule (roadmap README, Manifest policy): ids are permanent, new rows go after the last row present at merge time, never reordered, enum entries `kAllowEcho` and so on added in the same order. Row ids never get reused.
### 4.4 Seeding
The effect pass derives a per-event stream: `rng = splitmix64(patternSeed ^ 0xEC40'0000'0000'0001 ^ eventIndex * 0x9E3779B97F4A7C15)`. It is a **separate** stream from generation, so no draw is added to the generator. Decision uses draws in fixed order (1: choose, 2: repeat count 1..5 weighted toward 2..3).

## 5. Public API
`modules/pattern_engine` (portable, hot-free): new `fx_pass.hpp/.cpp`:
```cpp
struct FxPassSettings { bool allowEcho=false; std::uint8_t echoAmount=40, echoRate=1, echoDecay=55, echoTone=50; /* E02 appends fields */ };
struct FxPassReport { int echoed=0, echoSkipped=0; };
FxPassReport applyEffectPass(Pattern& p, const FxPassSettings&, std::uint64_t seed, const LockMask* locks);
```
Called by `ProjectSession` after `generate/mutate` and after history restore **only when any allow flag is set** (when none is set the function is not called, the pattern bytes are identical to the pre-feature ones). Idempotent: the pass first clears every auto-created `echo` (events whose `echoMode` is Auto; user-forced ones are marked by `rateIndex`... see 6) and recomputes, so Amount changes do not accumulate. Locked events (lock mask) are skipped and keep their stored echo.
Renderer (`audio_renderer`): `RenderParams` gains `echoRateIndex`, `echoDecay`, `echoTone` global fallbacks (defaults 1, 55, 50). Composition: `ProjectSession::setFxSettings(const FxPassSettings&)` triggers re-pass + undo entry `Echo settings`.

## 6. Behavior details and edge cases
**Choice (pass).** Candidates: hits with role snare/rim/clap/perc with velocity >= 0.5, plus the last non-rest hit of each bar. Each candidate echoes if `u01(draw1) < echoAmount/100 * weight` with weight 1.0 for last-of-bar, 0.7 for snare class, 0.4 for others. `repeats = 1 + floor(u01(draw2)^1.5 * 4.0)` (1..5, mostly 1 to 3). Kick-class hits and hits shorter than 1/32 never echo (kick-class excluded to keep low end clean).
**Room rule.** The repeat spacing in ticks is `S = {240, 480, 720, 960}[rate]` (960 ticks per quarter). Repeat k lands at `onset + k*S`. The pass trims `repeats` so the last repeat lands at least 120 ticks before the next onset and before the loop end; if none fits the event is skipped and counted in `echoSkipped`. At render time, repeats are additionally limited by the free capacity of the 4096 prepared-event buffer and the 8 voices (a repeat is dropped, never stolen from a live primary hit; primary hits keep priority in existing voice stealing; echoes carry a "secondary" priority bit in the prepared event and are stolen first).
**Playback.** Renderer expansion (same stage as retrigger expansion, after it): repeat k is a copy of the primary prepared event with `gain *= decay^k * velocityTrack`, start shifted by `k*S` frames computed from `bpm`, **no pitch change**, `reverse` inherited, and if `tone > 0` a one-pole low-pass is applied with cutoff `fc_k = 12000 * (1 - tone/100*0.85)^k` Hz (clamped to >= 400 Hz; coefficient computed once at expansion and stored in the prepared event, the filter state lives in the voice). Order inside the fixed transform chain: chop slice -> reverse -> pitch/glide -> filter (existing) -> crunch -> **echo tone filter** -> envelope -> gain. Swing/groove shift (F17) is applied to the primary onset before expansion, so repeats follow the moved hit.
**Cut rule.** A repeat past the cycle end is not scheduled (no wrap). With a looped playhead the pattern repeats at its own length, so echoes of the last hit never overlap the first hit of the next loop by design.
**Mutation/history.** `mutate` produces a pattern whose events carry the previous echo fields for retained events; the session re-runs the pass (idempotent), so mutate keeps stable echoes for unchanged hits as the seed stream is keyed by the event's index and the pattern seed. Undo restores the stored pattern bytes including echo fields; redo likewise.
**Hand edit.** Mode `On` writes `repeats = 3, rateIndex = 255`; `Off` writes `repeats = 0`; `Auto` removes the block. The pass skips events whose block exists with a user-forced flag: forced events are `repeats == 0` or the block's top bit of `level` set (bit 7 of the level byte means user forced; level range is therefore 0..100 plus 128 = forced). This keeps the block at 3 bytes.
**Bounds/sanitizing.** `repeats` > 5 is clamped on read and the event is accepted with a warning in the load report (not rejected); `rateIndex` > 3 and not 255 reads as 255; `level` 101..127 reads as 100.
**MIDI export.** Events are exported once (no repeats); the export report appends `Echo repeats are audio-only and are not in the MIDI file.` when any event has echo.
**Determinism.** Same pattern seed, settings, locks and tempo give the same echo set and the same render bits. Off (`allowEcho == false`, no forced events): the pass is not run, no `echo` fields exist, the renderer expansion loop has nothing to expand, and the four golden hashes and the renderer regression hash are unchanged. A pattern carrying an echo block plays echoes even when `allow_echo` is off? **Decision:** no. Playback respects the allow flag: with `allow_echo` off, echo blocks are ignored at render (so the toggle is a true bypass and automation of it is instant), but they are preserved in the pattern bytes.

## 7. Test plan
Unit (`modules/pattern_engine/tests/test_fx_pass_echo.cpp`): `pass_not_run_means_pattern_bytes_unchanged`; `golden_hashes_unchanged_with_allow_echo_off` (4 hashes); `same_seed_same_echo_set`; `amount_zero_echoes_nothing_and_amount_100_echoes_every_candidate_that_fits`; `kick_class_never_echoes`; `room_rule_trims_repeats_and_counts_skips`; `locked_events_keep_their_echo`; `pass_is_idempotent_when_settings_change_up_and_down` (echo set at amount 40 -> 80 -> 40 equals the first); `forced_on_and_off_survive_the_pass`; `event_roundtrip_with_and_without_echo_block` (byte equality for events without it; bounds sanitizing cases in 6); `old_pattern_file_loads_unchanged`; `unknown_flag_128_without_e02_is_rejected_cleanly` while 64 parses.
Renderer (`modules/audio_renderer/tests/test_echo.cpp`): `repeat_k_lands_at_k_times_spacing_frames` for each rate at 100, 120, 174.5 BPM within 1 frame; `levels_follow_decay_power`; `tone_darkens_each_repeat_monotonically` (spectral centroid decreases); `echo_expansion_does_not_exceed_4096_and_never_steals_primary`; `render_is_identical_across_block_sizes`; `allow_off_ignores_stored_blocks_bit_exactly`; `process_allocates_nothing_with_echo_active` (replaced `operator new` harness); `silent_when_tail_would_cross_cycle_end`.
Composition/host: `manifest_has_five_echo_rows_after_previous_last_and_old_ids_unchanged`; `extras_fx_section_roundtrip_and_absent_when_default` (F01); `undo_restores_echo_fields`; `mute_and_solo_apply_to_repeats`.
GUI (xvfb): `echo_button_opens_callout_and_controls_are_bound`; `controls_disabled_when_allow_off`; `grid_glyph_and_tooltip_for_echoed_event`; `hit_editor_echo_combo_writes_modes`; `all_new_controls_have_titles_and_tooltips`.
Sanitizers: ASan/UBSan all; TSan for parameter reads while rendering. Manual QA: drum loop, snares echoing in time with a metronome at 1/8; toggling Allow mid-playback has no click.

## 8. Acceptance criteria
- [ ] With Allow Echo off, golden hashes (2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128) and the renderer regression hash are bit-identical; files saved by older builds load unchanged and a feature-free project saves byte-identically.
- [ ] Same seed and settings give the same echoed event set; the pass is idempotent and honors locks and forced hit modes.
- [ ] Every repeat lands on the grid (within 1 frame), no repeat crosses the next onset (minus 120 ticks) or the cycle end, decay follows `decay^k`.
- [ ] Renderer: no allocation or lock, identical output for block sizes 1 to 2048, primary hits never stolen by echoes, 4096 cap respected.
- [ ] Five parameters appended with the exact ids of 4.3; no existing id or index changes; automation of `allow_echo` is click-free.
- [ ] Pattern flag bit 64 only; E02 bit 128 coexist in one event and round-trip.
- [ ] MIDI export unchanged and the report line is shown; all strings in section 3 match the tests; controls have titles and tooltips; docs and module README updated.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/chop_contracts/include/.../event.hpp`, `src/event.cpp`, tests | Mod | `EchoFx`, bit 64, serialize/parse, bounds |
| `modules/pattern_engine/include/.../fx_pass.hpp`, `src/fx_pass.cpp`, `tests/test_fx_pass_echo.cpp`, `CMakeLists.txt` | New/Mod | effect pass |
| `modules/audio_renderer/src/renderer.cpp`, `include/.../renderer.hpp`, `tests/test_echo.cpp` | Mod **(hot)** | expansion, tone filter, priority bit |
| `modules/plugin_host_adapter/src/parameters.cpp`, `parameters.hpp`, tests | Mod **(hot)** | five rows |
| `modules/midi_export/src/*` | Mod | report line |
| `composition/include/.../project_session.hpp`, `src/project_session.cpp`, `src/fx_settings.cpp` | Mod/New **(hot)** | settings, pass calls, undo |
| `composition/src/state_sections.cpp` | Mod | tag 17 `fx` |
| `plugin/src/PluginProcessor.cpp`, `PluginEditor.cpp` (or F00 panels), `EchoCallout.h/.cpp`, `PatternPanel` glyph | Mod/New **(hot)** | plumbing and UI |
| docs: `BUILD_STATUS.md`, roadmap README registries | Mod | registries |

## 10. Risks and mitigations
- **Prepared-event pressure at high density plus retrigger plus echo.** Mitigation: secondary priority, hard cap, test; room rule trims.
- **Bit 64/128 exhaust the flags byte.** Mitigation: documented extension byte plan (pattern schema 2); a third per-event block is gated on that.
- **Echo with tempo changes mid-play.** Spacing is computed at expansion from the tempo at schedule time; a tempo change re-expands on the next cycle (documented); test with automation.
- **Idempotent pass vs. user expectation of stable echoes when other settings change.** Keyed per-event streams keep unchanged events stable.
- **Pattern files with bit 64 opened by older builds fail.** Accepted: clear error; the manual says so.

## 11. Implementation steps
1. Event field, bit 64, serialization and tests (golden hashes must pass). 2. Pass module with tests. 3. Renderer expansion and tone filter, tests. 4. Manifest rows, processor plumbing. 5. Session settings, extras tag 17, undo. 6. UI call-out, grid glyph, hit editor combo. 7. MIDI report line, docs, registries, QA.


## Implementation notes (as built, RND)

- The effect pass is `pattern::applyFxPass(const Pattern&, const FxPassSettings&)` in the new `modules/pattern_engine/src/fx_pass.cpp` (header `fx_pass.hpp`); it only writes the optional per-event blocks, so generation output and the golden hashes are unchanged. Settings: per-effect amount 0..1 (host `allow_x` off means 0), a seed combined with the pattern seed; user-forced blocks (Echo level or Tremolo-Pan depth 128 and above, Tremolo-Pan mode 254) and locked or user-owned events are left alone; re-running replaces the pass's blocks and an all-zero setting clears them. `ProjectSession::setFx(pass, options)` re-runs it as one undo entry and republishes; generation, mutation and edits keep applying the stored settings.
- Global fallbacks are `render::PlaybackOptions` (echo rate, decay, tone; trem mode, rate, depth) passed to `makePlayback`, not `RenderParams`; repeats are expanded into the playback up front, so the audio thread does no work for them. `RenderParams::fxMask` (`fxbit::kEcho`, `kTremPan`, `kStutter`, `kRoll`) bypasses an effect at run time (the host `allow_*` parameters); the replaced voices play instead.
- Echo repeats are tone-filtered with a one-pole low-pass per repeat, never wrap past the loop end and never steal a primary voice. No state section (tag 17) is built; the parameters carry the state. The UI call-out is not built.
