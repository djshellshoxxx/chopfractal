# `audio_renderer` — Audio renderer

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Voice playback and event transforms: sample-accurate host-synced scheduler, bounded voice pool with stealing, reverse/pitch(resample)/gate/fades/level/pan, retrigger, dry mix and output gain, lock-free handoff of immutable playback data, auditioning, and an offline renderer that shares the live `process()` path.

## Public API

- `makePlayback(source, flatEvents, lengthTicks)` validates and prepares immutable data; `makePassThroughPlayback(source)`
- `Renderer::prepare/process/reset/requestPreview/activeVoices` and `mailbox().publish()`
- `renderOffline(playback, settings)`
- `Mailbox<T>`: wait-free reader, reclaims only after the reader acknowledges a newer object

Public headers:

- `include/chopfractal/audio_renderer/renderer.hpp`
- `include/chopfractal/audio_renderer/mailbox.hpp`

## Contract

- **Threading:** `process()`, `reset()` and `Mailbox::acquire()` are real-time safe (no allocation, locks, I/O). `prepare()`, `makePlayback()`, `Mailbox::publish()` and `renderOffline()` are non-real-time. `requestPreview()` is for one producer thread.
- **Deterministic:** yes · **Real-time safe:** yes (see threading)

## Limits

- 8 voices (plus fading voices), 4096 prepared events after retrigger expansion, host blocks of any size (chunked above the prepared maximum)
- Transform order is fixed: region, reverse, rate/pitch, gate/fades, level/pan, voice sum, dry mix and output gain. Pan is a balance law; mono output is (L+R)/2; interpolation is linear

## Dependencies

- `chop_contracts` ^0.1 — flat event and time contracts, Result

## Build and test

```sh
cmake -S modules/audio_renderer -B build/audio_renderer && cmake --build build/audio_renderer && ctest --test-dir build/audio_renderer
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-TBD` — the root license policy is still an open decision. Keep any `LICENSE`/`NOTICE` files when copying.
