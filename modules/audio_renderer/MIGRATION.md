# Migrating `audio_renderer` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `audio_renderer` 0.1.0 (API 0.1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/audio_renderer/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py audio_renderer /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/audio_renderer)
target_link_libraries(your_target PRIVATE chopfractal::audio_renderer)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Map your source and event types to `SourceData` and `FlatEvent`, set sample rate and maximum block in `Config`, publish prepared playback from a non-real-time thread.

Optional adapters: The plugin shell owns bus negotiation, host bypass, and transport translation (`plugin_host_adapter`).

## 5. State import and export

This module stores no project state.

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: render one event of a ramp source offline through the same path as live playback.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  auto src = std::make_shared<render::SourceData>();
  src->channels = 1;
  src->sampleRate = 48000;
  src->frames = 24000;
  src->samples.assign(24000, 0.5f);

  FlatEvent e;
  e.id = EventId{1};
  e.chop = ChopId{1};
  e.region = {0, 24000};
  e.start = 0;
  e.duration = kTicksPerQuarter;  // one beat

  auto playback = render::makePlayback(src, {e}, kTicksPerQuarter * 4);
  if (!playback.ok()) return 1;
  render::OfflineSettings settings;
  settings.bpm = 120.0;
  auto audio = render::renderOffline(playback.value(), settings);
  if (!audio.ok() || audio.value()[0].size() < 24000) return 1;
  std::printf("rendered %zu frames\n", audio.value()[0].size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/audio_renderer -B build/audio_renderer && cmake --build build/audio_renderer && ctest --test-dir build/audio_renderer
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh audio_renderer
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. The test suite replaces global `operator new` to prove zero allocation in `process()`.
- Removal: Remove the composition root and host adapter first.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
