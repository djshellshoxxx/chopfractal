# Migrating `chop_contracts` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `chop_contracts` 0.1.0 (API 0.1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py chop_contracts /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
target_link_libraries(your_target PRIVATE chopfractal::chop_contracts)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Convert your host's time and ID types to `Ticks` and the `Id<>` types in one adapter; do not edit this module for another project's naming.

Optional adapters: None. Map your own ID and time types to these at one boundary.

## 5. State import and export

This module stores no project state.

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: define chops, one event, flatten it. Uses only chop_contracts public headers.
#include <chopfractal/chop_contracts/event.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 48000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 24000}, 0, 0, 0});

  Event e;
  e.id = EventId{1};
  e.chop = ChopId{1};
  e.start = 0;
  e.duration = kTicksPerQuarter;

  FlatEventList flat;
  Status s = flattenInto(flat, {e}, 0, ticksPerBar(TimeSignature{}), {}, chops, {}, 1);
  if (!s.ok() || flat.size() != 1) return 1;
  std::printf("flattened %zu event(s)\n", flat.size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/chop_contracts -B build/chop_contracts && cmake --build build/chop_contracts && ctest --test-dir build/chop_contracts
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh chop_contracts
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler (GCC, Clang, MSVC). Assumes IEEE-754 floats and two's-complement integers.
- Removal: Every other ChopFractal module depends on this one; remove those first.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
