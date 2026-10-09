# Migrating `fractal_rhythm` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `fractal_rhythm` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/fractal_rhythm/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py fractal_rhythm /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/fractal_rhythm)
target_link_libraries(your_target PRIVATE chopfractal::fractal_rhythm)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Event IDs are derived (kDerivedIdBit) from seed, bar and path, so no id counter is needed.

Optional adapters: Install the returned events into your own pattern model (the pattern engine does this with `setBarEvents`).

## 5. State import and export

This module stores state under module ID `fractal_rhythm` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: generate a two-scale fractal bar over four chops and count the sounding hits.
#include <chopfractal/fractal_rhythm/fractal.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot snap;
  snap.sourceFrames = 4000;
  snap.sampleRate = 48000;
  for (int i = 0; i < 4; ++i) snap.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 1000, (i + 1) * 1000}, 0, 0, 0});
  fractal::Context ctx;
  ctx.chops = &snap;
  fractal::Settings s;
  s.motif = "x.xx";
  s.depth = 2;
  auto bar = fractal::generateBar(ctx, s);
  if (!bar.ok()) return 1;
  FlatEventList flat;
  Status st = flattenInto(flat, bar.value(), 0, ctx.barTicks, {}, snap, FlattenLimits{}, 1);
  if (!st.ok()) return 1;
  std::printf("%zu hits\n", flat.size());
  return flat.size() == 9 ? 0 : 1;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/fractal_rhythm -B build/fractal_rhythm && cmake --build build/fractal_rhythm && ctest --test-dir build/fractal_rhythm
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh fractal_rhythm
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; callers lose Fractal Rhythm only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
