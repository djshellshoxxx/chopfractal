# `scenes` — Evolve scenes

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

A bounded list of pattern snapshots (scenes) with per-scene loop counts and next/repeat/stop/jump actions, a versioned codec, and a pure sequencer that, given observed loop boundaries, says which scene plays and which scene the audio side should arm next.

## Public API

- `SceneList`, `Scene`, `add/remove/move/find/indexOf/validate`
- `serialize()` / `deserialize()` (schema 1, hostile-input safe)
- `Sequencer::start/stop/onBoundary/onTransportStopped/onTransportStarted` returning `Advance{arm, switched, playing, finished}`

Public headers:

- `include/chopfractal/scenes/scenes.hpp`

## Contract

- **Threading:** Not thread-safe by itself: use from one thread (the plugin uses the message thread). The sequencer allocates nothing.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 32 scenes, 1 to 64 loops each, names of 1 to 32 characters, 1 MiB of serialized pattern per scene

## Dependencies

- `chop_contracts` ^0.1 — Result/Status and the byte codec

## Build and test

```sh
cmake -S modules/scenes -B build/scenes && cmake --build build/scenes && ctest --test-dir build/scenes
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
