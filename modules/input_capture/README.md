# `input_capture` — Input capture

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Records the track's audio input into a preallocated buffer, either free-running (a ring that keeps the last N seconds) or synced to the host's bar grid (starts on the next bar line, ends by ppq so tempo changes stay bar-exact). Four states (Idle, Armed, Capturing, Captured), sample-accurate start, meters with clip counting, and an O(frames) hand-off of the planar audio.

## Public API

- `CaptureEngine::arm(ArmSettings)`, `startNow()`, `requestStop()`, `discard()`, `take() -> CapturedAudio`, `status()`, `reap()`
- `CaptureEngine::process(in, numChannels, frames, CaptureTransport)` (audio thread) and `onPrepare(sampleRate, channels)`
- `maxCaptureFrames(sampleRate, channels)` and the limits `kMaxCaptureSeconds`, `kMaxCaptureBytes`, `kMaxCaptureFrames`

Public headers:

- `include/chopfractal/input_capture/capture.hpp`

## Contract

- **Threading:** One audio thread calls process() (no allocation, locks or I/O); every other call is a message or worker thread. State changes hand the buffer between the threads with release/acquire stores, so no locks exist.
- **Deterministic:** yes · **Real-time safe:** yes (see threading)

## Limits

- Mono or stereo, 8 kHz to 384 kHz, at most 60 s, 100 MiB and 11,520,000 frames; memory is allocated per arm and released when the capture ends, so an idle engine holds nothing

## Dependencies

- `chop_contracts` ^0.1 — Status/Result error types

## Build and test

```sh
cmake -S modules/input_capture -B build/input_capture && cmake --build build/input_capture && ctest --test-dir build/input_capture
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
