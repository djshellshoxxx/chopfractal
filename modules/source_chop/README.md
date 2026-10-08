# `source_chop` — Source and chop model

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Source metadata, chop markers, marker editing with undo/redo, region and snapshot export, transient and even-grid analysis proposals, snapping helpers, and source identity hashing. Decoding, capture, file dialogs, and waveform drawing are adapters.

## Public API

- `ChopMap::create(SourceInfo)`; `addMarker/moveMarker/deleteMarker/setEnabled/setTrim/setFades/setGroup/setLabel`
- `regions()` and `snapshot()` (enabled chops, trims applied, immutable)
- `preview()/applyProposal()` with `MergeMode::Replace|Merge`, one undo step
- `detectTransients()`, `evenGrid()`, `snapToNearest()`, `snapToZeroCrossing()`, `computeSourceId()`
- `serialize()` / `deserialize()` (schema 1)

Public headers:

- `include/chopfractal/source_chop/chop_map.hpp`
- `include/chopfractal/source_chop/analysis.hpp`

## Contract

- **Threading:** UI/worker thread only. Analysis functions are pure and meant for a worker thread. Not real-time safe.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 256 markers (spec requires at least 128), minimum region 64 frames by default, 60 s at 192 kHz maximum source
- Markers are integer frames in the source's own sample rate, so they never drift when the host rate changes

## Dependencies

- `chop_contracts` ^0.1 — typed chop IDs, SampleRange, the ChopSnapshot export, Result, byte codec, and the undo stack

## Build and test

```sh
cmake -S modules/source_chop -B build/source_chop && cmake --build build/source_chop && ctest --test-dir build/source_chop
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
