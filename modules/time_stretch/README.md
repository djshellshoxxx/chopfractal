# `time_stretch` — Offline time stretch

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Offline WSOLA time stretching of planar float audio (1 or 2 channels) in chop-anchored regions, three quality presets (Draft, Standard, High), progress and cancel, and the exact integer marker scaling rule that keeps chop boundaries on their scaled positions without drift. No third-party code; decoding, tempo detection, and UI are adapters.

## Public API

- `lengthRatio(sourceBpm, targetBpm)` and `checkRatio(ratio)` (0.5x to 2x)
- `mapPosition(pos, in, out)` / `mapLength(len, in, out)`: floor((2*p*out + in) / (2*in)) computed from the original value, so scaled markers never drift
- `makePlan(inFrames, outFrames, anchors)`: sorted, de-duplicated region plan with 0 and inFrames added
- `stretch(Input, Plan, Quality, Progress)`: planar result of exactly `plan.outFrames` frames per channel; returns `Cancelled` when the progress callback returns false
- `Quality::Draft|Standard|High` with `qualityParams()`

Public headers:

- `include/chopfractal/time_stretch/stretch.hpp`

## Contract

- **Threading:** Worker thread only. Pure functions with no shared state, so concurrent calls on independent inputs are safe. Never call from the audio thread.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Length ratio 0.5 to 2.0 inclusive; frame counts up to 2,000,000,000 per side; 1 or 2 channels; 8 kHz to 384 kHz
- Regions shorter than three analysis windows (one drum hit) are copied with a 5 ms fade instead of stretched
- Non-finite input samples are read as 0; output is always finite

## Dependencies

- `chop_contracts` ^0.1 — Result/Status/ErrorCode (including Cancelled)

## Build and test

```sh
cmake -S modules/time_stretch -B build/time_stretch && cmake --build build/time_stretch && ctest --test-dir build/time_stretch
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
