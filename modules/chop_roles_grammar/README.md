# `chop_roles_grammar` — Chop roles and grammar

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

User-defined chop roles and a small serializable rule grammar (forbid, limit repeats, limit count, prefer, require, preserve). Plugs into pattern generation through `ICandidatePolicy` and explains every decision with a trace.

## Public API

- `RoleMap::assignRole`; built-in role IDs plus custom IDs (`[a-z0-9_-]`, up to 32 characters)
- `validateRules(ruleSet)` -> errors and conflict warnings
- `GrammarPolicy::evaluate/isPreserved/required` and `explain(decision)`
- guided `templates::*` rules (preserve first kick, avoid adjacent repeats, require accent in final beat, ...)
- `serialize()` / `deserialize()` (schema 1)

Public headers:

- `include/chopfractal/chop_roles_grammar/grammar.hpp`

## Contract

- **Threading:** Const use is thread-safe; build a new policy per generation. Not real-time safe.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- 64 rules, 256 role assignments
- Conflicts resolve deterministically: priority high to low, then rule id; a prohibition yields only to a strictly higher-priority matching requirement

## Dependencies

- `chop_contracts` ^0.1 — candidate-policy interface, chop IDs, byte codec

## Build and test

```sh
cmake -S modules/chop_roles_grammar -B build/chop_roles_grammar && cmake --build build/chop_roles_grammar && ctest --test-dir build/chop_roles_grammar
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
