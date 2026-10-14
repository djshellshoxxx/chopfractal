# `preset_store` — Preset store

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Versioned preset records for generation settings, Fractal Rhythm motif and Evolve cadence: validation, bounded binary (de)serialization with a checksum, six factory presets, and safe file-name derivation. Pure data: files and folders are handled by the plugin shell.

## Public API

- `validate(Preset)`, `serialize(Preset)`, `deserialize(bytes)`
- `factoryPresets()` (6 validated presets)
- `fileNameFor(name)` -> sanitized `.cfpreset` name

Public headers:

- `include/chopfractal/preset_store/presets.hpp`

## Contract

- **Threading:** Pure functions; thread-safe. Not real-time safe (allocates).
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Name 1..48 characters; motif 2..8 cells of x, a, .
- Files are small (under 512 bytes); deserialize rejects truncation, trailing bytes, bad magic or checksum, unknown versions

## Dependencies

- `chop_contracts` ^0.1 — byte codec, Result/Status

## Build and test

```sh
cmake -S modules/preset_store -B build/preset_store && cmake --build build/preset_store && ctest --test-dir build/preset_store
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
