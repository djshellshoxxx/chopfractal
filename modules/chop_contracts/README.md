# `chop_contracts` — Chop contracts

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

The small shared vocabulary every other module speaks: typed IDs, musical ticks, results/errors, a bounds-checked byte codec, a specified RNG, the chop snapshot, the nested event model with its single canonical flatten(), the candidate-policy interface, and an undo stack.

## Public API

- `Id<Tag>` IDs (`SourceId`, `ChopId`, `EventId`, `ScopeId`); IDs are never reused, 0 is invalid, derived child IDs set `kDerivedIdBit`
- `Ticks` (960 per quarter), `TimeSignature`, `Grid`, `gridTicks()`, `ticksPerBar()`: exact integer musical time
- `Result<T>`, `Status`, `Error{code,message,hint}`: core modules return errors, they never present UI
- `bytes::Writer/Reader`: little-endian, bounds-checked, rejects non-finite floats, bounds allocations by remaining input
- `Rng` (SplitMix64), `deriveKey()`: platform-independent, independent streams per scope
- `ChopSnapshot`/`ChopInfo`: read-only chop list handed to pattern modules
- `Event`, `NestedPattern`, `FlatEvent`, `flattenInto()`, `validateEvent()`, `writeEvent()/readEvent()`
- `ICandidatePolicy`, `CandidateQuery`, `Decision`: how an optional rule provider guides generation
- `SnapshotStack<T>`: bounded undo/redo
- `EventFx` (filter, cutoff, resonance, glide, crush) on `Event`/`FlatEvent`; serialized only when active (flag bit 32)

Public headers:

- `include/chopfractal/chop_contracts/ids.hpp`
- `include/chopfractal/chop_contracts/time.hpp`
- `include/chopfractal/chop_contracts/result.hpp`
- `include/chopfractal/chop_contracts/bytes.hpp`
- `include/chopfractal/chop_contracts/rng.hpp`
- `include/chopfractal/chop_contracts/chop.hpp`
- `include/chopfractal/chop_contracts/event.hpp`
- `include/chopfractal/chop_contracts/limits.hpp`
- `include/chopfractal/chop_contracts/policy.hpp`
- `include/chopfractal/chop_contracts/undo.hpp`

## Contract

- **Threading:** Value types and pure functions; thread-safe for distinct data. flatten() and the codecs allocate, so they are not real-time safe.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 512 flattened events per pattern, 8 voices, 8 bars, 256 chops, nesting depth 2 by default / 3 hard maximum (see `limits.hpp`)
- flatten() never truncates: exceeding a limit returns `LimitExceeded` with the permitted value in `hint`

## Dependencies

None.

## Build and test

```sh
cmake -S modules/chop_contracts -B build/chop_contracts && cmake --build build/chop_contracts && ctest --test-dir build/chop_contracts
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
