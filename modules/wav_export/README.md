# `wav_export` — WAV export

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Deterministic WAV encoder and decoder (16-bit, 24-bit PCM and 32-bit float, mono or stereo) with optional peak normalization, seeded TPDF dither, clip reporting, atomic file writing and safe file-name sanitizing. Pure byte transforms: no audio engine or host types.

## Public API

- `encode(planar, Options, EncodeReport*)` -> WAV bytes; `decode(bytes)` -> planar floats
- `writeFileAtomic(path, bytes, overwrite)` (temp file then rename; never replaces a file unless asked)
- `sanitizeFileName(text)` -> [a-z0-9_-] name

Public headers:

- `include/chopfractal/wav_export/wav.hpp`

## Contract

- **Threading:** Pure functions; thread-safe. Not real-time safe (allocates).
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 1 or 2 channels, 8 kHz to 384 kHz, at most 4 GiB minus headers (RIFF limit); non-finite samples are written as silence

## Dependencies

- `chop_contracts` ^0.1 — Result/Status and the deterministic RNG used for dither

## Build and test

```sh
cmake -S modules/wav_export -B build/wav_export && cmake --build build/wav_export && ctest --test-dir build/wav_export
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
