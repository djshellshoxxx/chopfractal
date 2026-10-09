# Migrating `pattern_engine` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `pattern_engine` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/pattern_engine/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py pattern_engine /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/pattern_engine)
target_link_libraries(your_target PRIVATE chopfractal::pattern_engine)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Pass chop snapshots in, consume `flatten()` output in your scheduler; choose the seed source yourself. Engine version is stored with every pattern so algorithm changes never silently rewrite old patterns.

Optional adapters: Optionally supply an `ICandidatePolicy` (for example `chop_roles_grammar`).

## 5. State import and export

This module stores state under module ID `pattern_engine` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: generate a 2-bar pattern from two chops and flatten it for a scheduler.
#include <chopfractal/pattern_engine/pattern.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 96000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 48000}, 0, 0, 0});
  chops.chops.push_back({ChopId{2}, {48000, 96000}, 0, 0, 0});

  pattern::Settings settings;
  settings.bars = 2;
  settings.density = 0.6;
  settings.seed = 1234;  // same chops + settings + seed => identical pattern, always

  auto generated = pattern::generate(chops, settings);
  if (!generated.ok()) return 1;
  auto flat = pattern::flatten(generated.value(), chops);
  if (!flat.ok() || flat.value().empty()) return 1;
  std::printf("%zu events over %lld ticks\n", flat.value().size(), static_cast<long long>(pattern::lengthTicks(generated.value())));
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/pattern_engine -B build/pattern_engine && cmake --build build/pattern_engine && ctest --test-dir build/pattern_engine
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh pattern_engine
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. Output is bit-identical across platforms (integer and exact-double arithmetic only; a golden hash test pins it).
- Removal: Remove the composition root and `plugin_ui_adapter` first.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
