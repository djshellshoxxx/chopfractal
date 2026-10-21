# Pass 3 features: Chaos, Presets, Drag-out, Direct hit editing

Status: implemented (audited 2026-10-20: matches the code except the drag-out folder note in P2; the Chaos mapping lives in `composition/src/chaos.cpp`, not in `pattern_engine`). Each feature has code, tests and a place in the editor. The shared rules are in `_BRIEF.md`.

## P1 Chaos macro (fun)
- **What:** one automatable host parameter `chaos` (0..1, default 0; host manifest version 3, appended after `fx_intensity`). It pushes the *effective* generation settings toward more variation. A **Reroll** button picks a new seed and runs Generate.
- **Mapping** (`composition/src/chaos.cpp`, `applyChaos(base, chaos)`): pure and deterministic. At chaos <= 0 (or non-finite) it returns the input untouched, so every existing project and golden hash is unchanged. As chaos rises: all four variation levels move up to 80% toward 1; density rises toward 0.6 (never lowered); reverse switches on at 0.35, filter 0.40, pitch 0.50, glide 0.60, retrigger 0.65, crunch 0.75 (never switched off); FX intensity moves up to 70% toward 1; pitch range, max retrigger and time shift grow. Output always passes `validateSettings`.
- **Wiring:** `settingsFromParams` applies it (Generate); Mutate applies it to the live settings.
- **Acceptance (tested):** identity at 0; monotone and always-valid property test across a sweep; parameter wiring and determinism; manifest golden row.

## P2 MIDI / WAV drag-out (valuable)
- **What:** drag the current pattern from the editor into a DAW track as a `.mid` or a rendered `.wav`.
- **Code:** `ProjectSession::renderMidi` / `renderWavBytes` (the exact bytes `exportKit` and `exportWav` write, tested byte-identical); `plugin/src/DragOut.*` writes a unique file under the per-user folder `<userApplicationData>/ChopFractal/Dropped` (changed in audit round 4 from a temp folder with a 7-day sweep; round 5 removed the automatic deletion, so files stay until the user deletes them), and `DragHandle` starts an OS file drag after 6 px of movement. With no pattern the handle reports the reason in the status line and drags nothing.
- **Acceptance (tested):** the dragged files start with `MThd` / `RIFF`; the drag folder is isolated in tests via `setFolderOverride`.

## P3 Presets (usable)
- **What:** preset combo (six factory presets, then user presets), **Save Preset** (name prompt, asks before replacing), **Delete** (user presets only).
- **Code:** portable module `modules/preset_store` (versioned binary, checksum, bounds checks, name sanitizing, never stores a seed or pattern). `plugin/src/PresetManager.*` stores `.cfpreset` files in the user application-data folder (`ChopFractal/Presets`), writes atomically, skips corrupt files and reports how many. Loading a preset sets the host parameters and editor fields and never changes the pattern until Generate.
- **Acceptance (tested):** module round-trip, corruption and factory validity; save / refuse duplicate / overwrite / list / delete / corrupt-file skip / delete outside the folder refused.

## P4 Direct hit editing (essential)
- **Gestures (pattern grid, top-level unlocked hits):** drag moves (snaps to the grid, stays in the pattern; dropping on another lane changes the hit's chop); drag the right edge resizes; Alt-drag copies within the bar; **Shift+click** on empty space adds a one-grid-cell hit; `Delete`/`Backspace` deletes the selected hit; `M` mutes/unmutes; `D` duplicates after the hit. Each finished gesture is one undo step; locked scopes refuse with a message; a movement under 3 px is a click.
- **Waveform:** a plain click auditions the chop under the cursor; **Shift+click** adds a marker; Alt+click deletes the nearest marker.
- **Code:** `pattern::resizeEvent`, `duplicateEvent`, `setEventEnabled` (pure, lock-checked); `plugin_ui_adapter/hit_edit.*` (pixel-to-tick, snapping, lane lookup); editor mouse handlers route through `ProjectSession::edit`.
- **Acceptance (tested):** engine edits incl. locks and the event cap; geometry unit tests; real synthesized mouse events on the editor (move, undo, click vs drag, mute, delete, Shift-click add).
