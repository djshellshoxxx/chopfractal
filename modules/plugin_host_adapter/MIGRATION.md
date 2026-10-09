# Migrating `plugin_host_adapter` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `plugin_host_adapter` 0.1.0 (API 0.1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/audio_renderer/` (^0.1)
- `modules/plugin_host_adapter/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py plugin_host_adapter /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/audio_renderer)
add_subdirectory(modules/plugin_host_adapter)
target_link_libraries(your_target PRIVATE chopfractal::plugin_host_adapter)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Build your framework's parameter tree from `parameterManifest()` using each entry's `id` and `sinceVersion` as the stable identity and version hint.

Optional adapters: The JUCE `AudioProcessor`/`AudioProcessorValueTreeState` glue (see `plugin/`).

## 5. State import and export

This module stores no project state.

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: turn raw host playhead data and automation values into renderer inputs.
#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::host;
  HostTimeInfo hostTime;  // a host that reports nothing at all
  hostTime.isPlaying = true;
  const TimeTranslation t = translateHostTime(hostTime, /*manualBpm=*/128.0);
  if (!t.usedFallbackTempo || t.block.bpm != 128.0) return 1;

  ParamValues values = ParamValues::defaults();
  values.set(kDryMix, 0.25);
  const auto render = toRenderParams(values);
  std::printf("%zu parameters, dry=%.2f, tempo fallback=%d\n", parameterManifest().size(), static_cast<double>(render.dryMix), t.usedFallbackTempo);
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/plugin_host_adapter -B build/plugin_host_adapter && cmake --build build/plugin_host_adapter && ctest --test-dir build/plugin_host_adapter
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh plugin_host_adapter
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder and rebuild your parameter tree by hand.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
