# `time_stretch` — Time stretch

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Offline WSOLA time stretching with chop-anchored regions: each region between anchors is stretched on its own, so every chop boundary lands exactly on its drift-free scaled position and attacks are kept sharp. Three quality presets, progress and cancellation.

## Public API

- `mapPosition()` / `mapLength()` (drift-free integer scaling), `makePlan()`, `lengthRatio()`, `checkRatio()`
- `stretch(input, plan, quality, progress)` returning planar floats of exactly `plan.outFrames` frames

Public headers:

- `include/chopfractal/time_stretch/stretch.hpp`

## Contract

- **Threading:** Pure functions, safe to call from any non-real-time thread (several at once). Never call from the audio thread.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Mono or stereo; tempo change between 0.5x and 2x of the length; regions shorter than three windows are copied (hit mode) instead of stretched

## Dependencies

- `chop_contracts` ^0.1 — Result and Status error types

## Build and test

```sh
cmake -S modules/time_stretch -B build/time_stretch && cmake --build build/time_stretch && ctest --test-dir build/time_stretch
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
