# F17 Groove Lab

**Status:** Ready for development  **Size:** M-L (8 engineer-days)  **Depends on:** F01 (extras tag 14, parameters mirrored), F00 soft (row E / call-out host). Soft: F02 (hit-edit coexistence), F18 (renders with the groove), F16 (accent curve is part of the level-match key)  **Blocks:** none
**Category:** musicality. Playback-time layer: it changes where and how loud already-generated events play, never what the generator produces.

## 1. Summary and user value
Quantized chops sound stiff. Groove Lab applies a **groove template** at playback and export: MPC-style 16th-note swing between 50 % and 75 %, a shuffle/triplet feel, per-role push/pull micro-timing (hats ahead, snares behind), a seeded deterministic **humanize** jitter and an **accent curve** that shapes velocity across the bar. It sits between the pattern and the renderer as a pure function `grooveShift(event, bar position) -> (tickOffset, velocityScale)`, so the stored pattern, the four golden pattern hashes and all history stay untouched; with Groove off the playback path is bit-identical to today. Producers get an MPC or Dilla-flavoured feel without leaving the plugin, and the feel is shared by playback, WAV export, MIDI drag-out (F13) and arrangement export (F18).

## 2. User stories and scope
- Producer: pick a template (`Straight`, `MPC 54`, `MPC 58`, `MPC 62`, `MPC 66`, `MPC 71`, `Shuffle`, `Dilla`, `Custom`) and hear the loop swing immediately.
- Producer: dial **Swing** continuously between 50 % and 75 %.
- Producer: set **Push/pull** per role in milliseconds-equivalent ticks (hats -10 ticks ahead, snares +14 behind) to taste.
- Producer: add **Humanize** (timing and velocity jitter) that is the same every time for a given seed.
- Producer: draw an **Accent curve** over 16 steps to shape velocity (e.g. emphasize beats 2 and 4).
- Producer: export MIDI/WAV with the groove baked in, or MIDI with the groove off (choice).
**In scope:** the portable `groove` module (templates, shift function, jitter), integration in the playback scheduler, MIDI export option, host parameters, state section, UI, tests. **Non-goals:** extracting a groove from audio (MPC "groove extract"), groove import/export (`.agr`), per-hit hand-timing (F02 hit edits keep their own tick positions and are shifted on top), swing on non-16th grids other than the 8th variant described in 6, time-signature other than the pattern's, modifying stored events.

## 3. UX
**Controls (call-out "Groove..." in FeaturePanel row E, 80 px; the call-out is 360 x 300):**
- `ComboBox` "Template" (title `Groove template`; items listed above; default `Straight`; parameter `groove_template`).
- `Slider` "Swing" (title `Groove swing`, 50 to 75 %, step 0.5, default 50, `groove_swing`, text `58 %`). Picking a preset template sets this slider; moving it switches the combo to `Custom`.
- `ToggleButton` "Swing grid" with choices `16th` and `8th` (a `ComboBox` titled `Groove grid`, default `16th`, `groove_grid`).
- Push/pull: five `Slider`s (title `Push pull kick`, `snare`, `hat`, `perc`, `other`), range -40 to +40 ticks (a tick is 1/960 of a quarter note; the label also shows the time at the current tempo, e.g. `+14 ticks (4.4 ms)`), default 0 (one parameter each: `groove_pp_kick`, `groove_pp_snare`, `groove_pp_hat`, `groove_pp_perc`, `groove_pp_other`).
- `Slider` "Humanize time" (title `Groove humanize time`, 0 to 30 ticks max deviation, default 0, `groove_hum_time`) and `Slider` "Humanize vel" (title `Groove humanize velocity`, 0 to 25 %, default 0, `groove_hum_vel`); `TextButton` "Reseed" (title `Groove reseed`) picks a new jitter seed (undoable).
- Accent curve: a 16-cell editor (`AccentCurveEditor`, title `Accent curve`), cells are 0.5 to 1.5 velocity multipliers, drag vertically, double-click resets a cell, right-click menu `Flat`, `Backbeat`, `Downbeat`, `Offbeat`, `Ramp up`.
- `ToggleButton` "Groove" (row E, 70 px, title `Groove on`, tooltip `Apply groove timing and accents when playing and exporting.`, `groove_enable`, default off).
- `ToggleButton` in the call-out "Bake into MIDI export" (title `Groove in MIDI`, default on, stored in the groove state, not a host parameter).
**Pattern grid:** with Groove on, a thin tick mark inside each cell shows the actual shifted position; hits keep their cell. Tooltip adds `Groove: +12 ticks, velocity x1.10`.
**Messages:** `Groove: MPC 62 swing, 6 of 8 roles shifted.` is not used; use `Groove on: MPC 62.` (Info when switched on or the template changes). `Groove shifts hits by up to 50 ms at this tempo.` (Warning when the largest shift exceeds 50 ms, i.e. swing 75 % at slow tempos; informational, not blocking).
**Accessibility:** call-out controls have titles, sliders show text values with units, accent cells are keyboard-adjustable (arrows change value, Tab moves cell).

## 4. Data model and state
### 4.1 Module `modules/groove` (portable; depends on `chop_contracts` for `Role` only; namespace `chopfractal::groove`)
```cpp
constexpr int kTicksPerQuarter = 960;              // engine resolution
enum class Template : std::uint8_t { Straight, Mpc54, Mpc58, Mpc62, Mpc66, Mpc71, Shuffle, Dilla, Custom };
enum class Grid : std::uint8_t { Sixteenth, Eighth };
struct Groove {
  bool enabled = false;
  Template tmpl = Template::Straight;
  float swingPercent = 50.f;                        // 50..75, step 0.5; the pair-ratio definition below
  Grid grid = Grid::Sixteenth;
  std::int8_t pushPull[5] = {0,0,0,0,0};            // kick, snare, hat, perc, other; ticks, -40..40
  std::uint8_t humanizeTime = 0;                    // 0..30 ticks (max deviation)
  std::uint8_t humanizeVel = 0;                     // 0..25 percent
  std::uint32_t seed = 0x0F17'6001u;
  std::uint8_t accent[16];                          // 0..100 -> 0.5..1.5, 50 = 1.0 (neutral default fill 50)
  bool bakeIntoMidi = true;
};
struct Shift { std::int32_t ticks = 0; float velScale = 1.f; };
Shift grooveShift(const Groove&, Role, std::int64_t tickInLoop, std::int64_t eventIndex, int ticksPerBar, int stepsPerBar);
Groove makeTemplate(Template);                      // fills swing/push-pull/humanize/accent of the named template
```
Serialization: `std::vector<std::uint8_t> serialize(const Groove&)` / `parse(...)` (little-endian, 1 version byte, see 4.3).
### 4.2 Template table (all values are defaults the user can then edit; swing is the share of the pair duration taken by the first note: 50 straight, 66.7 triplet, 75 maximum)
| Template | swing % | grid | push/pull (kick, snare, hat, perc, other) ticks | humanize time/vel | accent (steps 1..16 pattern) |
|---|---|---|---|---|---|
| Straight | 50.0 | 16th | 0,0,0,0,0 | 0 / 0 | flat |
| MPC 54 | 54 | 16th | 0,0,0,0,0 | 0 / 0 | flat |
| MPC 58 | 58 | 16th | 0,0,0,0,0 | 0 / 0 | flat |
| MPC 62 | 62 | 16th | 0,0,0,0,0 | 0 / 0 | flat |
| MPC 66 | 66.7 | 16th | 0,0,0,0,0 | 0 / 0 | flat |
| MPC 71 | 71 | 16th | 0,0,0,0,0 | 0 / 0 | flat |
| Shuffle | 66.7 | 8th | 0,0,0,0,0 | 0 / 0 | flat |
| Dilla | 58 | 16th | kick +6, snare +14, hat -6, perc +4, other 0 | 6 / 6 | 100,45,70,45, 110,45,70,50, 100,45,70,45, 110,50,70,55 (scaled to the 0..100 cell range) |

Rationale and sources in section 12. The MPC numbers are the classic Akai range of 50 % (straight) to 75 % (maximum); 54, 58, 62 are the hardware's common "light to medium" settings, 66 is the triplet position and 71 is the heavy end often used for hip-hop shuffle. The `Dilla` template is **not** a claim of copying any record: it combines moderate swing, a late snare and early hats, tiny jitter and an uneven accent, which are the commonly described ingredients of "Dilla time" (swung and straight notes layered with shifted hits), offered as a starting point.
### 4.3 Swing definition used by the engine (per pair)
Pair duration `Pd = 2 * stepTicks` (stepTicks = 240 for 16th, 480 for 8th at 960 ppq). Even step (0-based 0, 2, 4...) onset unchanged; odd step onset moves to `Pd * swing/100` from the pair start, i.e. the delay of an off-beat is `delay = (swing/100 - 0.5) * Pd` ticks; with swing 50 it is 0, with 66.7 it is `stepTicks/3`-aligned triplet (80 ticks at 16th), with 75 it is `0.25*Pd = 120 ticks` at 16th (the third 32nd-note position). Rounded to the nearest integer tick with `std::lround`. **Engine note:** the as-built generation swing parameter maps 0..1 to 50..66.7 % of the pair and bakes the shift into generated event positions (golden hashes include it). Groove Lab swing is additional and applied at playback; to avoid doubling, the UI warns when both are non-zero: `Pattern swing is also on. Groove swing is added on top.` (Info, and the Groove call-out shows a one-click `Set pattern swing to 0` button which only changes the existing swing parameter).
### 4.4 Project state (F01 StateSection tag 14 `groove`, schema 1, 46 bytes; absent when `Groove` equals the default object)
`u8 version=1, u8 flags (bit0 enabled, bit1 bakeIntoMidi), u8 template, u8 grid, u16 swingTenths (500..750), i8 pushPull[5], u8 humanizeTime, u8 humanizeVel, u32 seed, u8 accent[16], 5 reserved zero bytes`. Parsed values are clamped; an unknown version is ignored with a log line and the default groove is used.
### 4.5 Host parameters (manifest v3 appended; ids permanent)
```
{"groove_enable",   "Groove On",        "",  Bool,   0, 1, 0, 1, 0, 3, nullptr}
{"groove_template", "Groove Template",  "",  Choice, 0, 8, 0, 1, 0, 3, "Straight|MPC 54|MPC 58|MPC 62|MPC 66|MPC 71|Shuffle|Dilla|Custom"}
{"groove_swing",    "Groove Swing",     "%", Float, 50, 75, 50, 0.5, 10, 3, nullptr}
{"groove_hum_time", "Groove Humanize Time", "ticks", Float, 0, 30, 0, 1, 10, 3, nullptr}
{"groove_hum_vel",  "Groove Humanize Velocity", "%", Float, 0, 25, 0, 1, 10, 3, nullptr}
```
(`Float` rows use step 0 where not shown.) Grid, the five push/pull values, the accent curve, the seed and the MIDI-bake flag are **state only** (not automatable) to respect the parameter budget (roadmap README section 5): 5 parameters here. The state section is the source of truth for the rest; the five parameters override template, swing, humanize and enable at load when the host restores them.

## 5. Public API
Module functions in 4.1 (pure, no allocation, no clock, no global state; `grooveShift` is `noexcept` and O(1)). Composition: `ProjectSession::setGroove(const Groove&)` (message thread, undo entry `Groove`), `const Groove& groove() const`, and the scheduler hook: `PlaybackScheduler` (inside `audio_renderer`'s `makePlayback`) takes `const GrooveSnapshot*` (a small copy of `Groove`, 64 bytes, published through the existing playback-swap mechanism: the whole `Playback` object is rebuilt on the message thread, so the audio thread never sees a half-updated groove). `Playback::build(pattern, source, groove)` computes shifted onset ticks and velocity scales once when the playback is built, **not** per audio block; so `process()` is unchanged and allocation-free. MIDI export: `midi_export::Options::groove` (optional pointer; null = none) applies the same function. WAV export and F18 use `Playback::build` with the same snapshot. Threading: UI edits trigger a rebuild with 20 ms debounce; rebuild is the same operation as a pattern change.

## 6. Behavior details and edge cases
**Application order per event at build time:** (1) base onset tick `t` (event position in the loop, including F02 hand edits); (2) swing: if `grid` step index `s = floor(t / stepTicks)` is odd and `t` is within `+/- stepTicks/4` of the step's start (events not near a grid line, e.g. hand-moved or retrigger sub-hits, are shifted by the nearest step's swing delay scaled by `1 - dist/(stepTicks/4)` clamped to 0..1, so off-grid hits move smoothly rather than jump), apply `delay(swing)`; (3) push/pull for the event's role (`Role` from the chop role map; unknown role uses `other`); (4) humanize time `h = round((u01(hash) * 2 - 1) * humanizeTime)` with the draw from a **local** stream `splitmix64(seed ^ (eventIndex * 0x9E3779B97F4A7C15) ^ loopIndexMod8)` (never the generator's streams; two loop passes share jitter when `loopIndexMod8` is equal, giving an 8-bar-stable feel; documented) ; (5) clamp the total shift to `+/- stepTicks - 1` so a hit can never cross a neighbor grid line by more than a step; the clamp also keeps order of onsets for same-role consecutive hits (if two hits would swap, the later one is held at `earlier + 1`); (6) velocity: `vel * accent[step % 16 mapped from 16 steps per bar] * (1 + (u01(hash2) * 2 - 1) * humanizeVel/100)`, clamped to the event's valid velocity range 1..127 (rest events and velocity 0 untouched). Shifts that would place an onset before tick 0 wrap to the loop end minus the amount only if the pattern loops (they never leave the cycle); negative shift at tick 0 is clamped to 0 and counted.
**Triplet/meter limits.** The shift function needs `stepsPerBar` multiple of 4 and `ticksPerBar` divisible by `stepsPerBar`; for any other meter (e.g. 7/8 with 14 eighth steps counted as 8th grid) it uses the pattern's own step grid and logs `Groove: unusual meter, swing uses pairs of steps`; for a bar with an odd number of steps the last odd step of the bar is swung relative to the bar start.
**Tempo independence.** Offsets are in ticks (musical time), so swing stays proportional at any BPM; push/pull display converts to ms for the label only.
**Retriggers and E01/E02.** Retrigger sub-hits keep their offsets relative to their parent; echoes follow the parent (E01 section 6). Choke and voice-steal logic uses the shifted onsets (render order), so hits that swap order by clamping never occur.
**Interaction with Level Match (F16).** Accent curve changes loudness: the groove bytes are part of the measurement key, and a groove change schedules a re-measure.
**Interaction with MIDI drag-out (F13).** `bakeIntoMidi` on: events are written at shifted ticks and scaled velocities (clamped 1..127); off: raw grid positions, and the export text file notes `groove not baked`.
**Off:** `groove_enable == false` or `tmpl == Straight` with all values neutral gives no shifts and `Playback::build` takes the identical code path as before (skips the groove stage entirely: the shifted-tick array is not created). Pattern generation, history, the four golden hashes and the renderer regression hash do not change in any case, because the groove never writes into `Pattern`.
**Determinism.** Same groove bytes and pattern give the same shifts; jitter depends only on `(seed, eventIndex, loopIndexMod8)`. Reseed changes only `seed`.
**Validation.** Parameter values clamped; `swingPercent` rounded to 0.5; invalid template index falls back to `Custom`; accent cells clamped to 0..100.

## 7. Test plan
Unit (`modules/groove/tests/test_groove.cpp`):
- `swing_delay_ticks_match_the_published_values`: at 960 ppq, 16th grid: 50 % -> 0, 54 % -> 19, 58 % -> 38, 62 % -> 58, 66.7 % -> 80 (triplet), 71 % -> 101, 75 % -> 120; 8th grid values double.
- `even_steps_never_move_and_odd_steps_move_by_the_delay`.
- `off_grid_hits_move_by_a_distance_scaled_fraction_of_the_delay`.
- `push_pull_per_role_applies_to_the_right_role_only`.
- `humanize_is_deterministic_for_seed_event_and_loop_and_changes_with_seed`; `humanize_stays_within_bounds` (10,000 events: all within +/- humanizeTime and velocity within the percent).
- `total_shift_is_clamped_below_one_step_and_order_is_preserved`.
- `accent_curve_maps_50_to_unity_and_clamps_velocity_1_to_127`; `rest_and_zero_velocity_untouched`.
- `templates_table_matches_spec` (reads the 8 templates and compares to the constants in 4.2).
- `serialize_roundtrip_and_clamping_of_out_of_range_input`; `unknown_version_returns_default`; `default_groove_serializes_to_absent` (composition level).
- `grooveShift_is_noexcept_and_allocation_free` (replaced operator new harness).
Playback/integration (`modules/audio_renderer/tests/test_groove_playback.cpp`, `composition/tests/test_groove_session.cpp`):
- `groove_off_playback_is_bit_identical_to_pre_feature` (record the regression hash first).
- `golden_pattern_hashes_unchanged_with_groove_on_and_off` (the four values).
- `rendered_onsets_land_at_the_shifted_frames_within_1_frame` for 90, 120, 174.5 BPM.
- `changing_groove_rebuilds_playback_without_audio_dropouts` (swap during a render; output has no discontinuity beyond the pattern change itself).
- `midi_export_bakes_shifts_when_enabled_and_matches_raw_when_disabled`.
- `undo_redo_groove_changes`; `state_section_roundtrip_tag_14_and_absent_when_default`; `old_project_loads_with_default_groove`.
- `parameters_appended_after_last_row_and_ids_unchanged`.
GUI (xvfb): `groove_callout_controls_bound`; `picking_template_sets_sliders_and_moving_swing_switches_to_custom`; `accent_editor_keyboard_adjusts_cells`; `tick_marks_show_shifted_position`; `pattern_swing_warning_when_both_on`; titles and tooltips exist.
Sanitizers: ASan/UBSan; TSan on rebuild versus audio thread swap. Manual QA: MPC 62 on a straight breakbeat sounds like the same loop pushed late on the off 16ths; Dilla template is subtle, hats lead and snare drags; reseed changes micro-timing audibly but stays musical; bar loop does not drift across 100 loops.

## 8. Acceptance criteria
- [ ] Groove off: bit-identical playback, export and state of feature-free projects; four golden pattern hashes unchanged with Groove on or off.
- [ ] Swing delays equal the table in section 7 for every template at 960 ppq; even steps never move; shifts never exceed one step.
- [ ] Humanize is deterministic from `(seed, eventIndex, loop mod 8)`, bounded, and never touches the generator's RNG streams.
- [ ] Playback, WAV export, MIDI export (when baking is on) and F18 all use the same shift function and agree on onset ticks.
- [ ] Groove changes rebuild `Playback` on the message thread, never allocate on the audio thread, and are undoable.
- [ ] Five parameters appended with the ids of 4.5; state tag 14 as specified; extra values state-only.
- [ ] Warnings for double swing and large shifts appear with exact text; all controls have titles and tooltips; sources section present and each template cites a source or is marked as an offered default.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/groove/**` (CMake, module.json, README, MIGRATION, headers, src, tests, example) | New | templates, shift, serialization |
| `modules/audio_renderer/src/renderer.cpp`, `renderer.hpp` | Mod **(hot)** | `Playback::build` takes the groove snapshot |
| `modules/midi_export/src/*`, `include/.../midi.hpp`, tests | Mod | `Options::groove` |
| `modules/plugin_host_adapter/src/parameters.cpp`, `.hpp`, tests | Mod **(hot)** | five rows |
| `composition/include/.../project_session.hpp`, `src/project_session.cpp`, `src/groove.cpp`, `src/state_sections.cpp` | Mod/New **(hot)** | setGroove, undo, tag 14, rebuild |
| `plugin/src/GrooveCallout.h/.cpp`, `AccentCurveEditor.h/.cpp`, `PatternPanel` tick marks, `PluginProcessor.cpp`, FeaturePanel | New/Mod **(hot)** | UI and plumbing |
| `CMakeLists.txt` root, `docs/BUILD_STATUS.md`, roadmap README | Mod **(hot)** | module list, registries |

## 10. Risks and mitigations
- **Double swing with the generator's swing.** Mitigation: the Info message and the one-click reset; documented in the manual.
- **Perceived timing of "Dilla" templates is subjective.** Mitigation: labelled as an offered default, all values editable; QA by listening; no claim of fidelity to any record.
- **Order inversions of close hits.** Clamp and order-preserving rule, tested.
- **Interplay with hand edits (F02) and retriggers.** Offsets relative to parent, smooth fall-off for off-grid hits.
- **Build-time cost on large patterns.** O(n) with n <= 4096 events; measured in a benchmark test (logged).
- **PPQ resolution differences vs MPC (96 vs 960) mean the same percentage has finer steps here.** Acceptable; rounded to tick; documented.

## 11. Implementation steps
1. Record the regression hash on current main (commit alone). 2. Module `groove` with shift, templates, serialization and unit tests. 3. `Playback::build` integration with groove snapshot, bit-exact-when-off test. 4. MIDI export option. 5. Manifest rows, processor plumbing, session setGroove, undo, state tag 14. 6. UI call-out, accent editor, grid tick marks, warnings. 7. Docs, registries, QA.

## 12. Sources and rationale
- Roger Linn on the MPC swing range and the 50 % straight to 75 % maximum scale, with 66 % as triplet: Attack Magazine, "Swing in DAWs" (https://attackmagazine.com/technique/passing-notes/daw-drum-machine-swing/2) and the same series part 4 (https://attackmagazine.com/technique/passing-notes/daw-drum-machine-swing/4); background on Linn's shuffle: https://melodiefabriek.com/blog/roger-linns-shuffle/ and https://melodiefabriek.com/blog/mpc-swing-reason (MPC swing in Reason, percent values and the effect of timing resolution).
- J Dilla, MPC3000 and the "swung plus straight plus shifted" description of Dilla Time: MusicRadar, "How J Dilla and his MPC changed beatmaking forever" (https://www.musicradar.com/artists/djs-producers/every-producer-bows-down-to-dilla-whether-they-like-it-or-not-how-j-dilla-and-his-mpc-changed-beatmaking-forever), RouteNote, "The Dilla swing" (https://create.routenote.com/blog/the-dilla-swing-how-one-producer-humanised-the-sound-of-hip-hop/) and Ethan Hein's notes on the book Dilla Time (https://ethanhein.substack.com/p/dilla-time).
- Consistent with the as-built engine: swing parameter 0..1 maps to 50 to 66.7 % of the pair (docs/specs/pattern-generation-engine.md).
- These pages were found by web search during the audit; the specific percentages 54, 58, 62, 71 and the Dilla numbers are this spec's offered defaults (marked as such in 4.2), not quotations. Re-verify the links when the spec is implemented.
