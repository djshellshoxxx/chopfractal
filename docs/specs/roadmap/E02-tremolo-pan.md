# E02 Tremolo-Pan (per-event amplitude and pan motion)

**Status:** Ready for development  **Size:** S-M (4 engineer-days)  **Depends on:** E01 (defines the effect pass module, the `fx` extras section tag 17 and the event extension rules). Soft: F01, F04, F00  **Blocks:** none
**Family:** FX14 "Tremolo / auto-pan" of `docs/specs/advanced-section-effects.md`, per-event model of `docs/specs/effects-filter-glide-crunch.md`.

## 1. Summary and user value
Some sustained chops get a tempo-synced tremolo (amplitude wobble) and/or an auto-pan sweep over the life of the hit. It adds motion to long slices and pads without new samples. Chosen by the same seeded effect pass as E01, stored per event (flag bit 128), rendered inside the voice with a phase-locked LFO (no allocation, no delay line). Off by default; when off, nothing changes anywhere.

## 2. User stories and scope
- Producer: enable **Tremolo-Pan**, set Amount, and long hits wobble or sweep in time with the song.
- Producer: choose **Mode** (Tremolo, Pan, Both), **Rate** (1/16, 1/8, 1/4, 1/2 per cycle of the LFO) and **Depth**.
- Producer: force Tremolo-Pan on or off on a single hit.
**In scope:** `TremPanFx` event block with bit 128, the pass, voice LFO, parameters, UI, state, tests. **Non-goals:** free-running Hz rates, LFO shapes beyond sine and triangle (shape is fixed sine for tremolo, triangle for pan), modulating other parameters, stereo width processing, effects on the dry mix.

## 3. UX
**Controls:** `ToggleButton` "Trem-Pan" (title `Allow tremolo pan`, tooltip `Let some long hits wobble in level or sweep across the stereo field.`, `allow_tremolo`), `TextButton` "Trem-Pan..." opening a 260 x 200 call-out with: slider "Amount" (title `Tremolo pan amount`, 0..100 %, default 30, `trem_amount`), combo "Mode" (title `Tremolo pan mode`: `Tremolo`, `Pan`, `Both`; default `Both`; `trem_mode`), combo "Rate" (title `Tremolo pan rate`: `1/16`, `1/8`, `1/4`, `1/2`; default `1/8`; `trem_rate`), slider "Depth" (title `Tremolo pan depth`, 0..100 %, default 60, `trem_depth`). Row E cost 140 px (shares the row with E01's 140 px plus F16's 260 px: 540 of 920 px; F06/F07 controls take row A). Controls disabled and dimmed when the toggle is off.
**Grid glyph:** `t` at the cell's lower right (`Trem-Pan: both, 1/8, depth 60 %` in the tooltip). **Hit editor:** `ComboBox` "Trem-Pan" with `Auto`, `On`, `Off` (title `Hit tremolo pan`).
**Messages:** `Trem-Pan: 5 of 24 hits.` (Info after Generate); `Trem-Pan skipped on short hits.` is not shown (silent by design; the count is in the tooltip of the toggle: `5 hits use it, 9 were too short`).
**Accessibility:** focus order, titles, text values, glyph plus tooltip (not colour only).

## 4. Data model and state
### 4.1 Event block (flag bit 128, `kEventHasTremPan`, after the E01 block)
3 bytes: `u8 mode` (0 Tremolo, 1 Pan, 2 Both, 255 = global), `u8 rateIndex` (0..3, 255 = global), `u8 depth` (0..100, 255 = global; bit 7 of a value in 128..228 marks user-forced as in E01: forced value = 128 + depth, 255 stays "global, not forced"; a forced-off event is `mode == 254`). In memory `struct TremPanFx { uint8_t mode=255, rateIndex=255, depth=255; }`, `std::optional<TremPanFx> tremPan`. Reads outside the ranges are sanitized (mode > 2 and not 254/255 -> 255).
Flags byte is now full (bits 1..32, 64 and 128 used). **Extension rule:** the next per-event feature must bump pattern schema to 2 and add an extension flags byte signalled by a reserved bit combination; this spec reserves nothing further. The renderer and pattern code assert on `static_assert(kAllFlagBits == 0xFF - kReserved)` style checks in `event.cpp` so an accidental ninth bit fails to compile.
### 4.2 Settings
Project extras tag 17 `fx` (introduced by E01): E02 uses bit1 of the flags byte for `allowTremolo` and the reserved bytes: `u8 tremAmount, tremMode, tremRate, tremDepth` placed in the 3 reserved bytes plus one appended byte (section length becomes 9 for writers that store E02 fields; readers accept 8 or 9; schema stays 1). Default values mean the section is not written.
### 4.3 Host parameters (manifest v3, appended after E01's rows if merged earlier, else after the last row present; ids permanent)
```
{"allow_tremolo", "Allow Tremolo Pan",  "",  ParamKind::Bool,   0,   1,  0, 1, 0,  3, nullptr}
{"trem_amount",   "Tremolo Pan Amount", "%", ParamKind::Float,  0, 100, 30, 0, 10, 3, nullptr}
{"trem_mode",     "Tremolo Pan Mode",   "",  ParamKind::Choice, 0,   2,  2, 1, 0,  3, "Tremolo|Pan|Both"}
{"trem_rate",     "Tremolo Pan Rate",   "",  ParamKind::Choice, 0,   3,  1, 1, 0,  3, "1/16|1/8|1/4|1/2"}
{"trem_depth",    "Tremolo Pan Depth",  "%", ParamKind::Float,  0, 100, 60, 0, 10, 3, nullptr}
```
### 4.4 Seeding
Stream `splitmix64(patternSeed ^ 0x7E40'0000'0000'0002 ^ eventIndex * 0x9E3779B97F4A7C15)`; draws: choose, then mode when the global mode is `Both`... **Decision:** the global Mode is used for every selected event (no per-event random mode), keeping the pass to one draw.

## 5. Public API
`FxPassSettings` (E01) gains `bool allowTremolo=false; uint8_t tremAmount=30, tremMode=2, tremRate=1, tremDepth=60;` and `FxPassReport` gains `tremmed`, `tremShort`. `applyEffectPass` handles both effects; it is not called when neither allow flag is set. Renderer `RenderParams` gains `tremModeG, tremRateG, tremDepthG` fallbacks. The voice gets `lfoPhase` initialised from the event start.

## 6. Behavior details and edge cases
**Choice.** Eligible: events whose playable length is at least one LFO period at the chosen rate (1/16 period is 240 ticks, 1/2 is 1920) and role not kick. Selected if `u01(draw) < amount/100 * w` with `w = 1.0` for hits >= 2 periods, `0.6` otherwise. Shorter hits count in `tremShort`.
**DSP (inside `renderVoice`, after echo tone filter, before envelope/gain).** Period `P = rateTicks` converted to frames from the tempo at schedule time. Phase `phi = 2*pi*(n / P)` starting at the hit onset (phase-locked to the hit, so every hit starts the same way: tremolo starts at full level, pan starts at centre moving right on even event indices and left on odd indices, deterministic by index). Tremolo gain `gT = 1 - d * (0.5 - 0.5*cos(phi))` with `d = depth/100 * amount-independent`; the amount only decides selection, depth sets the intensity (a stated simplification). Pan position `p = d * tri(phi)` where `tri` is a triangle in [-1, 1]; equal-power law `gL = cos((p+1)*pi/4)`, `gR = sin((p+1)*pi/4)` normalized so `p = 0` gives 0.7071 both, then multiplied by 1.4142 so the centre keeps unity (the existing voice pan law is replaced for these voices only by `existing pan + p` clamped to [-1, 1]). All math in `float` with a precomputed table of 1024 entries for sine and triangle (built in `prepare`, no allocation in `process`). Tail of a hit past its note-off is modulated until the voice ends.
**Interaction with E01.** Echo repeats copy the primary's block, with phase restarting at each repeat. Both coexist in one event (bits 64 and 128) in that serialization order.
**Mono hosts.** Pan has no audible effect in mono (documented); tremolo does.
**History/lock/hand edit.** As E01 (pass idempotent, forced events preserved, locks respected, undo restores bytes).
**Off:** allow off means stored blocks are ignored at render; no pass is run; no new RNG draw anywhere in generation; golden hashes and renderer regression hash unchanged.
**Bounds.** `depth` clamps 0..100; unknown `mode` falls back to global; NaN impossible (integers only).

## 7. Test plan
Unit pattern: `pass_not_run_when_off_bytes_unchanged`; golden hashes unchanged; `same_seed_same_selection`; `short_hits_never_selected_and_counted`; `kick_never_selected`; `idempotent_under_settings_changes`; `event_roundtrip_with_echo_and_trempan_together` (bits 64+128, byte equality; one without either is byte-equal to the old format); `sanitizing_of_out_of_range_blocks`; `ninth_flag_bit_does_not_compile` (compile-fail test via CMake `try_compile`).
Renderer: `tremolo_gain_matches_formula_at_sample_points` (1e-4); `pan_is_equal_power_and_centre_unity` (constant L^2+R^2 within 1e-3 across the sweep); `phase_starts_at_onset_for_every_hit`; `pan_direction_alternates_with_event_index`; `mono_downmix_keeps_tremolo_and_loses_pan_motion`; `identical_across_block_sizes_1_to_2048`; `no_allocation_in_process` (harness); `off_is_bit_identical` (regression hash).
Composition/host: `manifest_rows_appended_in_order_and_old_ids_unchanged`; `extras_tag17_reads_8_and_9_byte_forms`; `undo_redo_restores_blocks`. GUI: `callout_controls_bound_and_disabled_when_off`, `glyph_and_tooltip`, `hit_editor_combo_modes`, `titles_and_tooltips_present`.
Manual QA: a long vocal chop with Both at 1/8 sweeps in time; toggling mid-playback is click-free; Pan in a mono host is silent as documented.

## 8. Acceptance criteria
- [ ] Off: golden hashes, renderer hash and saved state of feature-free projects are byte-identical.
- [ ] Selection deterministic; hits shorter than one LFO period never selected; locks and forced modes respected; pass idempotent.
- [ ] Tremolo and pan match the formulas within tolerance; pan keeps constant power; phase locked to each hit start.
- [ ] Bit 128 round-trips alone and with bit 64; a ninth flag bit fails the build check; extension plan documented in `event.hpp`.
- [ ] Five parameters appended with the exact ids of 4.3, none existing changed; no audio-thread allocation or lock; identical output for any block size; TSan clean.
- [ ] UI strings, titles and tooltips match; docs, registries (tag 17, bit 128, five rows) updated.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/chop_contracts/.../event.hpp`, `src/event.cpp`, tests | Mod | `TremPanFx`, bit 128, flag-space check |
| `modules/pattern_engine/src/fx_pass.cpp`, `fx_pass.hpp`, `tests/test_fx_pass_trempan.cpp` | Mod/New | selection |
| `modules/audio_renderer/src/renderer.cpp`, `renderer.hpp`, `tests/test_trempan.cpp` | Mod **(hot)** | LFO tables, voice processing |
| `modules/plugin_host_adapter/src/parameters.cpp`, `.hpp`, tests | Mod **(hot)** | five rows |
| `composition/src/fx_settings.cpp`, `state_sections.cpp`, `project_session.hpp` | Mod **(hot)** | settings, tag 17 |
| `plugin/src/TremPanCallout.h/.cpp`, `PluginProcessor.cpp`, `PatternPanel` | New/Mod **(hot)** | UI |
| `docs/BUILD_STATUS.md`, roadmap README | Mod | registries |

## 10. Risks and mitigations
- **Flags byte exhausted.** Mitigation: compile-time check and written extension plan.
- **Pan law change for affected voices causes level jumps.** Mitigation: centre unity normalization and a test.
- **Selection overlap with E01 reduces both.** Independent streams, independent selection; overlap is allowed and tested.
- **Tempo change mid-hit changes the period.** Period fixed at schedule time; documented.
- **Perceived little effect at low depth.** Depth default 60 and the QA clip check audibility.

## 11. Implementation steps
1. Event block and flag-space check (after E01 step 1). 2. Pass extension and tests. 3. Voice LFO and tests. 4. Manifest rows and plumbing. 5. Tag 17 extension, undo. 6. UI. 7. Docs and registries, QA.
