# Pattern generation engine

**Status:** Draft for review

## Purpose

Generate structured, repeatable patterns from the available chop set. The engine must create variation that is bounded, explainable, and editable. It is not a black-box loop generator.

## Pattern representation

A pattern contains one or more bars of events. Each event stores:

- Stable event ID and source chop ID.
- Start position and duration in musical ticks or a canonical beat-relative representation.
- Velocity/level, pan, pitch offset, reverse, and optional retrigger count.
- Probability and enabled state.
- Parent scope ID for bar/beat grouping and lock inheritance.

The pattern hierarchy is Phrase → Bar → Beat group → Event. A generated pattern may use repeated motifs between bars. Each node stores the generation rule and any user override needed to reproduce its children.

## Generation model

Generation has two operations:

1. **Generate:** create an initial pattern from source chops and current constraints.
2. **Mutate:** preserve locked scopes and modify only unlocked scopes according to variation settings.

The same source identity, settings, engine version, and seed must produce the same generated pattern. Seed advancement is an explicit user action. Do not change a pattern merely because the editor opens or playback starts.

At each hierarchy level, allowed operations include:
- Repeat, omit, or select a child motif.
- Subdivide an event into allowed grid cells.
- Shift timing within a bounded range.
- Select an alternate chop from the same user-defined group, if grouping exists.
- Apply enabled event transforms with bounded values.

Use a stable, specified pseudo-random generator and deterministic operation order. Engine version is part of saved state so future algorithm changes do not silently rewrite old patterns.

## Musical constraints

Required controls:
- Pattern length: 1, 2, 4, or 8 bars.
- Grid: straight and triplet options from whole-note through 1/32-note; allow a smaller subdivision only if performance and UI remain clear.
- Density: target event density with a hard maximum event count.
- Variation by hierarchy level: phrase, bar, beat, event.
- Swing: 0–100% applied only to eligible off-grid subdivisions.
- Seed: integer display with randomize, copy, and restore actions.
- Locks: event, beat group, bar, or phrase.
- Reset to source order and clear all locks.

Recommended safety limits for the first release:
- At most 512 scheduled events per 8-bar pattern.
- At most 8 simultaneous playback voices.
- No zero-length events; no event starts beyond the pattern boundary unless wrap is enabled.
- Retrigger count and micro-shift ranges have finite bounds and are visible to the user.

## Pattern editing

Users can edit event placement, source chop, and transforms directly in the timeline. A manual edit marks the affected node as user-owned. Mutation respects user-owned/locked nodes unless the user explicitly chooses “include edited events.”

Provide:
- Generate, Mutate, Undo, Redo, Clear, and Restore Source actions.
- A/B or snapshot slots for comparing variations.
- “Keep this bar” and “mutate this bar” context actions.
- Pattern copy/paste and duplication within the phrase.

Undo covers generation and mutation as single operations, then individual manual edits as separate operations.

## Timing contract

Represent musical positions independently of the audio sample rate. The scheduler converts musical positions to sample frames using host tempo and time signature at each processing segment. Tempo changes and transport jumps must not change the pattern contents. They may change playback scheduling.

When host tempo is absent, use a manual tempo. If time signature is absent, assume 4/4 and show the fallback state. Do not infer tempo silently from imported audio; if tempo estimation is later added, present it as a suggestion that the user accepts.

## Acceptance criteria

- Identical inputs reproduce identical pattern data.
- Mutation leaves locked content byte-for-byte unchanged except for derived display state.
- Density and event caps are never exceeded.
- Undo restores the exact pre-generation pattern and seed.
- Tempo changes alter timing, not event ordering or generated random choices.
- Randomized patterns can be inspected and edited without rerolling unrelated sections.
## Module boundary and migration

The deterministic model and generator live in `pattern_engine`. It owns pattern nodes, event identities, seed interpretation, generation/mutation, lock semantics, edit commands, and conversion of nested patterns to a bounded flat event list. It has no editor, file path, host playhead, audio buffer, or JUCE dependency.

Feature modules may extend the pattern using stable contracts, but the base engine must not call feature-specific implementations directly. The composition layer supplies optional rule/transform providers and validates their outputs. Keep the pattern schema versioned, with explicit migrations for persisted projects.

**Transfer guide:** copy `modules/pattern_engine/` plus its declared dependency `chop_contracts`; link its CMake target; provide source chop snapshots through the public interface; choose a deterministic seed source; consume flattened events in the destination scheduler; run standalone tests and the receiving host's transport tests. Retain the schema version and migration functions when moving saved patterns. See `modules/pattern_engine/MIGRATION.md`.
