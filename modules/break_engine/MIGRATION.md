# Migrating `break_engine` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `break_engine` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/break_engine/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py break_engine /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/break_engine)
target_link_libraries(your_target PRIVATE chopfractal::break_engine)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

The caller supplies which roles exist (`Avail`) so the plan never asks for a hat the source does not have.

Optional adapters: Map each Hit's role onto a chop of that role (pattern_engine does this) and its velocity onto event level.

## 5. State import and export

This module stores state under module ID `break_engine` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: plan one 4-bar phrase for the Jungle profile and print the hits of the last bar.
#include <chopfractal/break_engine/break_engine.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::breakeng;
  PlanSettings s;
  s.options.profile = Profile::Jungle;
  s.seed = 7;
  const auto plans = planPattern(s);
  if (plans.size() != 4) return 1;
  int snareAnchors = 0;
  for (const Hit& h : plans[3].hits) {
    std::printf("step %2d tick %4lld role %d vel %3d%s\n", h.step, static_cast<long long>(h.start), static_cast<int>(h.role), h.velocity, h.anchor ? " anchor" : "");
    if (h.anchor && h.role == chopfractal::DrumRole::Snare) ++snareAnchors;
  }
  return snareAnchors >= 1 ? 0 : 1;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/break_engine -B build/break_engine && cmake --build build/break_engine && ctest --test-dir build/break_engine
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh break_engine
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. Bit-identical output across platforms (integer and exact-double arithmetic only).
- Removal: Remove pattern_engine's engine v2 path first (generate_v2.cpp); the legacy engine does not use this module.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
