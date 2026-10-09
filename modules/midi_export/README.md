# `midi_export` — MIDI file export

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Deterministic Standard MIDI File (format 0) encoder and decoder for note sequences with tempo and time-signature events. The tick grid equals the pattern engine's, so exported hits land on exact ticks.

## Public API

- `encode(FileSpec)` -> .mid bytes; `decode(bytes)` -> FileSpec (for verification)
- `FileSpec{bpm, timeSignature, trackName, notes[]}`, `Note{note, velocity, start, duration, channel}`

Public headers:

- `include/chopfractal/midi_export/midi.hpp`

## Contract

- **Threading:** Pure functions; thread-safe. Not real-time safe (allocates).
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 960 ticks per quarter; notes 0..127, velocity 1..127, at most 65536 notes; same-pitch overlaps are clipped and zero-length results dropped

## Dependencies

- `chop_contracts` ^0.1 — Result/Status and the Ticks type (960 per quarter, matching the pattern grid)

## Build and test

```sh
cmake -S modules/midi_export -B build/midi_export && cmake --build build/midi_export && ctest --test-dir build/midi_export
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
