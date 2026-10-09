# Orbit View

**Status:** Creation spec  
**Module:** extends `plugin_ui_adapter` (view model) and the plugin editor (drawing)  
**Purpose:** The visual feature. The whole phrase is drawn as **concentric rings** with a sweeping playhead: every chop is a ring, every hit is an arc on its ring, zoomed (nested) hits appear as thinner arcs inside their parent, and hits **pulse** as the playhead crosses them. It makes the hierarchy and the fractal structure obvious at a glance and gives the plugin a signature look.

## User behavior

- 12 o'clock is the start of the pattern; time runs clockwise. Bar and beat ticks are drawn on the outer edge.
- Ring *n* is chop *n* (outermost is chop 1); the ring number is also drawn so colour is never the only indicator. Locked bars are marked.
- Arc length is the hit's duration; arc brightness is its level. Zoomed hits show their children as thinner concentric arcs just inside the parent arc (one step in per nesting level).
- The playhead is a line from the centre to the edge. A hit that is sounding glows; the glow fades over the hit's duration.
- Clicking an arc selects that hit (same selection as the pattern grid); Zoom, Collapse and Lock then act on it.
- The view follows the host transport; when stopped it shows the position where playback stopped.

## Contract (view model, no drawing code)

In `plugin_ui_adapter`:
- `buildOrbitView(pattern, chops) -> OrbitView{ lengthTicks, barAngles, beatAngles, rings, arcs[] }` where each `OrbitArc` has id, parent id, ring, depth, start angle, sweep, chop index, level, locked, has-child.
- `playheadAngle(positionQuarters, lengthTicks)` -> radians in `[0, 2*pi)`.
- `hitPulse(arc, positionTicks)` -> `0..1` glow for the hit at the playhead (1 at the hit's start, fading to 0 at its end; 0 outside the hit).
- `hitTest(view, x, y, radiusPx)` -> the arc id under a point (for clicking).
- Angles are computed from exact tick fractions, so arcs of a bar tile its sector with no gaps or overlaps; nested arcs lie within their parent's angular span.

## Tests and acceptance

- Arc angles equal `2*pi*tick/length`; bar sectors partition the circle; nested arcs are contained in their parents.
- Playhead angle wraps at the loop length and handles negative and non-finite input.
- Pulse is 0 before and after a hit, 1 at the start, monotonically decreasing within it.
- Hit testing returns the innermost arc under the point and nothing outside the rings.
- The editor renders the view without error under a virtual display (snapshot test).

## Migration to another project

The view model has no drawing dependency; copy `modules/plugin_ui_adapter/` (with `source_chop`, `pattern_engine`, `variation_history` and `chop_contracts`) and draw the arcs with any 2D toolkit.
