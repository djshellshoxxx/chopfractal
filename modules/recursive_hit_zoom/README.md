# `recursive_hit_zoom` — Recursive hit zoom

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Creates a nested child pattern inside one event's exact time window and source bounds. Children never read outside their ancestor's source range unless an explicit source replacement is requested.

## Public API

- `createChildPattern(parent, context, settings, seed)`
- `mutateChildren(parentWithChild, context, settings, seed)` keeps locked child events
- `collapse(parent)`, `flatten(parent, windowStart, chops)`
- `serialize()` / `deserialize()` (schema 1)
- `nearestPermittedSubdivision(budget)`

Public headers:

- `include/chopfractal/recursive_hit_zoom/zoom.hpp`

## Contract

- **Threading:** Pure functions; thread-safe. Not real-time safe.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Subdivisions 2, 3, 4, 5, 6 or 8; minimum 15 ticks per child
- Depth 2 by default, 3 hard maximum; exceeding the event budget returns `LimitExceeded` with the nearest permitted subdivision, never a truncation

## Dependencies

- `chop_contracts` ^0.1 — event model, chop snapshot, flatten(), RNG, byte codec

## Build and test

```sh
cmake -S modules/recursive_hit_zoom -B build/recursive_hit_zoom && cmake --build build/recursive_hit_zoom && ctest --test-dir build/recursive_hit_zoom
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-TBD` — the root license policy is still an open decision. Keep any `LICENSE`/`NOTICE` files when copying.
