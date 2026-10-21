# Migrating `preset_store` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `preset_store` 0.1.0 (API 0.1, state schema 2).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/chop_roles_grammar/` (^0.1)
- `modules/preset_store/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py preset_store /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/chop_roles_grammar)
add_subdirectory(modules/preset_store)
target_link_libraries(your_target PRIVATE chopfractal::preset_store)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

A preset may carry a starting seed but never a pattern; applying one changes settings only.

Optional adapters: Your app maps a Preset onto its own parameters and UI fields; this module never touches a host or filesystem.

## 5. State import and export

This module stores state under module ID `preset_store` at schema version 2. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: save the first factory preset to bytes and read it back.
#include <chopfractal/preset_store/presets.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::presets;
  const Preset& p = factoryPresets().front();
  auto bytes = serialize(p);
  auto back = deserialize(bytes.data(), bytes.size());
  if (!back.ok() || back.value().name != p.name) return 1;
  std::printf("%s -> %s (%zu bytes)\n", p.name.c_str(), fileNameFor(p.name).c_str(), bytes.size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/preset_store -B build/preset_store && cmake --build build/preset_store && ctest --test-dir build/preset_store
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh preset_store
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; callers lose presets only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
