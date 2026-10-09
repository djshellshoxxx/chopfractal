# Migrating `variation_history` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `variation_history` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/variation_history/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py variation_history /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/variation_history)
target_link_libraries(your_target PRIVATE chopfractal::variation_history)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Supply payload bytes from your own serializer and keep `payloadSchema` in the metadata so old snapshots can be migrated.

Optional adapters: Your serializer produces/consumes the payload bytes; your UI renders the tree.

## 5. State import and export

This module stores state under module ID `variation_history` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: record two snapshots of any serialized state and branch from the first.
#include <chopfractal/variation_history/history.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::history;
  VariationTree tree;
  Metadata m;
  m.label = "first";
  auto a = tree.addSnapshot(kNoNode, {1, 2, 3}, m);
  if (!a.ok()) return 1;
  auto b = tree.addSnapshot(a.value().id, {4, 5, 6}, m);
  auto c = tree.addSnapshot(a.value().id, {7, 8, 9}, m);  // a sibling branch
  if (!b.ok() || !c.ok() || tree.listChildren(a.value().id).size() != 2) return 1;
  std::printf("%zu snapshots\n", tree.size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/variation_history -B build/variation_history && cmake --build build/variation_history && ctest --test-dir build/variation_history
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh variation_history
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. No dependencies.
- Removal: Delete the folder; nothing else depends on its internals.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
