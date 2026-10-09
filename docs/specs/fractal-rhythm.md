# Fractal Rhythm

**Status:** Creation spec  
**Module ID:** `fractal_rhythm`  
**Purpose:** The feature that earns the name. A short rhythmic motif is applied **at every scale**: the bar is divided by the motif, and each hit is itself divided by the same motif, down to a chosen depth. The result is a self-similar groove (a Cantor-like rhythm) that no ordinary step sequencer produces, and it is built from the same nested hits as Recursive Hit Zoom, so it can be zoomed, collapsed, locked and played by the existing engine.

## User behavior

- The user types a motif of 2 to 8 cells using `x` (hit), `a` (accented hit) and `.` (rest), for example `x.xx` or `a.x.xx..`.
- **Depth** 1 to 3 chooses how many scales the motif is applied at. Depth 1 is the motif across the bar; depth 2 subdivides every hit by the motif again; depth 3 once more.
- **Mutation** (0 to 1) makes deeper levels vary: each deeper cell flips with that probability, so the groove is self-*similar* rather than identical.
- **Mirror** reverses the motif on alternate levels, giving palindromic structure.
- **Accent decay** (0 to 1) lowers the level of the sounding hits as depth grows. Only the deepest cells sound, so each hit's level is the product, over every scale, of its cell's weight (`a` = 1.0, `x` = 0.8), multiplied by `decay^(depth-1)` and clamped to 0.05 to 1. A decay of 1 leaves only the accent structure.
- The result replaces the chosen bars (default all unlocked bars). The bar is marked as user-owned, so **Mutate keeps it** unless "include edited" is on. Locked bars are never touched.
- Chops follow source order along the rhythm (the walk through the loop follows the walk through the source) with a seeded chance of an alternate chop; an optional candidate policy (roles grammar) is honored for forbid rules.

## Contract

`generateBar(Context, Settings) -> Result<std::vector<Event>>`:
- `Context`: bar index and count, bar length in ticks, meter, the chop list, optional `ICandidatePolicy*`.
- `Settings`: motif string, depth, seed, mutation, mirror, accent decay.
- Output: top-level events for the bar; every active cell above the deepest level carries a `NestedPattern` child covering exactly that cell's window. Event IDs are derived (`kDerivedIdBit`) from (seed, bar, path), so they are stable and need no counter. Child sources stay inside the parent's source range; a child that uses a different chop sets `sourceOverride`.
- Deterministic for the same context and settings. Cell boundaries are exact integers that tile the window with no rounding loss.

`pattern_engine` gains `setBarEvents(pattern, bar, events, chops)`: replaces a bar's content, refuses locked scopes, validates windows, IDs and the event cap, and marks the bar user-owned.

## Limits

- Total played hits per pattern never exceed the event cap: if `activeCells^depth` per bar would exceed the budget the call returns `LimitExceeded` with the deepest permitted depth in `hint`.
- The smallest cell at the deepest level must be at least 15 ticks.
- Nesting follows the global limit (depth 3 patterns use all three nesting levels).

## Tests and acceptance

- Leaf timing equals the analytic positions of the motif at each scale; cells tile the bar exactly.
- Same inputs give identical output; different seeds change chops and mutation only.
- Rests produce silence at every scale; accent and decay set levels as specified.
- Children stay inside parent bounds; flattening validates; the event cap and minimum cell size are enforced with the documented hints.
- Locked bars are refused; Mutate keeps fractal bars; undo restores the previous bar; the pattern serializes and reloads.

## Migration to another project

Copy `modules/fractal_rhythm/` and `modules/chop_contracts/`. Consume the returned nested events with `flattenInto()`.
