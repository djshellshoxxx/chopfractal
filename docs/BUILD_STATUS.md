# ChopFractal build status

Last updated with the implementation pass that added all code (the repository previously held specifications only).

## Where the build stands

| Stage (from the build plan) | State | Notes |
|---|---|---|
| 0 Toolchain audit | **Done** | C++17; GCC 13, Clang 18, Apple Clang (macOS arm64) and MSVC 2022 all build and pass the full suite in CI |
| 1 Source and pattern model | **Done** | Deterministic generation, locks, undo, state round-trip, no audio needed to test it |
| 2 Minimal audio effect | **Done on Linux** | VST3 shell builds, passes the official Steinberg validator (47/47), pass-through before a source exists |
| 3 Source workflow | **Done** | Import (WAV verified; AIFF and other JUCE basic formats are untested), transient and grid analysis, marker editing, audition work, input capture (Arm / Capture / End Capture / Keep, `input_capture` module, F12) |
| 4 Pattern creation and editing | **Done (model); partial (UI)** | Generate, Mutate, locks, seed, undo/redo, A/B, duplicate bar, restore source order are all in the model; the editor exposes the main ones |
| 5 Event transforms and polished GUI | **Transforms done; GUI is a first pass** | Reverse, pitch (resample), retrigger, gate/fades, level, pan are sample-tested. No filter. Editor is functional but not polished |
| 6 State, presets, export | **Done** | Save/restore, missing-source recovery, optional embedding, WAV export, Producer Kit, six factory presets plus user presets, MIDI/WAV drag-out |
| 7 Compatibility, optimization, beta | **Not started** | Needs real DAWs, macOS/Windows *plugin* builds (the portable modules already build there), performance thresholds, installers |
| 8 Release | **Not started** | |

Modular plan: Stages A to D are complete; Stage E (portability and release) is complete for portability and not started for packaging.

## What exists

Sixteen portable C++17 modules (no JUCE, no host types), a headless composition root, and a thin JUCE shell.

| Module | Role | Test cases |
|---|---|---|
| `chop_contracts` | typed IDs, ticks, results, byte codec, RNG, nested events (with per-event effects) + `flatten`, policy interface | 10 |
| `state_codec` | versioned project container, CRC, migrations | 5 |
| `variation_history` | generic branch graph with protected pruning | 7 |
| `source_chop` | markers, undo, proposals, transient and grid analysis | 14 |
| `pattern_engine` | generate, mutate, locks, edits, effects, `setBarEvents`, flatten, state, session | 23 |
| `recursive_hit_zoom` | nested child patterns inside a hit's time and source bounds | 12 |
| `chop_roles_grammar` | roles, serializable rules, explainable policy | 9 |
| `wav_export` | deterministic WAV encode/decode, dither, normalize, atomic file write | 6 |
| `midi_export` | deterministic Standard MIDI File encode/decode | 4 |
| `evolve` | Evolve scheduler (cadence, seeds, ramp, failure stop) | 4 |
| `fractal_rhythm` | self-similar motif groove as nested events | 8 |
| `chop_insight` | chop features, role suggestions, loop/tempo suggestions | 4 |
| `audio_renderer` | scheduler, voices, transforms, filter/glide/crunch, lock-free handoff, offline render | 23 |
| `plugin_host_adapter` | parameter manifest (version 2), host-time validation, bus rules | 6 |
| `plugin_ui_adapter` | headless view models including the Orbit View | 8 |
| `composition` | `ProjectSession`: the end-to-end flow, Fractal, Evolve, Smart Setup, exports | 18 |
| `plugin/` (JUCE) | processor, editor, state glue | 11 |

Features added in the second round (each has a spec in `docs/specs/`): **Fractal Rhythm** (unique), **Evolve** (fun), **Producer Kit + WAV export** (valuable), **Smart Setup** (usable), **Orbit View** (visual), and three effects (**Filter, Tape Glide, Crunch**).

## Verification that was actually run

- Full tree builds and passes under **GCC 13** and **Clang 18** with warnings as errors (32 ctest targets).
- **AddressSanitizer + UBSan** over everything, including corruption and hostile-state sweeps. Clean.
- **ThreadSanitizer** on the real-time renderer and its mailbox (threaded stress). Clean.
- `process()` is proven allocation-free by replacing global `operator new` in the test.
- Output is bit-identical for host block sizes 1, 7, 32 ... 2048, 333.
- Cross-platform determinism: the pinned pattern hashes match on GCC and Clang (x86-64), Apple Clang (arm64) and MSVC, confirmed by CI.
- **GitHub Actions CI is green on all 8 jobs** (portability, include audit, gcc, clang, macOS arm64, MSVC, ASan+UBSan+TSan, clean-consumer transfer, Linux plugin build + shell tests + official validator). CI also caught real portability bugs that local GCC builds hid (a missing `<algorithm>`, an optimizer-only allocator warning); both are fixed and `tools/check_includes.py` now prevents the class.
- **Portability gate** (`tools/check_modules.py`): undeclared dependencies, JUCE/VST includes in portable code, private-source reach, CMake vs manifest, cycles, stale docs. The gate was itself tested by injecting each violation.
- **Clean-consumer transfer test** (`tools/smoke_transfer.sh`): each module plus only its declared dependencies builds and passes in an empty project (15/15).
- **VST3 validation** with Steinberg's validator 3.8.0: 47 tests passed, 0 failed. (It caught a real defect, an unnamed program, which is fixed and has a regression test.)
- The editor was rendered under a virtual display and inspected; it draws the waveform, markers, pattern grid, history tree, Orbit View, feature controls and status line.
- Second-round verification (this change set): GCC, Clang, GCC Release (all warnings as errors), ASan+UBSan, TSan on the renderer, portability and include gates, clean-consumer transfer of all 15 modules, plugin shell tests headless and under xvfb (11/11), and the VST3 validator (47/47). The pinned golden pattern hashes are unchanged, proving that projects that do not use the new features generate and play exactly as before.

Defects found and fixed during integration (each now has a test): derived child IDs were rejected by pattern validation; hard grammar rules could be bypassed by copied beats and bars (the test was mutation-checked: it fails without the fix); required chops were not enforced on copied beats; a mismatch between test allocator replacement and ASan; an unnamed VST3 program.

## Decisions taken on open items (change them if you disagree)

| Item | Chosen default |
|---|---|
| Language / compilers | C++17; GCC, Clang, MSVC |
| Musical time | 960 ticks per quarter note; positions are exact integers |
| Event limits | 512 per pattern, 8 voices, 8 bars, 256 chops, nesting depth 2 (hard maximum 3) |
| Source limits | 60 s and 100 MB decoded; mono or stereo; markers are integer frames in the source's own rate |
| Embedded audio cap | 128 MiB (about 12 minutes of 44.1 kHz stereo float), off by default; adjustable per session with `setEmbeddedSourceCap()`; codec ceiling 256 MiB |
| License | All rights reserved (`LICENSE`, `LICENSE_ID`); change with `python3 tools/set_license.py <SPDX>`; see `docs/LICENSING.md` |
| WAV export | In the first release: 16/24-bit PCM or 32-bit float, 44.1 to 192 kHz, deterministic, atomic writes, never overwrites without confirmation |
| History | 64 snapshots (hard max 128), oldest unprotected leaf pruned and reported |
| Pan / mono | balance law (center is unity); mono output is (L+R)/2; linear interpolation |
| Retrigger | expanded into decaying sub-events (x0.8 per hit) when playback is prepared |
| Preview with stopped transport | audition plays; the generated pattern is silent when the transport is stopped unless Dry/Source is raised |
| Seed | never an automatable parameter; Generate/Mutate are explicit actions |
| Automation parameters | `variation_amount`, `density`, `swing`, `pattern_bars`, `dry_mix`, `output_gain_db`, `effect_enable`, `allow_reverse`, `allow_pitch`, `allow_retrigger`; manifest version 2 appends `allow_filter`, `allow_glide`, `allow_crunch`, `fx_intensity`; version 3 appends `chaos`. IDs are permanent and append-only |
| JUCE | 8.0.15 via FetchContent (`-DCHOPFRACTAL_JUCE_DIR` for a local checkout) |

## Needs your decision

1. **Supported platforms for the first beta** (Linux builds the plugin and passes the validator in CI; the portable modules and their tests pass on macOS arm64 and Windows MSVC, but the JUCE plugin is not yet built there) and whether Linux ships.
2. **Before distributing a plugin:** the JUCE license route (AGPLv3, or a commercial JUCE license for a closed-source product) and the VST3 SDK terms. See `docs/LICENSING.md`. Settled for now: the project is all rights reserved.

## Code audit (third round)

Three independent read-only audits (core logic, audio/export/feature modules, plugin shell and GUI) plus a button-driving GUI test found and fixed: a failed marker edit that wedged the session (now transactional, and zoomed content stranded by a deleted chop is dropped); undo, redo and A/B recall restoring patterns that reference deleted chops; roles lost when a chop is disabled; per-event locks bypassed by bar replacement and bar duplication; signed overflow in event timing; grammar labels that saved but could not be loaded; duplicate marker IDs and a zero id counter in saved state; a history config that could write unreadable state; voice-pool exhaustion hard-cutting a sounding voice, and newest-voice stealing; MIDI decoder accepting invalid tempo, meter, running status and zero-length notes (and a shift-exponent bug); non-atomic WAV overwrite and false clip reports at 0 dB; stale waveform after loading a new source; embed flag, manual tempo not persisted; no Locate Source for a missing audio file; use-after-free risk in menu callbacks; Lock Bar acting on bar 0 with nothing selected; Evolve toggle out of sync with the session; repaint 10 times a second while idle. Each has a regression test.

Not fixed (documented): marker trim validation for disabled markers, history tree layout recursion on very long chains, seed field and motif not persisted, A/B snapshots not saved with the project, export runs on the message thread, no meters.

## Pass 3 (pre-beta)

Four features, specified in `docs/specs/pass3/pass3-features.md`, implemented and tested: **Chaos** macro + Reroll (fun), **MIDI/WAV drag-out** (valuable), **presets** (usable, new module `preset_store`), **direct hit editing** with Shift+click markers/hits, drag move/resize, Delete/M/D keys (the essential missing editor feature). The host manifest is version 3 (`chaos` appended). All golden pattern hashes are unchanged.

Builds: Linux VST3 (validator-checked) and **CLAP** (via clap-juce-extensions, pinned commit) and a **Windows (MSVC) VST3 + CLAP** job in CI that uploads pre-beta artifacts. The Windows plugin is verified only by that CI job; it has not been run in a DAW. CLAP has not been run through clap-validator or a CLAP host.

## Audit round 4 (after pass 3)

Fixed (each with a test or CI check where practical): Evolve now applies the live Chaos value (and records each step's own seed); the history tree kept 64 snapshots then silently stopped recording a straight chain, now it splices out the oldest node and tells the user; a queued variation switch could overwrite a newer edit; quantized switches and Evolve never fired for hosts without a play position (a local clock now drives them); a use-after-free window in the renderer after a pattern switch; detached file-decode threads outliving the plugin, out-of-order loads, and a state-restore from a UNC path; dragged WAV/MIDI files lived in a purged temp folder (now `<user data>/ChopFractal/Dropped`, 90 days), and the WAV used default export options; non-ASCII Windows export paths; Windows reserved and non-Latin preset names; macOS preset folder; dither draw order and hash-set iteration order (compiler-dependent output); FMA contraction (`-ffp-contract=off`); NaN/Inf and silence handling in WAV code; roles of disabled chops lost on load; stale Smart Setup suggestions; narrow hits that could only be resized; history tree clipping; keyboard focus in hosts; arrow-key nudge/resize; repaint cost while playing; CRC speed; a changed FX intensity lost on save; plugin sources now compile with `-Wall -Wextra` / `/W4`; JUCE commit check; macOS plugin CI job; versioned, checksummed artifacts with notices.

Open (not fixed, by design or cost): the event cap counts top-level hits while flatten counts children (a near-full zoomed pattern can fail to mutate); policy-driven generation is O(events x chops x events) and slow with large rule sets; Fractal Rhythm ignores repeat/required grammar rules; child-pattern locks can only come from a loaded file and are not honored by every edit; mailbox retention grows if a track is bypassed while the UI keeps editing; per-sample DSP micro-optimizations; no accessibility handler for the custom-drawn views; export runs on the message thread; recall of A/B snapshots can fail after chops change; WAV import of WAVE_FORMAT_EXTENSIBLE (the plugin imports through JUCE, so only the tool path is affected).

## Audit round 5

Fixed: a Require rule naming a disabled chop broke every Generate/Mutate/Evolve; two requirements overwrote each other; locked zoom trees were bypassed by Fractal, bar duplication, nested Zoom/Collapse and Clear locks (now recursive); Evolve on a fully locked pattern flooded undo (it now counts as a failed step and stops); the event cap counts zoomed children; first-block output gain/dry started from defaults (exports 12 dB too loud at -12 dB); retriggered hits near the loop end depended on host block size; exact offline lengths gained a frame; short regions were squashed by the end fade; loop/midpoint events could be missed on a block edge; zoom children ignored "Reset" for effects; hat classification skipped its zero-crossing rule; 999 BPM MIDI could not be decoded; several integer-overflow/hostile-counter hazards; host meter was ignored when generating; snapping could place a hit in a 1-tick sliver; hand edits now create family-tree variations; status line repaint; Ctrl/Cmd/Alt shortcuts no longer swallowed; dropped files are no longer auto-deleted; export buttons disabled when the source is missing; ".loop" names no longer get WAV data under the wrong extension; stale file loads cannot overwrite a restored project; newer-version project gives a message; hidden temp preset files are ignored; the GUI tests fail (not skip) in the xvfb CI job and now also run on Windows/macOS; transfer smoke test can no longer pass with zero modules; CI artifacts must exist and jobs have timeouts.

Open: the family tree is not moved by Undo/Redo; Mutate can break "at most N" grammar rules using later events; Fractal ignores repeat/required rules; FirstInBar means the first event of any chop; unknown future project modules are dropped on re-save; policy-driven generation is slow with big rule sets; Detect (Replace) removes manual markers; Smart Setup only detects when no marker exists; host effect tail (some hosts sleep the plugin on silent input); loop-boundary swaps can lag by up to one 33 ms timer tick and flags coalesce in offline bounces; voices are hard-cut when effect_enable toggles; a plugin ASan/TSan CI job does not exist yet.

## Known gaps

- **Input capture** (F12) is implemented with an input meter and latching clip indicator; there is no Discard-on-Arm confirmation (Arm is disabled until the capture is kept or discarded). Not run in a real DAW.
- **Editor polish:** no marker dragging, snap controls, detection preview/Merge dialog, rule editor, resizable layout, or full keyboard navigation. (Done: A/B snapshot buttons, a role setter for the selected hit's chop, and a right-click menu on history nodes for favorite, rename, compare and delete.) The view models and session API for all of these exist and are tested; the JUCE controls do not.
- Restoring a reference-only project decodes its audio file on the calling thread (the spec wants a worker thread). Embedded projects avoid file access.
- Not run: real DAW hosts (REAPER, FL Studio, Live, Cubase), macOS and Windows *plugin* builds, MSVC `/W4` warnings-as-errors (warnings are reported but not fatal there), installers and signing, performance thresholds on a reference machine, long-duration soak tests, mono-host behavior in a real host.
- Export runs on the message thread (a few seconds for very long exports); direct MIDI output from the plugin is out of scope (the Producer Kit writes a MIDI *file*).
- The Orbit View, Smart Setup, Fractal, Evolve and export controls are functional but basic (no motif presets, no export progress bar, fixed Fractal accent decay).

## Input capture (F12) and audit round 6

New portable module `input_capture` (11 tests, TSan-clean, allocation-free `process`), composition `loadCapturedSource` / `sourceNeedsEmbed` / `sourceData`, plugin `CaptureController` + `CapturePanel`, forced embedding of pathless audio, Undo Keep. Shell tests 31/31 (xvfb), all 36 ctest targets pass, portability and include gates pass.
