# `state_codec` — State codec

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Versioned project container. Each stateful module serializes its own payload; this module composes them under module ID + schema version with a CRC, validates every size before allocating, and chains registered migrations.

## Public API

- `encode(ProjectState, CodecLimits)` / `decode(bytes, CodecLimits)`
- `ProjectState{projectVersion, modules: id -> {schemaVersion, bytes}}`
- `MigrationRegistry::add(moduleId, fromVersion, step)` and `migrate(moduleId, payload, targetVersion)`
- `crc32()`

Public headers:

- `include/chopfractal/state_codec/codec.hpp`
- `include/chopfractal/state_codec/result.hpp`

## Contract

- **Threading:** Pure functions; thread-safe. Not real-time safe (allocates).
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 256 MiB total, 192 MiB per payload, 32 modules, 64-character module IDs (sized for the 128 MiB default embedded-source cap plus pattern and history data)

## Dependencies

None.

## Build and test

```sh
cmake -S modules/state_codec -B build/state_codec && cmake --build build/state_codec && ctest --test-dir build/state_codec
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
