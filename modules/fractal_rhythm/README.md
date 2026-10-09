# `fractal_rhythm` — Fractal rhythm

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Generates a self-similar groove: a short motif (x hit, a accent, . rest) is applied at every scale, each hit subdividing itself by the same motif down to a chosen depth. Output is ordinary nested events (children via NestedPattern) that the pattern engine, zoom, renderer and flatten already understand.

## Public API

- `generateBar(Context, Settings)` -> `vector<Event>` for one bar
- `validate(Context, Settings)`
- `Settings{motif, depth, seed, mutation, mirror, accentDecay}`

Public headers:

- `include/chopfractal/fractal_rhythm/fractal.hpp`

## Contract

- **Threading:** Pure functions; thread-safe. Not real-time safe.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Motif 2..8 cells, depth 1..3, smallest leaf cell 15 ticks, leaves limited by the caller's event budget (error carries the deepest permitted depth in `hint`)

## Dependencies

- `chop_contracts` ^0.1 — Event/NestedPattern model, ChopSnapshot, ICandidatePolicy, derived IDs, RNG and Result

## Build and test

```sh
cmake -S modules/fractal_rhythm -B build/fractal_rhythm && cmake --build build/fractal_rhythm && ctest --test-dir build/fractal_rhythm
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
