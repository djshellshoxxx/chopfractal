# ChopFractal build plan

**Status:** Draft for review  
**Scope:** Product-level delivery sequence, not a code task list

## Stage 0 — Repository and toolchain audit

Inspect repository state, licensing, CI, compiler versions, JUCE availability, signing strategy, and target platforms. Record the initial supported OS/architecture matrix. Confirm whether Linux ships in the first beta or remains a later target. Establish format and parameter-ID rules before code is created.

**Exit:** approved engineering baseline, clean local and CI builds of an empty VST3, and a documented dependency policy.

## Stage 1 — Source and pattern model

Implement the source buffer model, chop markers, event/pattern data structures, state versioning, deterministic seed handling, and a command-line or unit-testable generation core.

**Exit:** unit tests demonstrate repeatable patterns, locks, boundaries, undo snapshots, and saved-state round trips without audio rendering.

## Stage 2 — Minimal audio effect

Add the VST3 shell, stereo/mono buses, safe pass-through, source playback voices, scheduler, host transport synchronization, and basic dry/source mix.

**Exit:** valid plugin loads in the initial host set; playback follows host timing; no callback allocation or blocking is detected.

## Stage 3 — Source workflow

Add file import, explicit input capture, transient and grid analysis, waveform view, marker editing, and chop audition.

**Exit:** a producer can import or capture a loop, fix markers, audition chops, save, and reopen the project.

## Stage 4 — Pattern creation and editing

Add Generate, Mutate, seed display, density/grid/swing controls, hierarchy controls, locks, timeline editing, undo/redo, and A/B snapshots.

**Exit:** locked scopes remain unchanged; repeated seeds reproduce results; user can produce and refine variations without losing the source.

## Stage 5 — Event transformations and polished GUI

Add reverse, bounded pitch/resample, retrigger, gate/fades, level/pan, optional filtering if approved, responsive layout, keyboard/accessibility work, meters, and error handling.

**Exit:** transforms are sample-tested, events are inspectable, UI behavior meets usability acceptance tests.

## Stage 6 — State, presets, and export decision

Complete source portability, missing-file recovery, preset behavior, automation manifest, and decide whether WAV export is part of the first release. If export ships, use the same rendering path as live playback.

**Exit:** project recall and automation tests pass; export scope is explicitly accepted or deferred.

## Stage 7 — Compatibility, optimization, and beta

Run the host/OS matrix, plugin validation, audio-thread checks, long-duration tests, installer checks, and performance profiling. Fix defects before optimization; optimize only against measured bottlenecks.

**Exit:** all release gates in the testing spec pass and a beta package can be installed and removed cleanly.

## Stage 8 — Release and follow-up

Publish a versioned build, user guide, known-host notes, changelog, support issue template, and reproducible build instructions. Gather user feedback on pattern usefulness before prioritizing MIDI output, extra buses, time-stretching, standalone mode, or expanded effects.

## Dependency order

Source representation and deterministic pattern semantics must be stable before GUI polish. Host timing and audio safety must be stable before event transformations expand. State portability and compatibility testing must be completed before release packaging.

## Deferred features

- MIDI output / MIDI FX mode.
- Multi-output buses and per-chop stems.
- High-quality time-stretching.
- Automatic semantic source classification.
- Standalone app, sample library, account system, cloud generation.
- Networked or model-based “AI” generation.
