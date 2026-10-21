# Developer Code Map

Paths are relative to the repo root.

## Directory tree

```
chopfractal/
├── CMakeLists.txt                 root build: modules, composition, plugin (optional), warnings-as-errors option
├── VERSION                        0.2.0-beta.1 (the only typed version)
├── modules/<id>/                  fifteen portable C++17 modules, each with
│   │                              src/, include/chopfractal/<id>/, tests/, examples/, README.md,
│   │                              MIGRATION.md (generated), module.json (id, version, dependencies)
│   ├── chop_contracts/            Result, Status, IDs, SampleRange, ChopSnapshot, RNG, byte codec, time types
│   ├── source_chop/               chop map, markers, detection, undo
│   ├── pattern_engine/            Pattern, Bar, Beat, Note, Variation, Settings; generate, mutate, edit, flatten, session, undo
│   ├── recursive_hit_zoom/        nested patterns from one hit (zoom.hpp)
│   ├── fractal_rhythm/            motif applied at every scale
│   ├── chop_insight/              analysis of chops (ChopSnapshot consumers)
│   ├── chop_roles_grammar/        candidate policy, role suggestions (kick, snare, hat, cymbal)
│   ├── evolve/                    timed mutation while playing
│   ├── audio_renderer/            renderer, Mailbox (audio-thread handoff), playback snapshot
│   ├── plugin_host_adapter/       host transport and time to RenderParams / TransportBlock
│   ├── plugin_ui_adapter/         UI views over chops, pattern tree and history
│   ├── wav_export/                WAV writer with dither
│   ├── midi_export/               MIDI file writer (960 ticks per quarter)
│   ├── preset_store/              preset save and load
│   ├── state_codec/               state encode and decode
│   ├── time_stretch/              project time stretching (no third-party stretch libraries)
│   └── variation_history/         variation family tree
├── composition/
│   ├── include/chopfractal/composition/project_session.hpp   ProjectSession: the headless root
│   ├── include/chopfractal/composition/source_replace.hpp    source replacement helper
│   ├── src/, examples/, tests/
├── plugin/
│   ├── src/PluginProcessor.*, PluginEditor.* (shell: window, keys, drop, tooltips), EditorModel.*, EditorCommands.*,
│   │   ControlsPanel, FeaturePanel, HistoryPanel, OrbitPanel, PatternPanel, WavePanel, StatusBar, DragOut,
│   │   PresetManager, EditorTheme.h, ControlHelp.h, AppPaths.h, BuildInfo.h.in
│   └── tests/
├── tools/                         check_modules.py, gen_module_docs.py, check_includes.py, check_version.py,
│                                  transfer_module.py, validate_vst3.sh, packaging tests
├── docs/                          MODULARITY_MAP, BUILD_STATUS, QA_ALPHA2, RELEASING, LICENSING, specs/
│   └── wiki/                      these wiki pages
├── IP/                            public-safe IP records
├── .github/workflows/             ci.yml (module checks, Linux/Windows/macOS builds and tests), build-packages.yml, release-beta.yml
└── shared/, skills/               shared tooling and skills (not app code)
```

## Key symbols

| Symbol (file) | Purpose |
|---|---|
| `class ProjectSession` (`composition/include/chopfractal/composition/project_session.hpp:92`) | the headless root. Holds the source, chop map, pattern and history |
| `attachMailbox()` (same file, line 98) | where validated playback snapshots are published |
| `playback()` (same file, line 99) | the latest snapshot as `shared_ptr<const render::Playback>` |
| `renderOffline()` (line 100) | offline render from settings |
| `auditionRequest()` (line 101) | audition uses the same read and fade path as playback |
| `loadSource()`, `clearSource()`, `hasSource()` (lines 104 to 106) | source loading |
| `previewChops()` (line 111), `applyChops()` (line 112) | detect chops. Preview never applies |
| `editMarkers()`, `undoMarkers()`, `redoMarkers()` (lines 114 to 116) | marker edits with undo |
| `struct DetectOptions` (line 53), `SetupSuggestion` (line 63), `ExportWavOptions` (line 67), `ExportReport` (line 74) | option and result types |
| `class Mailbox` (`modules/audio_renderer/include/chopfractal/audio_renderer/mailbox.hpp`) | lock-free handoff to the audio thread |
| `struct Pattern`, `Bar`, `Beat`, `Note`, `Variation`, `Settings`, `MutateOptions`, `ScopeRef`, `Location` (`modules/pattern_engine/include/chopfractal/pattern_engine/pattern.hpp:27` to `:118`) | pattern model |
| `session.hpp` (`modules/pattern_engine/include/chopfractal/pattern_engine/`) | pattern session and undo |
| `map_regions.cpp`, `generate.cpp`, `edit.cpp`, `serialize.cpp` (`modules/pattern_engine/src/`) | region mapping, generation, editing, serialisation |
| `Result`, `Status`, `ErrorCode` (`modules/chop_contracts/`) | shared result types |

## Where do I find X / to change X, edit Y

| I want to... | Edit |
|---|---|
| change how a pattern is generated | `modules/pattern_engine/src/generate.cpp` |
| change mutate or edit behaviour | `modules/pattern_engine/src/edit.cpp` and `MutateOptions` in `pattern.hpp` |
| change how patterns are saved | `modules/pattern_engine/src/serialize.cpp`, and the state codec if the wire format changes |
| change chop detection | `modules/source_chop/` and `ProjectSession::previewChops` / `applyChops` in `project_session.hpp` |
| change Recursive Hit Zoom | `modules/recursive_hit_zoom/src/zoom.cpp` |
| change Fractal Rhythm | `modules/fractal_rhythm/` |
| change role suggestions | `modules/chop_roles_grammar/` and `SetupSuggestion` in `project_session.hpp` |
| change playback rendering | `modules/audio_renderer/src/renderer.cpp` |
| change the audio-thread handoff | `modules/audio_renderer/include/chopfractal/audio_renderer/mailbox.hpp` |
| change WAV export | `modules/wav_export/src/wav.cpp` and `ExportWavOptions` |
| change MIDI export | `modules/midi_export/` |
| change presets | `modules/preset_store/src/presets.cpp` |
| change the variation tree | `modules/variation_history/` |
| change the plug-in editor | `plugin/src/PluginEditor.cpp` (shell), `EditorModel.cpp`, the panel files (`ControlsPanel`, `PatternPanel`, `OrbitPanel`, `WavePanel`, `HistoryPanel`, `FeaturePanel`) |
| add a module | new folder under `modules/` with `module.json`, add it to the root `CMakeLists.txt` module list, then `tools/check_modules.py` |

## Gotchas for navigation

- `modules/*` must not depend on JUCE. `plugin/` is the only place JUCE appears.
- Check `module.json` before you add an include across modules. `tools/check_includes.py` enforces the declared dependencies.
- Generated `MIGRATION.md` files come from `tools/gen_module_docs.py`. Edit the generator source, not the output.
