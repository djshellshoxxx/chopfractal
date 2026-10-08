# Migrating `recursive_hit_zoom` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `recursive_hit_zoom` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/recursive_hit_zoom/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py recursive_hit_zoom /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/recursive_hit_zoom)
target_link_libraries(your_target PRIVATE chopfractal::recursive_hit_zoom)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

If your project has no nested pattern model, store the returned child tree as module-owned state and call this module's `flatten()` before playback.

Optional adapters: Your UI chooses the parent event and shows the breadcrumb; the composition root supplies `Context` (source bounds, depth, event budget).

## 5. State import and export

This module stores state under module ID `recursive_hit_zoom` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: zoom into one event and flatten the result for playback.
#include <chopfractal/recursive_hit_zoom/zoom.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 48000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 48000}, 0, 0, 0});

  Event hit;
  hit.id = EventId{1};
  hit.chop = ChopId{1};
  hit.start = 0;
  hit.duration = kTicksPerQuarter;

  zoom::Context ctx;
  ctx.sourceBounds = chops.chops[0].range;  // the parent's resolved source range
  zoom::Settings settings;
  settings.subdivisions = 4;
  settings.density = zoom::Density::Dense;

  auto child = zoom::createChildPattern(hit, ctx, settings, /*seed=*/42);
  if (!child.ok()) return 1;
  hit.child = child.value();
  auto flat = zoom::flatten(hit, /*windowStart=*/0, chops);
  if (!flat.ok() || flat.value().size() != 4) return 1;
  std::printf("zoomed into %zu events\n", flat.value().size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/recursive_hit_zoom -B build/recursive_hit_zoom && cmake --build build/recursive_hit_zoom && ctest --test-dir build/recursive_hit_zoom
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh recursive_hit_zoom
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; events simply lose the optional child tree.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
