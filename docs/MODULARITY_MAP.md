# Modularity map and pass-1 work notes

Scope of this document: where ChopFractal already has clean module boundaries, where it does not, which candidates were considered, what pass 1 extracted, and the evidence that behavior was preserved. Written conservatively: nothing here changes an algorithm, formula, processing order or decision rule.

## 1. Starting point

| Item | Value |
|---|---|
| Base | `origin/main` at `9e4f380` ("Full code audit: bug fixes, GUI wiring fixes and regression tests (#3)") |
| Working branch | `codex/modularization-pass-1` (local only; not pushed, not merged) |
| Why this base | It is the squash-merge of PR #3 whose head (`394dc7c`) had **all 8 CI jobs green** (portability, GCC, Clang, macOS arm64, MSVC, sanitizers, transfer-smoke, Linux plugin build + validator). Later branches only add documentation (PR #4, open). No tags or releases exist. |
| Repository visibility | **Public** (GitHub API `private: false`). Consequence: IP notes in `IP/` stay high-level (section 8). |
| Repo instructions | No `AGENTS.md` or `CONTRIBUTING.md`. Conventions come from `README.md`, `docs/specs/component-portability-protocol.md`, `tools/check_modules.py`, `.github/workflows/ci.yml`. |
| Working tree at start | clean |

## 2. Architecture map

```
                    +-------------------------------+
 host (DAW) ------> | plugin/   JUCE VST3 shell     |  PluginProcessor (audio thread: atomics + mailbox only)
                    |           PluginEditor (GUI)  |  PluginEditor (message thread: draws view models)
                    +---------------+---------------+
                                    | calls
                    +---------------v---------------+
                    | composition/  ProjectSession  |  the only place that wires modules together
                    +---------------+---------------+
        +---------+---------+-------+--------+---------+----------+----------+
        |         |         |       |        |         |          |          |
  pattern_engine  zoom   roles   history  source_chop  evolve  fractal  chop_insight   wav/midi export
        \         |         |       |        |         |          |          /
         +--------+---------+-------+--------+---------+----------+---------+
                                    |
                           chop_contracts  (ids, ticks, Result, byte codec, RNG, nested events, flatten)
        audio_renderer (voices, transforms, effects, offline)   plugin_host_adapter (params, host time)
        plugin_ui_adapter (headless view models)                state_codec (versioned container, CRC)
```

Layers: **portable core** (`modules/*`, C++17, no JUCE or host types, each buildable alone), **composition root** (`composition/`, headless), **product shell** (`plugin/`, JUCE 8.0.15 via FetchContent). Dependency direction is strictly downward; `tools/check_modules.py` enforces it (undeclared dependencies, cycles, JUCE includes in portable code, private-source reach, manifest vs CMake).

- **Threading:** audio thread reads parameter atomics and the renderer's wait-free mailbox only; the message thread owns `ProjectSession` behind `sessionLock_`; decode runs on a worker thread. `Renderer::process` is allocation-free (tested by replacing global `operator new`).
- **Serialization:** `state_codec` container (module id + schema version + CRC) with one payload per module; wrapper `[magic][version][APVTS XML][session blob]` in `PluginProcessor`. Pattern golden hashes pin cross-platform determinism.
- **Public/compatibility surfaces:** VST3 parameter IDs (append-only manifest), saved-state formats, module `public_headers`, the 15 `module.json` manifests.
- **Tests/CI:** 32 ctest targets, plugin shell tests (headless and xvfb), Steinberg validator 47/47, ASan+UBSan, TSan (renderer), clean-consumer transfer smoke for every module.

## 3. Where responsibilities are still mixed

| Location | Mixed concern | Effect |
|---|---|---|
| `plugin/src/PluginEditor.cpp` (815 lines) | layout, drawing, mouse hit-testing, command dispatch, file dialogs | hard to unit test; every UI feature edits one file |
| `plugin/src/PluginProcessor.cpp` | state wrapper framing, file decoding limits, audio-thread arithmetic, status text | pure logic untestable without JUCE |
| `composition/src/project_session.cpp` (+ `features.cpp`) | many unrelated session operations in one class | large surface; already split once (features.cpp) |

## 4. Candidate modules, ranked

| # | Candidate | Cohesion | Coupling | Reuse | Testability gain | Risk | Decision |
|---|---|---|---|---|---|---|---|
| 1 | **Loop-position arithmetic** (boundary / midpoint / playhead from `processBlock`) | high | none (pure) | any host-synced pattern plugin | high (was JUCE-only) | very low | **Extracted in pass 1** |
| 2 | Pattern-lane hit-testing and layout math from `PluginEditor::mouseDown` / `paint` | high | low | medium | high | low | deferred (specified in roadmap F00/F02; touches the editor's hot file) |
| 3 | State wrapper framing (`magic/version/xml/blob`) | high | JUCE streams | low | medium | medium (state compatibility) | deferred (F01 changes the format; do not split first) |
| 4 | Source file decoding + limits (`decodeFile`) | medium | JUCE formats | low (JUCE-bound) | low | medium | rejected: it is a JUCE adapter by nature |
| 5 | Editor class split into panels | medium | high | none | high | medium-high (UI behavior) | deferred to F00 (own PR; GUI harness as safety net) |
| 6 | `ProjectSession` split by feature | medium | high | low | medium | medium | deferred; continue the `features.cpp` pattern |

Rationale for scope: the portable core is already modular (15 modules, enforced). The best small, genuinely reusable, behavior-neutral extraction is the transport arithmetic: it is real-time code, it had no tests of its own, and a second plugin using the same loop-synced approach could reuse it.

## 5. Pass-1 extraction: `plugin_host_adapter::computeLoopPosition`

| | |
|---|---|
| Module | `modules/plugin_host_adapter` (existing; one header + one source added) |
| Files | new `include/chopfractal/plugin_host_adapter/loop_position.hpp`, `src/loop_position.cpp`; tests appended to `tests/test_host_adapter.cpp`; `module.json` public headers/API updated; generated docs refreshed |
| Old location | inline in `ChopFractalProcessor::processBlock` (`plugin/src/PluginProcessor.cpp`) |
| New call chain | `processBlock` -> `host::computeLoopPosition(tt.block, frames, sampleRate_, patternQuarters_)` -> three atomic stores (`boundaryFlag_`, `midpointFlag_`, `playheadQuarters_`) |
| Purpose | Given one host block, report whether it crosses the pattern's loop boundary, its midpoint, and the playhead position inside the loop (quarter notes). |
| Non-goals | Does not read or write any state; does not decide what to do about a crossing (the processor/message thread does). |
| Contract | Pure; no allocation, locks or I/O (audio-thread safe). Units: ppq in quarter notes, bpm, Hz. Reports only when `playing && positionValid && patternQuarters > 0`; otherwise `playheadQuarters = -1`. |
| Method (unchanged) | `end = ppq + frames*bpm/(60*sr)`; boundary if `floor(ppq/q) != floor(end/q)`; midpoint if `floor((ppq-q/2)/q) != floor((end-q/2)/q)`; playhead `fmod(ppq, q)` wrapped to `[0, q)`. Same operations in the same order as the original inline code. |
| Build alone | `cmake -S modules/plugin_host_adapter -B build/pha && cmake --build build/pha && ctest --test-dir build/pha` (depends on `audio_renderer` for `TransportBlock`) |
| Migrate elsewhere | `python3 tools/transfer_module.py plugin_host_adapter <dest>` copies the module and `audio_renderer`/`chop_contracts`; see the module's `MIGRATION.md`. License: all rights reserved, see `docs/LICENSING.md`. |
| Diagnostics | None added. A runtime flag was judged unnecessary: the characterization test is the migration comparison and is permanent. |
| Known limitation | Callers must pass the sample rate the audio thread actually uses; `patternQuarters` must come from a consistent playback snapshot (the processor keeps both in atomics). |

### Before / after wiring

Before: `processBlock` computed the three values inline and stored them. After: identical stores, values come from the function. No other caller exists. `pollAudioFlags()` (message thread) and the Orbit View playhead are unchanged consumers of the same atomics.

## 6. Behavior-preservation evidence

| Check | Result |
|---|---|
| Baseline before changes (`build/all`, GCC, warnings as errors): `ctest` | 32/32 passed |
| Baseline plugin shell tests, headless and `xvfb-run` | 13/13 and 13/13 |
| Baseline VST3 validator (`tools/validate_vst3.sh`) | 47 passed, 0 failed |
| **Characterization:** extracted function vs a verbatim copy of the original expressions, bit-for-bit (`memcmp` on the double, equality on flags) over 77,760 input combinations (playing/valid x 15 ppq values incl. negative, exact boundaries, 1e9 x 6 tempos x 4 rates x 9 loop lengths incl. <= 0 x 6 block sizes) | all equal |
| Plugin test `evolve_steps_the_pattern_at_the_loop_midpoint_...` (exercises the rewired processor path end to end) | passes after rewiring |
| After rewiring: plugin shell tests headless / xvfb, validator | 13/13, 13/13, 47/47 |
| Full verification set after changes (GCC, Clang, Release, ASan+UBSan with `halt_on_error`, TSan on renderer, `check_modules.py`, `check_includes.py`, clean-consumer transfer smoke) | see section 7 |

Not verified: behavior inside real DAW hosts; macOS/Windows plugin builds (only the portable modules are built there by CI).

## 7. Final verification log (run on the finished branch, 2026-10-10)

| Command | Result |
|---|---|
| `cmake -S . -B build/all -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON && cmake --build build/all -j8 && ctest --test-dir build/all` (GCC) | 32/32 passed |
| same with `CC=clang CXX=clang++` (`build/clang`) | 32/32 passed |
| same with `-DCMAKE_BUILD_TYPE=Release` (`build/rel`) | 32/32 passed |
| same with `-DCHOPFRACTAL_SANITIZE=ON` and `UBSAN_OPTIONS=halt_on_error=1` (`build/asan`) | 32/32 passed, 0 "runtime error" lines |
| `cmake -S modules/audio_renderer -B build/tsan` with `-fsanitize=thread`, build + ctest | passed (renderer code unchanged by this pass) |
| `python3 tools/check_modules.py && python3 tools/check_includes.py` | passed (15 modules, 0 missing includes) |
| `tools/smoke_transfer.sh` (each module in an empty consumer project) | 15/15 PASS, including `plugin_host_adapter` |
| Plugin: build `ChopFractal_VST3` + `ChopFractalShellTests`; run headless and `xvfb-run` | 13/13 and 13/13 |
| `tools/validate_vst3.sh .../ChopFractal.vst3` | 47 passed, 0 failed |

Not run: GitHub CI for this branch (not pushed, per instructions); macOS and Windows builds; real DAW hosts.

## 7a. Maintainer quick reference

- **Entry points and data flow:** host -> `PluginProcessor::processBlock` (audio thread; atomics + `Renderer::process`) ; GUI -> `PluginEditor` -> `ProjectSession` (message thread, `sessionLock_`) -> portable modules -> immutable `Playback` published through the lock-free mailbox to the renderer.
- **Build/test:** see the commands in section 7; one module alone: `cmake -S modules/<id> -B build/<id> && cmake --build build/<id> && ctest --test-dir build/<id>`.
- **Troubleshooting:** a failing golden hash means generation output changed (an engine-version decision, never a test edit); use `UBSAN_OPTIONS=halt_on_error=1` for sanitizer runs; `tools/check_modules.py` explains boundary violations.
- **Configuration and logs:** no log files or user config are written. Plugin state lives in the host project (`getStateInformation`); status text is shown in the editor. Diagnostics: none added (see section 5).
- **Add or change a module safely:** create `modules/<id>/` with `CMakeLists.txt` (standalone-capable), `module.json`, tests, an example; declare dependencies in both; run `tools/gen_module_docs.py`, `check_modules.py`, `check_includes.py`, `smoke_transfer.sh`.
- **Invariants that must stay true:** (1) portable modules include no JUCE/host headers; (2) `process()` allocates and locks nothing; (3) same seed and settings give identical output on every compiler (golden hashes `2397844821793184813`, `12084884237330071892`, `13710596758976548913`, `18237151833431327128`); (4) new random draws are appended to existing per-event streams; (5) parameter IDs are append-only; (6) state formats stay backward compatible; (7) the loop-position arithmetic is bit-identical to the characterization oracle.

## 8. Public-repository note

This repository is public. `IP/` therefore records only high-level, non-enabling descriptions and process questions. Detailed invention disclosures must be written in a private location chosen by the owner; do not add them to this repository.

## 9. Deferred work and recommended next small step

1. Extract pattern-lane hit-testing/layout math into `plugin_ui_adapter` with characterization tests of `mouseDown` selection (roadmap F00/F02 prerequisite).
2. Then split `PluginEditor.cpp` into panels (F00), one panel per commit.
3. Do not touch state framing until F01 defines the v2 format.
