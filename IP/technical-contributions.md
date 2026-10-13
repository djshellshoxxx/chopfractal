# Technical contributions (candidate list, high level)

Public repository: descriptions are intentionally non-enabling. "Potentially worth patent review" below is a **preliminary research lead only**, not a conclusion about patentability, novelty, ownership or protection.

All code dates are from `git log` (first commit 2026-10-07; implementation merged 2026-10-08 to 2026-10-09). Conception dates and the human/AI split of ideas are **unknown** (see `ownership-and-provenance.md`).

| ID | Candidate | What it does (practical problem) | Where | Evidence in repo | Classification | Research lead |
|---|---|---|---|---|---|---|
| C1 | Self-similar rhythm generation as nested playable events | Lets a user create grooves where each hit is subdivided by the same short pattern at several scales, producing events the existing engine can zoom, lock, collapse and render | `modules/fractal_rhythm`, `docs/specs/fractal-rhythm.md` | unit tests (leaf timing equals analytic positions, determinism, limits), integration tests, spec | product feature / algorithm | **potentially worth patent review** (see prior-art note: related recursive-subdivision work exists) |
| C2 | Deterministic, seed-reproducible multi-level pattern generation with scope locks and partial regeneration | Reproduce the same pattern from a seed across compilers/platforms while letting users lock parts and mutate the rest | `modules/pattern_engine`, `modules/chop_contracts` (RNG, derived ids), golden hashes in tests | cross-platform golden hash tests passing on GCC, Clang, Apple Clang, MSVC in CI | general-purpose component + workflow | Hypothesis: engineering rigor more than distinct method; low novelty confidence |
| C3 | Nested events with ancestor-bounded source ranges and a single canonical flatten | Zoom into a hit to create child patterns that provably stay inside the parent's time and audio range | `modules/chop_contracts` (`flattenInto`), `modules/recursive_hit_zoom` | tests incl. mutation-checked bound rules | data-model / algorithm | Question |
| C4 | Variation family tree with protected pruning | Keep a bounded history of branches without losing favorites or active lineage | `modules/variation_history` | unit tests | workflow / data structure | likely conventional; low confidence |
| C5 | Rule-based chop selection grammar with explainable decisions | Express "never two kicks in a row"-style constraints that guide generation and explain blocks | `modules/chop_roles_grammar` | tests incl. mutation-checked hard rules | feature / algorithm | Question |
| C6 | Evolve: seeded, scheduled, lock-respecting mutation steps during playback | Beat slowly changes while it plays without touching locked parts and stays reproducible | `modules/evolve`, `composition/src/features.cpp`, plugin midpoint flag | unit + integration + plugin test | workflow | low confidence |
| C7 | Portable, individually transferable module system with enforced boundaries and clean-consumer transfer tests | Reuse any module in another project and prove it builds in an empty project | `tools/check_modules.py`, `tools/transfer_module.py`, `tools/smoke_transfer.sh`, `modules/*/module.json` | CI job `transfer-smoke` | engineering process / tooling | not an IP lead; reuse value |
| C8 | Loop-position arithmetic for host-synced pattern switching (pass-1 extraction) | Tells the host-facing code when a block crosses the loop boundary/midpoint | `modules/plugin_host_adapter/src/loop_position.cpp` | bit-for-bit characterization test | general-purpose utility | not an IP lead; ordinary arithmetic |

## Reuse value (separate from IP)

Highest reuse potential for other projects: `chop_contracts`, `state_codec`, `variation_history`, `wav_export`, `midi_export`, `evolve`, `plugin_host_adapter`, `plugin_ui_adapter` (view models). Evidence: each has a manifest, tests, a standalone build and a passing clean-consumer transfer test. Market demand and commercial value are **not evidenced** here (Hypothesis only).

## Possible trade-secret content

Hypothesis: none of the current source is confidential, because it is public. Future unreleased work (roadmap specs F00-F15 are also public) should be evaluated before publication.
