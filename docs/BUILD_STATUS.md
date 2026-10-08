# ChopFractal build status

Last updated with the implementation pass that added all code (the repository previously held specifications only).

## Where the build stands

| Stage (from the build plan) | State | Notes |
|---|---|---|
| 0 Toolchain audit | **Done** | C++17; GCC 13, Clang 18, Apple Clang (macOS arm64) and MSVC 2022 all build and pass the full suite in CI |
| 1 Source and pattern model | **Done** | Deterministic generation, locks, undo, state round-trip, no audio needed to test it |
| 2 Minimal audio effect | **Done on Linux** | VST3 shell builds, passes the official Steinberg validator (47/47), pass-through before a source exists |
| 3 Source workflow | **Mostly done** | Import (WAV verified; AIFF and other JUCE basic formats are untested), transient and grid analysis, marker editing, audition work. **Not done: input capture** (Arm / Capture / Stop / Keep) |
| 4 Pattern creation and editing | **Done (model); partial (UI)** | Generate, Mutate, locks, seed, undo/redo, A/B, duplicate bar, restore source order are all in the model; the editor exposes the main ones |
| 5 Event transforms and polished GUI | **Transforms done; GUI is a first pass** | Reverse, pitch (resample), retrigger, gate/fades, level, pan are sample-tested. No filter. Editor is functional but not polished |
| 6 State, presets, export | **State done; presets and WAV export not done** | Save/restore, missing-source recovery, optional embedding. Offline renderer exists and shares the live path, but there is no WAV writer or export UI, and no factory presets |
| 7 Compatibility, optimization, beta | **Not started** | Needs real DAWs, macOS/Windows *plugin* builds (the portable modules already build there), performance thresholds, installers |
| 8 Release | **Not started** | |

Modular plan: Stages A to D are complete; Stage E (portability and release) is complete for portability and not started for packaging.

## What exists

Ten portable C++17 modules (no JUCE, no host types), a headless composition root, and a thin JUCE shell.

| Module | Role | Test cases |
|---|---|---|
| `chop_contracts` | typed IDs, ticks, results, byte codec, RNG, nested events + `flatten`, policy interface | 10 |
| `state_codec` | versioned project container, CRC, migrations | 5 |
| `variation_history` | generic branch graph with protected pruning | 7 |
| `source_chop` | markers, undo, proposals, transient and grid analysis | 14 |
| `pattern_engine` | generate, mutate, locks, edits, flatten, state, session | 20 |
| `recursive_hit_zoom` | nested child patterns inside a hit's time and source bounds | 12 |
| `chop_roles_grammar` | roles, serializable rules, explainable policy | 9 |
| `audio_renderer` | scheduler, voices, transforms, lock-free handoff, offline render | 19 |
| `plugin_host_adapter` | parameter manifest, host-time validation, bus rules | 6 |
| `plugin_ui_adapter` | headless view models | 6 |
| `composition` | `ProjectSession`: the end-to-end flow | 10 |
| `plugin/` (JUCE) | processor, editor, state glue | 9 |

## Verification that was actually run

- Full tree builds and passes under **GCC 13** and **Clang 18** with warnings as errors (22 ctest targets).
- **AddressSanitizer + UBSan** over everything, including corruption and hostile-state sweeps. Clean.
- **ThreadSanitizer** on the real-time renderer and its mailbox (threaded stress). Clean.
- `process()` is proven allocation-free by replacing global `operator new` in the test.
- Output is bit-identical for host block sizes 1, 7, 32 ... 2048, 333.
- Cross-platform determinism: the pinned pattern hashes match on GCC and Clang (x86-64), Apple Clang (arm64) and MSVC, confirmed by CI.
- **GitHub Actions CI is green on all 8 jobs** (portability, include audit, gcc, clang, macOS arm64, MSVC, ASan+UBSan+TSan, clean-consumer transfer, Linux plugin build + shell tests + official validator). CI also caught real portability bugs that local GCC builds hid (a missing `<algorithm>`, an optimizer-only allocator warning); both are fixed and `tools/check_includes.py` now prevents the class.
- **Portability gate** (`tools/check_modules.py`): undeclared dependencies, JUCE/VST includes in portable code, private-source reach, CMake vs manifest, cycles, stale docs. The gate was itself tested by injecting each violation.
- **Clean-consumer transfer test** (`tools/smoke_transfer.sh`): each module plus only its declared dependencies builds and passes in an empty project (10/10).
- **VST3 validation** with Steinberg's validator 3.8.0: 47 tests passed, 0 failed. (It caught a real defect, an unnamed program, which is fixed and has a regression test.)
- The editor was rendered under a virtual display and inspected; it draws the waveform, markers, pattern grid, history tree, controls and status line.

Defects found and fixed during integration (each now has a test): derived child IDs were rejected by pattern validation; hard grammar rules could be bypassed by copied beats and bars (the test was mutation-checked: it fails without the fix); required chops were not enforced on copied beats; a mismatch between test allocator replacement and ASan; an unnamed VST3 program.

## Decisions taken on open items (change them if you disagree)

| Item | Chosen default |
|---|---|
| Language / compilers | C++17; GCC, Clang, MSVC |
| Musical time | 960 ticks per quarter note; positions are exact integers |
| Event limits | 512 per pattern, 8 voices, 8 bars, 256 chops, nesting depth 2 (hard maximum 3) |
| Source limits | 60 s and 100 MB decoded; mono or stereo; markers are integer frames in the source's own rate |
| Embedded audio cap | 32 MiB, off by default (visible size estimate via `embeddedSourceBytes()`); project state hard cap 64 MiB |
| History | 64 snapshots (hard max 128), oldest unprotected leaf pruned and reported |
| Pan / mono | balance law (center is unity); mono output is (L+R)/2; linear interpolation |
| Retrigger | expanded into decaying sub-events (x0.8 per hit) when playback is prepared |
| Preview with stopped transport | audition plays; the generated pattern is silent when the transport is stopped unless Dry/Source is raised |
| Seed | never an automatable parameter; Generate/Mutate are explicit actions |
| Automation parameters | `variation_amount`, `density`, `swing`, `pattern_bars`, `dry_mix`, `output_gain_db`, `effect_enable`, `allow_reverse`, `allow_pitch`, `allow_retrigger`; IDs are permanent and append-only |
| JUCE | 8.0.15 via FetchContent (`-DCHOPFRACTAL_JUCE_DIR` for a local checkout) |

## Needs your decision

1. **License.** The project license is not chosen (`module.json` says `LicenseRef-ChopFractal-TBD`). JUCE is AGPLv3 or commercial and bundles the VST3 SDK; distributing a built plugin requires settling this. The portable modules do not depend on JUCE, so they can carry whatever license you pick.
2. **Supported platforms for the first beta** (Linux builds the plugin and passes the validator in CI; the portable modules and their tests pass on macOS arm64 and Windows MSVC, but the JUCE plugin is not yet built there) and whether Linux ships.
3. **Whether WAV export is in the first release** (the spec asks for this decision at Stage 6). The renderer is ready for it.
4. Embedded-source cap and default (currently 32 MiB, off).

## Known gaps

- **Input capture** is not implemented (the spec's explicit Arm/Capture/Stop/Keep flow); only file import exists.
- **Editor polish:** no marker dragging, snap controls, detection preview/Merge dialog, role and rule panel, history rename/favorite/compare UI, A/B buttons, resizable layout, or full keyboard navigation. The view models and session API for all of these exist and are tested; the JUCE controls do not.
- Restoring a reference-only project decodes its audio file on the calling thread (the spec wants a worker thread). Embedded projects avoid file access.
- Not run: real DAW hosts (REAPER, FL Studio, Live, Cubase), macOS and Windows *plugin* builds, MSVC `/W4` warnings-as-errors (warnings are reported but not fatal there), installers and signing, performance thresholds on a reference machine, long-duration soak tests, mono-host behavior in a real host.
- No factory presets, no per-event filter, no WAV export writer.
- No LICENSE or NOTICE files yet (see decision 1).
