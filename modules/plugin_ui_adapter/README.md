# `plugin_ui_adapter` — Plugin UI adapter (portable part)

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Headless view models for the editor so geometry, text, and layout are unit-testable without a GUI: waveform peaks, drift-free time-to-pixel mapping with zoom/scroll, marker and pattern views, breadcrumbs, accessible descriptions, and history-tree layout.

## Public API

- `computePeaks()`, `ViewWindow`, `frameToX/xToFrame/zoomAround/scrolledBy`
- `buildMarkerViews()`, `buildPatternView()` (nested hits included), `breadcrumbFor()`
- `layoutHistoryTree()`
- `buildOrbitView(pattern, chops)`, `playheadAngle()`, `hitPulse()`, `arcRadii()`, `hitTest()` (Orbit View model)

Public headers:

- `include/chopfractal/plugin_ui_adapter/views.hpp`
- `include/chopfractal/plugin_ui_adapter/orbit.hpp`

## Contract

- **Threading:** UI thread only. Views are derived from immutable snapshots and never mutate project state.
- **Deterministic:** yes · **Real-time safe:** no

## Limits

- Colour is never the only indicator: every rectangle carries a text description

## Dependencies

- `chop_contracts` ^0.1 — chop and ID types
- `source_chop` ^0.1 — marker views read the chop map
- `pattern_engine` ^0.1 — pattern views read the pattern tree
- `variation_history` ^0.1 — the tree layout reads node summaries

## Build and test

```sh
cmake -S modules/plugin_ui_adapter -B build/plugin_ui_adapter && cmake --build build/plugin_ui_adapter && ctest --test-dir build/plugin_ui_adapter
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
