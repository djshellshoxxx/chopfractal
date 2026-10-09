# `evolve` — Evolve scheduler

Version 0.1.0 · API 0.1 · state schema 1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Deterministic scheduler for Evolve mode: decides when the beat should take a mutation step (every N loops), with what seed and amount (optional build-up ramp), and stops itself after repeated failures. It performs no mutation itself; the host application applies each step with its own mutate function.

## Public API

- `Settings{enabled, everyLoops, amount, ramp, rampCeiling, startSeed}` and `valid(settings)`
- `Controller::start/stop/onLoopMidpoint() -> optional<Step>`, `reportSuccess()/reportFailure()`
- `seedForStep()` and `amountForStep()` (pure)

Public headers:

- `include/chopfractal/evolve/evolve.hpp`

## Contract

- **Threading:** Not thread-safe by itself: call from one thread (the plugin calls it from the message thread). Allocation-free after construction.
- **Deterministic:** yes · **Real-time safe:** yes (see threading)

## Limits

- Every 1, 2, 4, 8 or 16 loops; amount, ramp and ceiling in [0, 1]; stops after 3 consecutive failures; never steps faster than once per loop

## Dependencies

None.

## Build and test

```sh
cmake -S modules/evolve -B build/evolve && cmake --build build/evolve && ctest --test-dir build/evolve
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
