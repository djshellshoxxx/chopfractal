# `input_capture` — Input capture

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Real-time-safe recorder for a plugin's audio input. Arm, record freely or exactly N bars starting on a bar line, stop, then take the audio as a planar buffer. Sample-accurate and independent of the host block size.

## Public API

- `Capture::arm(Options)`, `stop()`, `cancel()`, `take()` (control thread)
- `Capture::process(in, channels, frames, Transport)` (audio thread, no allocation)
- `framesForBars(bars, quartersPerBar, bpm, sampleRate)`

Public headers:

- `include/chopfractal/input_capture/capture.hpp`

## Contract

- **Threading:** One control thread (arm/stop/cancel/take) plus one audio thread (process); they communicate only through atomics. process() allocates, locks and waits for nothing.
- **Deterministic:** yes · **Real-time safe:** yes (see threading)

## Limits

- At most 60 seconds and 100 MB; mono or stereo; sample rate 8 kHz to 384 kHz
- A stop or cancel of a Recording capture takes effect on the next process() call

## Dependencies

- `chop_contracts` ^0.1 — Result/Status

## Build and test

```sh
cmake -S modules/input_capture -B build/input_capture && cmake --build build/input_capture && ctest --test-dir build/input_capture
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
