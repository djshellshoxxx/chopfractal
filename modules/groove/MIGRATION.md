# Migrating `groove` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `groove` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/groove/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py groove /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/groove)
target_link_libraries(your_target PRIVATE chopfractal::groove)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

The groove is playback-time only: call apply() on the flattened list before you build your playback, and pass the same Groove to your MIDI writer.

Optional adapters: Pass a function from chop to RoleClass (use classOf(DrumRole))

## 5. State import and export

This module stores state under module ID `groove` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: swing a row of sixteenth hats with the MPC 62 template and print the shifted ticks.
#include <chopfractal/groove/groove.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::groove;
  Groove g = makeTemplate(Template::Mpc62);
  g.enabled = true;
  int moved = 0;
  for (int step = 0; step < 16; ++step) {
    const Shift s = grooveShift(g, RoleClass::Hat, step * 240, step, 3840, 16);
    std::printf("%d:%+d ", step, s.ticks);
    moved += s.ticks != 0;
  }
  std::printf("\n");
  return moved == 8 ? 0 : 1;  // every odd step moves
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/groove -B build/groove && cmake --build build/groove && ctest --test-dir build/groove
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh groove
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder and the composition hook (composition/src/groove.cpp); patterns are unaffected because a groove never writes into them.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
