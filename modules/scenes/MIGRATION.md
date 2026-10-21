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

The host owns pattern validation: it must parse every scene's bytes with its own pattern codec after `deserialize`.

Optional adapters: Call `onBoundary()` once per observed pattern-loop boundary; arm the returned scene on the audio side and adopt it when the audio side reports the switch.

## 5. State import and export

This module stores state under module ID `scenes` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: three scenes played A (2 loops), B (1 loop), C (3 loops, then stop).
#include <chopfractal/scenes/scenes.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::scenes;
  SceneList list;
  list.wrap = false;
  const int loops[3] = {2, 1, 3};
  for (int i = 0; i < 3; ++i) {
    Scene s;
    s.name = std::string("Scene ") + char('1' + i);
    s.loops = loops[i];
    s.next = i == 2 ? Next::Stop : Next::Advance;
    s.pattern = {1, 2, 3};
    if (!add(list, s)) return 1;
  }
  Sequencer q;
  q.start(list, list.scenes[0].id);
  int switches = 0;
  for (int boundary = 1; boundary <= 6; ++boundary) {
    const Advance a = q.onBoundary(list);
    if (a.arm) std::printf("boundary %d: arm scene %u\n", boundary, a.arm);
    if (a.switched) {
      std::printf("boundary %d: now playing scene %u\n", boundary, a.playing);
      ++switches;
    }
    if (a.finished) std::printf("boundary %d: finished\n", boundary);
  }
  return switches == 2 && !q.running() ? 0 : 1;
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

- Platforms: Any C++17 compiler. Depends only on chop_contracts.
- Removal: Delete the folder; callers lose scenes only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
