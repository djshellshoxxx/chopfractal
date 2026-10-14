# Migrating `scenes` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `scenes` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/scenes/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py scenes /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/scenes)
target_link_libraries(your_target PRIVATE chopfractal::scenes)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Scene patterns are opaque byte blobs here; the caller decides how to serialize and restore them.

Optional adapters: Call `onBoundary()` once per observed pattern loop boundary and arm the returned scene in your audio engine so the switch lands on the boundary frame.

## 5. State import and export

This module stores state under module ID `scenes` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: three scenes of two loops each; print which scene plays after each loop boundary.
#include <chopfractal/scenes/scenes.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::scenes;
  SceneList list;
  for (const char* name : {"A", "B", "C"}) {
    Scene s;
    s.name = name;
    s.loops = 2;
    if (!add(list, s).ok()) return 1;
  }
  Sequencer q;
  q.start(list, list.scenes.front().id);
  int switches = 0;
  for (int loop = 0; loop < 6; ++loop) {
    const Advance a = q.onBoundary(list);
    switches += a.switched ? 1 : 0;
    std::printf("boundary %d: playing %u%s\n", loop + 1, a.playing, a.switched ? " (switched)" : "");
  }
  return switches == 3 ? 0 : 1;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/scenes -B build/scenes && cmake --build build/scenes && ctest --test-dir build/scenes
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh scenes
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. No JUCE, no host types, no audio.
- Removal: Delete the folder; callers lose scene sequencing only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
