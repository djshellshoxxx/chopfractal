# `level_match` — Level match

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

ITU-R BS.1770-4 integrated loudness (K-weighting, 400 ms blocks with 75 % overlap, absolute and relative gates) and the Level Match gain: the clamped, 0.1 dB rounded gain that brings a generated pattern to the loudness of the loop it came from, with a headroom rule so the match alone can never clip.

## Public API

- `measure(planar, channels, frames, sampleRate) -> Loudness{lufs, samplePeak, valid}`
- `computeMatch(source, pattern, trimDb, limiterOn, ceilingDb) -> MatchResult{gainDb, requestedDb, valid, clamped, headroomLimited}`
- `kWeightShelf(sampleRate)` and `kWeightHighPass(sampleRate)` biquad coefficients (double)

Public headers:

- `include/chopfractal/level_match/level_match.hpp`

## Contract

- **Threading:** Pure functions with no shared state: call from any non-real-time thread (the plugin runs them on a worker). They allocate, so never call them from an audio callback.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Mono or stereo, 8 kHz and up; at least 100 ms of audio; gain clamped to +/-12 dB and rounded to 0.1 dB; mono is measured as if duplicated to both channels

## Dependencies

None.

## Build and test

```sh
cmake -S modules/level_match -B build/level_match && cmake --build build/level_match && ctest --test-dir build/level_match
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
