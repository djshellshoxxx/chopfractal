# State, presets, automation, and export

**Status:** Draft for review

## Project state

Save and restore:
- Source file reference and/or embedded source data.
- Source metadata and canonical source hash.
- Chop markers, enabled state, labels, and user group assignments.
- Pattern hierarchy, event data, locks, manual edits, and snapshots.
- Generator seed and engine version.
- Plugin parameters and UI state needed to restore the workflow.

State restore must validate all sizes, IDs, ranges, and version fields before publishing data to the audio processor. Malformed or unsupported state should fail safely and keep the plugin silent or in pass-through mode, with an actionable message.

## Source portability

Two strategies are under review:
1. **Reference only:** small project state, but the source can go missing when moved.
2. **Optional embedded audio:** portable state, with a bounded project-size increase.

Recommended direction: default to referencing the source and provide an explicit “Embed source in project state” option with a strict size cap and visible size estimate. If the source is missing, show Locate/Relink and keep markers and pattern intact. Exact cap should be chosen after host testing; it must not allow unbounded state blobs.

The source hash helps detect the wrong file at the same path. Relinking to a different source must require confirmation because chop positions may no longer match.

## Presets and snapshots

- Factory presets are pattern-generation starting points, not bundled audio samples.
- User presets may store settings and optionally a source reference; they must not silently embed large samples.
- Provide A/B snapshots for patterns, locks, and seed.
- Preset changes must be undoable where the host/framework supports it.
- The plugin state format is versioned. Implement migrations for future versions and safe rejection for unsupported newer versions.

## Host automation

Automatable controls should be stable and have permanent IDs from the first code implementation. Recommended automatable controls:
- Variation amount (global and per-level if exposed).
- Density, swing, pattern length, dry/source mix, output gain.
- Effect enable and bounded transform macros.
- Seed should not be continuously automatable in the initial release; expose explicit Generate/Mutate actions in the GUI. If host-triggered generation is later required, use a dedicated trigger parameter with tested edge semantics.

Manual chop markers and individual event edits are project state, not automation parameters.

Each parameter needs a canonical ID, name, unit, range, default, step behavior, normalization mapping, and automation smoothing rule. Never reuse a shipped ID. Publish a parameter manifest in code and tests.

## Audio render and export

First release should support reliable playback in the DAW and project-state recall. If direct export is included:
- Export stereo WAV at selected bit depth and sample rate.
- Render the complete pattern length using the same scheduler and transform path as live playback.
- Include configurable tail handling and normalize off by default.
- Provide a deterministic render option tied to source, state, seed, engine version, and render format.
- Write to a temporary file and atomically rename after success; never overwrite without confirmation.

Direct MIDI export is out of scope for the initial VST3 effect because MIDI-output behavior and host support vary. It can be reconsidered as a later feature with a separate routing and host-compatibility spec.

## Acceptance criteria

- Save/reload restores identical audio for a fixed source and pattern.
- Missing-source recovery retains editable markers and pattern.
- Old supported state versions migrate correctly; malformed state cannot crash or allocate unbounded memory.
- Automation changes are smooth where needed and do not allocate or lock in the audio callback.
- If WAV export ships, rendered output matches offline reference within a defined numeric tolerance and reports all failures.
## Module boundary and migration

Keep state serialization in `state_codec`, separate from host project callbacks and file dialogs. Each stateful module owns the serialization of its own versioned state. The host adapter composes module payloads under a top-level project version; it must not reach into module internals. Parameter IDs and host automation stay in `plugin_host_adapter`, while stable parameter definitions are documented independently from UI control IDs.

**Transfer guide:** copy the codec and each state-owning module listed in its manifest; preserve each module's schema version and migration function; adapt the receiving host's save/restore callbacks to the top-level container; test missing sources, older state, malformed input, and round-trip equivalence. Never strip a dependency's license or attribution files. See `modules/state_codec/MIGRATION.md`.
