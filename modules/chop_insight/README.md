# `chop_insight` — Chop insight

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Deterministic, model-free audio analysis for Smart Setup: per-chop spectral balance, noisiness and decay, rule-based role suggestions (kick, snare, hat, cymbal, other) with confidence and a plain-language reason, and loop length / tempo suggestions from a clip duration.

## Public API

- `analyzeChop(SourceView, SampleRange)` -> `ChopFeatures`
- `suggestRoles(SourceView, ChopSnapshot)` -> `RoleSuggestion{chop, role, confidence, reason}[]`
- `suggestLoop(seconds, hostBpm)` -> `LoopSuggestion{beats, bars, bpm, confidence, note}[]`

Public headers:

- `include/chopfractal/chop_insight/insight.hpp`

## Contract

- **Threading:** Pure functions; thread-safe; run on a worker thread. Not real-time safe.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Mono mix-down analysis; chops shorter than 5 ms or silent are classified as `other` with low confidence; suggestions never change project state

## Dependencies

- `chop_contracts` ^0.1 — ChopSnapshot, SampleRange, ChopId and Result

## Build and test

```sh
cmake -S modules/chop_insight -B build/chop_insight && cmake --build build/chop_insight && ctest --test-dir build/chop_insight
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
