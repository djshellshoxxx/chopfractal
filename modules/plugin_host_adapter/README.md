# `plugin_host_adapter` — Plugin host adapter (portable part)

Version 0.1.0 · API 0.1 · C++17

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Everything about the VST3 host boundary that needs no JUCE: the append-only automation parameter manifest (golden-pinned), normalization, host playhead validation with visible tempo/meter fallbacks, and bus-layout rules. The JUCE processor in `plugin/` is a thin shell over this.

## Public API

- `parameterManifest()`, `findParam()`, `normalize/denormalize/snapValue`, `ParamValues`, `toRenderParams()`
- `translateHostTime(HostTimeInfo, manualBpm, fallbackMeter)` -> transport block + fallback flags
- `isSupportedBusLayout(in, out)`
- Manifest version 2 appends `allow_filter`, `allow_glide`, `allow_crunch`, `fx_intensity`
- `computeLoopPosition(TransportBlock, frames, sampleRate, patternQuarters)` -> `{boundary, midpoint, playheadQuarters}`: loop-boundary / loop-midpoint crossing and playhead position for one host block (extracted unchanged from the plugin's processBlock; pure and audio-thread safe)

Public headers:

- `include/chopfractal/plugin_host_adapter/parameters.hpp`
- `include/chopfractal/plugin_host_adapter/host_time.hpp`
- `include/chopfractal/plugin_host_adapter/loop_position.hpp`

## Contract

- **Threading:** Pure functions; thread-safe. `translateHostTime` and `toRenderParams` are allocation-free and safe on the audio thread.
- **Deterministic:** yes · **Real-time safe:** yes (see threading)

## Limits

- Parameter IDs are permanent and append-only; the seed is deliberately not a parameter

## Dependencies

- `chop_contracts` ^0.1 — time types
- `audio_renderer` ^0.1 — RenderParams and TransportBlock are the outputs of the translation

## Build and test

```sh
cmake -S modules/plugin_host_adapter -B build/plugin_host_adapter && cmake --build build/plugin_host_adapter && ctest --test-dir build/plugin_host_adapter
```

Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).

## License

`LicenseRef-ChopFractal-Proprietary` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it).
