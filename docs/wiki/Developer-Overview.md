# Developer Overview

## What it is

A C++17 project made of independent modules, a headless composition root, and a thin JUCE VST3 shell. The modules are portable and do not depend on JUCE. Version is `0.2.0-beta.1` (`VERSION` file, the only place it is typed).

## Design decisions (and why)

**Modules that build alone.** Each folder under `modules/` builds with only its declared dependencies. `module.json` lists them, with a reason for each. The point is to reuse a module in another project (`tools/transfer_module.py` copies a module and its dependencies).

**Headless engine, thin shell.** All behaviour sits in the modules and in `composition/` (`ProjectSession`). `plugin/` only wires JUCE to the session: window, panels, keys, file drop, tooltips. The engine can be tested and run without a host.

**Deterministic by design.** The seed drives all generation, using the deterministic RNG in `chop_contracts`. The same seed and settings give the same pattern. The byte codec and state codec keep saved state exact.

**Audio thread gets snapshots, not state.** The composition publishes validated playback snapshots through a `render::Mailbox` (`ProjectSession::attachMailbox`). The audio side reads those, so the UI can edit the project without locking the audio thread.

**Edits are undoable.** Markers and patterns have undo and redo stacks (`source_chop` and `pattern_engine`).

**No time-stretch libraries.** CI fails if `rubberband`, `soundtouch` or `elastique` appear in a build file. Time stretching is a project-owned module (`time_stretch`).

**Versioned contracts.** `chop_contracts` holds the shared types (`Result`, `Status`, ID types, `SampleRange`, `ChopSnapshot`). Other modules depend on it with a version range in `module.json`.

## Module graph (main dependencies)

```
chop_contracts  ◄── almost everything (Result, IDs, RNG, byte codec, time types)
   ▲
   ├── source_chop ──► chop map, markers, undo, detection
   ├── pattern_engine ──► events, flatten(), generation, mutate, edit, undo
   ├── recursive_hit_zoom ──► nested patterns from a hit
   ├── fractal_rhythm ──► motif applied at every scale
   ├── chop_insight, chop_roles_grammar ──► analysis, role suggestions
   ├── audio_renderer ──► playback snapshots, mailbox (depends on chop_contracts)
   ├── wav_export, midi_export ──► file output
   ├── preset_store, state_codec ──► saved state
   ├── time_stretch ──► project time stretching
   ├── variation_history ──► family tree of variations
   ├── evolve ──► evolve timing (no declared dependency)
   ├── plugin_host_adapter ──► host time to RenderParams and TransportBlock (uses audio_renderer)
   └── plugin_ui_adapter ──► UI views over chops, pattern tree and variation history
            ▲
   composition/ ProjectSession (headless root wiring the modules)
            ▲
   plugin/ JUCE VST3 shell (PluginEditor, EditorModel, panels)
```

The authoritative dependency list for each module is its `module.json`.

## Signal and data flow

```
loop audio ──► source_chop (detect or place chops)  ──► ChopSnapshot
                    │
                    ▼
            pattern_engine (generate, mutate, zoom, fractal)  ──► Pattern (phrase/bar/beat/hit)
                    │                                       │
                    │                                       ▼
                    │                       audio_renderer  ──► Mailbox ──► audio thread (playback)
                    ▼
        variation_history (tree of branches)    wav_export / midi_export / preset_store
```

## Threads

- Audio thread reads `render::Playback` snapshots from the mailbox. It does not take UI locks.
- UI thread (`plugin/`) edits `ProjectSession`. Edits publish new snapshots.
- Operations return `Result` values. The shared status type includes a `Cancelled` error code (see the `time_stretch` dependency note).

See [[Developer-Code-Map]] for locations and [[Developer-Conventions]] for the rules.
