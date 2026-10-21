# `plugin_ui_adapter` — Plugin UI adapter (portable part)

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Headless view models for the editor so geometry, text, and layout are unit-testable without a GUI: waveform peaks, drift-free time-to-pixel mapping with zoom/scroll, marker and pattern views, breadcrumbs, accessible descriptions, and history-tree layout.

## Public API

- `computePeaks()`, `ViewWindow`, `frameToX/xToFrame/zoomAround/scrolledBy`
- `buildMarkerViews()`, `buildPatternView()` (nested hits included), `breadcrumbFor()`
- `layoutHistoryTree()`
- `buildOrbitView(pattern, chops)`, `playheadAngle()`, `hitPulse()`, `arcRadii()`, `hitTest()` (Orbit View model)
- `xToTick`, `snapDown`, `snapNearest`, `splitTick`, `laneAtY` (pixel/tick/lane geometry for direct hit editing)
- `snapMarkerFrame()`, `clampMarkerFrame()`, `snapTickToGrid()`, `planHitMove()`, `planHitResize()`, `laneGeometry()` and the scrolled-lane helpers (edit_geometry.hpp, G2 direct editing)
- `buildFxRackView()`, `fxSetControl()`, `fxSetModuleOn()`, `hitFxOf()` (per-hit effects rack model, fx_rack_view.hpp); `buildInspectorView()`, `formatLevelDb()`, `formatPan()`, `formatPitch()` (per-hit inspector model, inspector_view.hpp)
- `RuleEditorModel` (rule_editor.hpp): the rule editor draft with templates, engine-ordered rows, issues and Apply gating (F06)
- `buildCaptureView()`, `captureEndNotice()`, `formatCaptureTime()` (input capture control state, labels and notices)
- `buildPadView()`, `padTakeNotice()`, `padNoteName()`, `padVelocityFromY()`, `padQuantizeFromIndex()` (performance pad grid, labels, settings boxes and take messages)
- `buildMidiOptionsView()`, `buildDragOutView()`, `dragFileName()`, `midiNoteText()` and the exact drag-out status strings (F13); session-feature view models for F10, F09 and F18 in `session_views.hpp`

Public headers:

- `include/chopfractal/plugin_ui_adapter/views.hpp`
- `include/chopfractal/plugin_ui_adapter/preset_browser.hpp`
- `include/chopfractal/plugin_ui_adapter/export_view.hpp`
- `include/chopfractal/plugin_ui_adapter/settings_view.hpp`
- `include/chopfractal/plugin_ui_adapter/status_text.hpp`
- `include/chopfractal/plugin_ui_adapter/command_match.hpp`
- `include/chopfractal/plugin_ui_adapter/access_text.hpp`
- `include/chopfractal/plugin_ui_adapter/orbit.hpp`
- `include/chopfractal/plugin_ui_adapter/hit_edit.hpp`
- `include/chopfractal/plugin_ui_adapter/edit_geometry.hpp`
- `include/chopfractal/plugin_ui_adapter/fx_rack_view.hpp`
- `include/chopfractal/plugin_ui_adapter/inspector_view.hpp`
- `include/chopfractal/plugin_ui_adapter/rule_editor.hpp`
- `include/chopfractal/plugin_ui_adapter/session_views.hpp`
- `include/chopfractal/plugin_ui_adapter/capture_view.hpp`
- `include/chopfractal/plugin_ui_adapter/pads_view.hpp`
- `include/chopfractal/plugin_ui_adapter/outputs_view.hpp`

## Contract

- **Threading:** UI thread only. Views are derived from immutable snapshots and never mutate project state.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Colour is never the only indicator: every rectangle carries a text description

## Dependencies

- `chop_contracts` ^0.1 — chop and ID types
- `source_chop` ^0.1 — marker views read the chop map
- `chop_roles_grammar` ^0.1 — the rule editor model builds, validates and describes rules
- `pattern_engine` ^0.1 — pattern views read the pattern tree
- `variation_history` ^0.1 — the tree layout reads node summaries
- `input_capture` ^0.1 — the capture view reads the engine's status and end reasons

## Build and test

```sh
cmake -S modules/plugin_ui_adapter -B build/plugin_ui_adapter && cmake --build build/plugin_ui_adapter && ctest --test-dir build/plugin_ui_adapter
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
