# `scenes` — Scene list and sequencer

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

An ordered list of up to 32 scenes (opaque serialized pattern plus name, loop count and what follows) with stable ids, a validated byte codec, and a pure loop-boundary Sequencer that decides which scene to arm next (advance, repeat, jump, stop, wrap). It never parses the pattern bytes and never touches audio: the host arms and switches.

## Public API

- `Scene`, `SceneList`, `validate`, `validateScene`, `add`, `remove`, `move`, `find`, `indexOf`
- `serialize` / `deserialize` (schema 1 body; rejects truncation and bit damage)
- `Sequencer::start/stop/onBoundary/onTransportStopped/onTransportStarted` returning `Advance{arm, switched, playing, finished}`

Public headers:

- `include/chopfractal/scenes/scenes.hpp`

## Contract

- **Threading:** Not thread-safe: own it from one thread (the plugin uses the message thread). The Sequencer allocates nothing after construction. Not real-time safe (the list owns vectors).
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 32 scenes, 1 to 64 loops, names 1 to 32 bytes, 1 MiB per pattern, 32 MiB serialized list

## Dependencies

- `chop_contracts` ^0.1 — Result/Status and the bounds-checked byte codec used by the list serializer

## Build and test

```sh
cmake -S modules/scenes -B build/scenes && cmake --build build/scenes && ctest --test-dir build/scenes
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
