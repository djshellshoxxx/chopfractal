# `input_capture` — Input capture

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Records the track input into a preallocated buffer: Arm, Capture, Stop, Keep. Synced mode starts on the next bar line and ends by host position (bar-exact through tempo changes); Free mode is a ring that keeps the last N seconds. Output is identical for any host block size.

## Public API

- `CaptureEngine::arm/startNow/requestStop/discard/take/status/reap/onPrepare/process`
- `ArmSettings`, `CaptureTransport`, `CaptureStatus`, `CapturedAudio`, `maxCaptureFrames()`

Public headers:

- `include/chopfractal/input_capture/capture.hpp`

## Contract

- **Threading:** Message thread: arm, startNow, requestStop, discard, take, status, reap. Audio thread: process only (no allocation, locks or I/O). Lifecycle calls from other threads are lock-free handoffs through atomics.
- **Deterministic:** yes · **Real-time safe:** yes (see threading)

## Limits

- Mono or stereo, 8 kHz to 384 kHz, at most 60 s, 100 MiB and 11,520,000 frames; synced captures of 1 to 8 bars; synced captures that cannot fit are refused, free ring captures are clamped

## Dependencies

- `chop_contracts` ^0.1 — Result and Status error types

## Build and test

```sh
cmake -S modules/input_capture -B build/input_capture && cmake --build build/input_capture && ctest --test-dir build/input_capture
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
