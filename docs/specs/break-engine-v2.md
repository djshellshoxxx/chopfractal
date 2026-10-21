# Break engine v2

Status: implemented. Sources: `docs/research/BREAK_ENGINE_RESEARCH.md` (sections 5, 6, 10, 12) and
`docs/research/BREAK_ANALYSIS.md`.

## Why

Engine v1 draws each step independently from the chop pool. Output has no backbeat, no velocity hierarchy and no
phrase shape, so it does not sound like a break even before any effect is applied. Engine v2 plans every bar from
drum roles, so glitched or stuttered output still rests on a kick and a snare.

## Versioning and hashes

- `Pattern.engineVersion` is persisted (pattern codec). 1 = Legacy, 2 = Break. A pattern without the field is Legacy.
- Library default (`pattern::Settings::engineVersion`) is 1: every existing golden hash is unchanged.
- Plugin default is Break through the host parameter `engine_version` (default 1 = Break). A project saved before the
  parameter existed carries no `engineParams` marker; on load the processor sets `engine_version` to Legacy and
  `groove_enable` off, so it keeps generating with the classic engine.
- v2 falls back to Legacy (with a notice) for meters other than 4/4 or grids coarser than 1/16. The result is stamped
  Legacy.
- v2 goldens are pinned in `modules/pattern_engine/tests/test_pattern_engine_v2.cpp`. The planner uses exact integer
  and single-rounding arithmetic only (`-ffp-contract=off`), so GCC and Clang agree; other platforms are checked by CI.

## Modules

| Module / file | Role |
|---|---|
| `break_engine` | Pure bar planner: roles, anchors, tables, collision and adjacency rules, phrase slots, fills, glitch gestures, microtiming, profiles |
| `pattern_engine/src/generate_v2.cpp` | Pools by role, chooser, level = velocity / 127 x chop gain trim, anchor-preserving mutate |
| `groove` | F17 Groove Lab: templates, swing, push/pull per role class, seeded humanize, accents; 46-byte state form |
| `composition/src/engine_roles.cpp` | Role annotation (explicit assignment wins, otherwise audio detection), per-role level matching, anchor overlay for Fractal Rhythm, transient-first for Zoom |
| `composition/src/groove.cpp` | Groove applied once at playback build, tag 14 state section, undo, MIDI bake |
| `chop_contracts` | `FlatEvent::chokeGroup` and `smoothFades` hints, set by composition for v2 patterns; the renderer (RND branch) consumes them |

## Rules (summary)

- Roles: Kick, Snare, Ghost, Hat, OpenHat, Cymbal, Perc. Detected from the audio unless the user assigned a role.
- Anchors: kick on step 1, snare on steps 5 and 13. Rolling backbeat puts ghosts on steps 7 and 15. Anchors are
  never removed in Strict mode; Profile mode lets soft profiles (Breakcore, IDM) drop them, Free removes the guarantee.
- Per-step probability and velocity tables per role and profile (header comment in `tables.cpp` cites the source table;
  Groove MIDI Dataset, CC BY 4.0, see `THIRD_PARTY_NOTICES.md`).
- Velocity hierarchy: backbeat snare far above ghosts; ghosts only on weak steps, never three in a row; kick and snare
  never share a step; at most 12 events per bar outside glitch profiles.
- Phrase: 4-bar slots, bar 4 carries fills (rolls 1/8, 1/16, 1/32 with decay or ramp, reverse snare or cymbal, tape-stop
  at phrase end). Chaos raises glitch gestures and fill probability and never lowers anything.
- Microtiming per role with seeded humanize, bounded to 20 ms for anchors and 12 ms otherwise.
- Mutate keeps anchors. Fractal Rhythm children are overlaid with the anchors of the bar. Zoom into an anchor keeps its
  transient first.

## Session API (host-facing)

`pattern::Settings::brk` (profile, backbeat, anchors, groove, humanize, fill, glitch, chaos, tempo hint),
`applyBreakParams`, `settingsFromParams`, `ProjectSession::annotatedChops/setGroove/undoGroove/redoGroove/midiEvents`.
Host parameters, appended to manifest v3: `engine_version`, `break_profile`, `break_backbeat`, `break_anchors`,
`break_groove`, `break_humanize`, `break_fill`, `break_glitch`, `groove_enable`, `groove_template`, `groove_swing`,
`groove_hum_time`, `groove_hum_vel`.

## Tests

- `break_engine.unit`: a unit test per rule, and the plan-level metrics 1 to 14, 24, 25 as `break_quality_*` cases over
  200 seeds and every profile. Thresholds are the table in `modules/break_engine/tests/break_thresholds.hpp`.
- `break_quality_audio` (ctest, runs `tools/engine_audition.cpp --check`): audio-level metrics 16, 18, 19, 20, 23, 25 over
  every profile on the synthetic kit loop. Metric 22 (choke) belongs to the renderer branch that consumes the hints.
  Metrics 17, 21 and 24 are not automated on a full mix; see the notes in the audition tool.
- Audition: `engine_audition --seeds 8 --out DIR` writes one WAV per profile and seed and prints a text report.
