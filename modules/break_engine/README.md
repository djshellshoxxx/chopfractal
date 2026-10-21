# `break_engine` — Break engine

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Break-aware rhythm model (engine v2): role-driven 16-step bar plans for drum breaks with backbeat anchors, per-step per-role probability and velocity tables, ghost notes, kick/snare collision rules, 4-bar phrase modifiers, bar-4 fills, fill and glitch gesture tables, six genre profiles, and seeded role-aware swing and microtiming. It plans which role plays where; mapping onto chops and events is done by pattern_engine.

## Public API

- `planPattern(PlanSettings)` / `planBar(PlanSettings, bar)` -> `BarPlan{hits[]}` with role, step, tick, velocity, gesture tag and anchor flag
- `Options` (profile, backbeat variant, anchor mode, groove, humanize, fill, glitch, chaos, phrase strictness, tempo hint) and `valid(Options)`
- `cellProbability`, `anchorProbability`, `fillProbability`, `effectiveSwing`, `profileInfo` for tests and reports
- `canAdd(settings, cells, cell, anchor)`: the collision and density rules, reused by mutate

Public headers:

- `include/chopfractal/break_engine/break_engine.hpp`

## Contract

- **Threading:** Pure functions; thread-safe; not real-time safe (allocates the plan).
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 16 steps per bar (4/4 on a 16th grid); other meters stay on the legacy engine
- Rolls and stutters are separate hits (no retrigger expansion); at most two glitch gestures and one fill per bar

## Dependencies

- `chop_contracts` ^0.1 — DrumRole, Ticks, EventTransform / EventFx, RNG

## Build and test

```sh
cmake -S modules/break_engine -B build/break_engine && cmake --build build/break_engine && ctest --test-dir build/break_engine
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
