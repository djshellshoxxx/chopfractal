# `groove` — Groove Lab

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Playback-time groove: MPC-style swing (50 to 75 percent of a step pair on a 16th or 8th grid), per-role push/pull, seeded humanize of time and velocity, a 16-cell accent curve and eight named templates. A groove never edits stored events; the caller applies grooveShift (or apply) once when it builds a playback snapshot or a MIDI file.

## Public API

- `Groove` (enabled, template, swing, grid, push/pull per role class, humanize time and velocity, seed, accent, bake flag) and `makeTemplate`
- `grooveShift(groove, role, tick, eventIndex, ticksPerBar, stepsPerBar, loopIndex)` -> `Shift{ticks, velScale}`
- `apply(groove, FlatEventList&, loopTicks, ticksPerBar, stepsPerBar, roleOf)` shifts a flattened pattern
- `serialize` / `parse`: the 46-byte project-state form; `sanitized` clamps every field

Public headers:

- `include/chopfractal/groove/groove.hpp`

## Contract

- **Threading:** Pure functions; thread-safe; grooveShift is noexcept and allocation-free. apply() sorts and belongs on the message thread.
- **Deterministic:** yes · **Real-time safe:** yes (see threading)

## Limits

- Shifts are limited to less than half a groove step so events never swap order
- Swing is defined per step pair of the pattern's own step grid (16th or 8th variant)

## Dependencies

- `chop_contracts` ^0.1 — DrumRole, FlatEvent, Ticks

## Build and test

```sh
cmake -S modules/groove -B build/groove && cmake --build build/groove && ctest --test-dir build/groove
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
