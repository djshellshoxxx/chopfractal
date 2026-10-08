# GUI and interaction

**Status:** Draft for review

## Design goal

Build a modern, technical interface that makes source chops and generated rhythms understandable at a glance. Visual activity should explain audio behavior, not decorate it. The interface must remain legible at common plugin sizes and must not require color perception for editing.

## Layout

### Top bar

- Product name and plugin preset menu.
- Source status and load/capture controls.
- Host sync indicator and tempo/time-signature display.
- Undo/redo and global bypass.

### Source panel

- Waveform overview with a zoomable detailed region.
- Chop markers with visible start/end boundaries and selected marker state.
- Candidate transient strength shown by height or opacity.
- Mode selector for Transients, Even Grid, Manual.
- Detection sensitivity and marker snap controls.
- Chop audition.

### Pattern panel

- Primary timeline with bar divisions and event blocks.
- Event block color or shape identifies source chop; labels or tooltips also show chop IDs.
- Current playback position is animated in sync with host time.
- Density, swing, grid, and pattern-length controls.
- Event selection exposes source chop and transforms.
- Lock icons or patterns distinguish locked scopes; never rely on color alone.

### Fractal controls

A compact hierarchy view provides Phrase, Bar, Beat, and Event rows. Each row has:
- Variation amount.
- Lock state and scope.
- A small preview of repeated/subdivided structure.
- Mutate scope action.

The user can switch between a simple view (Generate, Mutate, Density, Variation, Seed) and an advanced view (per-level variation and event controls). Do not bury essential playback or source controls in advanced mode.

### Output panel

- Dry/Source blend and output gain.
- Optional effect enable controls.
- Active-voice and peak meters.
- Clear clipping indication.
- Export/render action if implemented.

## Interaction details

- Generate creates a new pattern; Mutate changes only unlocked scopes.
- Seed is visible and copyable. A dice action changes the seed and regenerates only when the user requests it.
- Locking a bar freezes it immediately; unlocking does not automatically mutate it.
- Clicking a source chop auditions it. Dragging a chop onto an event changes that event’s source.
- Double-clicking an event opens concise event editing; advanced settings stay in context.
- Right-click/context menu exposes duplicate, lock, mute, reset, and edit actions.
- Keyboard focus order follows the visible layout. Space toggles playback preview only when focus is not in a text field or host-owned control.
- Tooltips include current value, range, and units.
- Destructive source replacement and clearing ask for confirmation when unsaved edits would be lost.

## Visual style

- Dark neutral background with restrained high-contrast accents.
- Distinct but consistent colors for phrase, bar, beat, and event scopes.
- Typography sized for readability; numeric controls show exact values.
- Smooth, low-cost waveform and playhead drawing; no continuous animation while stopped.
- Resizable layout with sensible minimum dimensions and preserved proportions.
- Support high-DPI displays and OS scaling.

## Accessibility and usability

- Text or shape accompanies all color-coded states.
- Keyboard navigation and accessible names for controls.
- Contrast must support text and selection states.
- Avoid rapid flashing, high-frequency pulsing, and motion that provides no functional information.
- Provide a reset-to-default action for each control group.
- Confirm that the host can resize and close/reopen the editor without changing audio state.

## Acceptance criteria

- Core import, generate, mutate, lock, playback, and undo actions are discoverable without documentation.
- Current play position, source chop, event transform, lock status, and clipping are distinguishable.
- Resizing does not hide core controls or make timeline editing impossible.
- UI refresh rate is decoupled from audio processing and does not affect audio determinism.
## Module boundary and migration

The UI is a replaceable adapter, not the owner of pattern or source state. It consumes immutable view snapshots and sends commands through public interfaces. Waveform drawing, timeline rendering, and hierarchy visualization may be separate UI components, but they must not depend on the audio callback or module-private data.

**Transfer guide:** copy the desired component under `modules/ui_components/` with its documented public model interface; supply a view-model adapter for the destination application's source and pattern types; map actions to that application's command queue; verify keyboard, scaling, and accessibility behavior. JUCE-specific widgets remain in a JUCE adapter and are not a dependency of the core modules. See the UI migration notes in the component protocol.
