# Recursive Hit Zoom

**Status:** Creation spec for review  
**Module ID:** `recursive_hit_zoom`  
**Purpose:** Let a user open one pattern event and create a smaller, nested rhythm inside that event's original time and audio boundaries.

## Product behavior

Selecting a chop event exposes **Zoom In**. The module creates a child pattern that occupies exactly the parent's musical duration. The parent remains available as a fallback and can be collapsed to its original single event. The user can zoom into a child event again, creating a limited recursion tree.

This is a concrete use of ChopFractal's fractal identity: the user applies variation at the scale of the phrase, then zooms into a hit and applies another bounded rule at a smaller scale. It is not simply a global ratchet or random stutter control.

## Controls

- Subdivision count: 2, 3, 4, 5, 6, or 8.
- Child density: sparse, balanced, or dense, with an advanced numeric control.
- Child operation: repeat parent chop, select a subregion, alternate between compatible chops, or create rests.
- Local shuffle: bounded timing movement that never leaves the parent window.
- Transform inheritance: inherit parent transform, override selected fields, or reset child transforms.
- Depth: show current nesting level; default maximum 2, configurable hard maximum 3.
- Lock parent/child and Mutate Children actions.
- Preview Parent / Preview Children comparison.

## Data and algorithm contract

The module receives a parent event, allowed source-region bounds, generation settings, and a seed. It returns an immutable child-pattern node or an explicit error. Child timing is stored relative to the parent window; source offsets are always clamped to the parent chop. No descendant may read audio outside its ancestor's source region unless the user explicitly replaces the source chop for that child.

Generation is deterministic for identical input, settings, seed, and module version. Every child has a stable ID. Hard limits apply to total flattened events across the full pattern: the project-level event cap is never exceeded. If the requested subdivision would exceed that cap, return a limit error with the nearest permitted subdivision; do not silently truncate.

Suggested portable interface:
- `createChildPattern(parentEvent, sourceBounds, settings, seed) -> Result<ChildPattern, Error>`
- `flatten(parentPattern, limits) -> Result<FlatEventList, Error>`
- `collapse(nodeId) -> ParentEvent`

The module consumes and emits `chop_contracts` data only. It does not schedule audio, draw the zoom UI, call the host, or own sample memory.

## Integration plan

1. Pattern model stores an optional child-pattern reference on an event.
2. The composition root calls this module when the user chooses Zoom In or Mutate Children.
3. The pattern engine validates IDs, total event count, duration, and lock rules.
4. Before playback, the pattern is flattened into sample-independent scheduled events.
5. The audio renderer plays flattened events; it does not need to understand the nested tree.
6. The GUI renders a breadcrumb (Phrase → Bar → Beat → Hit → Child) and lets the user collapse or compare parent/child.
7. State serialization stores the child tree and module schema version.

## Tests and acceptance

- Child duration equals the parent duration within the canonical tick representation.
- Source reads remain within ancestor bounds.
- Identical seeds reproduce the same child tree.
- Depth, density, and event limits are enforced.
- Parent fallback is unchanged after child generation or mutation.
- Locked ancestors prevent descendant mutation unless explicitly unlocked.
- Flattened output matches the nested tree's intended timing and transforms.
- Invalid bounds return a safe error and never generate a malformed event.

## Migration to another project

Copy `modules/recursive_hit_zoom/` and the declared compatible `chop_contracts` module. Add the target to the receiving build, adapt its event and source-boundary types once, and provide its own parent-event selection UI. If the target has no nested pattern model, store the returned child tree as module-owned state and use the supplied flatten operation before playback. Preserve the `recursive_hit_zoom` module ID and schema version; retain migration tests and notices. The module's `MIGRATION.md` must contain a minimal consumer example, limits, and test command.
