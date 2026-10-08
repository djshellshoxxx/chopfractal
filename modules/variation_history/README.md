# `variation_history` — Variation family tree

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Generic immutable snapshot branch graph. Stores opaque payload bytes plus metadata; it knows nothing about audio, UI, hosts, or ChopFractal. Usable for presets, scenes, or any branching creative history.

## Public API

- `VariationTree::addSnapshot(parent, payload, metadata)` -> id (+ any pruned ids)
- `getSnapshot(id)` verifies the payload hash (Corrupt error names the node)
- `listChildren`, `listAll`, `ancestors`, `setActive`, `rename`, `setFavorite`
- `deleteBranch(id, policy)`; `compare(a, b, diffFn)`
- `serialize()` / `deserialize()` (schema 1)

Public headers:

- `include/chopfractal/variation_history/history.hpp`
- `include/chopfractal/variation_history/result.hpp`

## Contract

- **Threading:** Not thread-safe: own it from one thread. Not real-time safe.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 64 nodes by default, 128 hard maximum; 1 MiB payload per node by default
- When full, the oldest unprotected leaf is pruned and reported; the active node, its ancestors and favorites are never pruned, and a full tree of protected nodes refuses the new snapshot

## Dependencies

None.

## Build and test

```sh
cmake -S modules/variation_history -B build/variation_history && cmake --build build/variation_history && ctest --test-dir build/variation_history
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-TBD` — the root license policy is still an open decision. Keep any `LICENSE`/`NOTICE` files when copying.
