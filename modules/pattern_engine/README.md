# `pattern_engine` — Pattern engine

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Deterministic hierarchical pattern model (phrase, bar, beat, event), Generate and Mutate with locks, edit commands, flattening to a bounded event list, validation, versioned state, and an undo/redo + A/B session.

## Public API

- `generate(chops, settings, policy)` and `mutate(pattern, chops, options, policy)`
- `flatten(pattern, chops)`, `validate(pattern)`, `sanitize(pattern, chops)`
- edits: `setLock`, `clearLocks`, `addEvent`, `deleteEvent`, `moveEvent`, `setEventChop`, `setEventTransform`, `duplicateBar`, `restoreSourceOrder`, `setChild`, `collapse`, `setChildActive`
- `serialize()` / `deserialize()` (schema 1, engine version 1)
- `PatternSession`: undo/redo and two A/B snapshot slots

Public headers:

- `include/chopfractal/pattern_engine/pattern.hpp`
- `include/chopfractal/pattern_engine/session.hpp`

## Contract

- **Threading:** UI/worker thread only. Not real-time safe; the audio side consumes only the flattened events.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Pattern length 1, 2, 4 or 8 bars; grids 1/1 to 1/32, straight or triplet
- 512 events per pattern (configurable down), retrigger 2-8, pitch range up to 24 semitones, micro-shift up to one eighth of a grid cell

## Dependencies

- `chop_contracts` ^0.1 — event model, flatten(), chop snapshot, policy interface, RNG, byte codec, time, undo stack

## Build and test

```sh
cmake -S modules/pattern_engine -B build/pattern_engine && cmake --build build/pattern_engine && ctest --test-dir build/pattern_engine
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-TBD` — the root license policy is still an open decision. Keep any `LICENSE`/`NOTICE` files when copying.
