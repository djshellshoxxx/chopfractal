# Migrating `state_codec` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `state_codec` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/state_codec/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py state_codec /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/state_codec)
target_link_libraries(your_target PRIVATE chopfractal::state_codec)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Keep file dialogs and host project callbacks in your application; this module only sees byte vectors.

Optional adapters: Your host's save/restore callbacks hand the byte blob to `decode()` / receive it from `encode()`.

## 5. State import and export

This module stores state under module ID `state_codec` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: compose two module payloads into a project blob and read it back.
#include <chopfractal/state_codec/codec.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::codec;
  ProjectState state;
  state.modules["my_module"] = {1, {1, 2, 3}};
  auto blob = encode(state);
  if (!blob.ok()) return 1;
  auto back = decode(blob.value().data(), blob.value().size());
  if (!back.ok() || back.value().modules.at("my_module").bytes.size() != 3) return 1;
  std::printf("project state: %zu bytes\n", blob.value().size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/state_codec -B build/state_codec && cmake --build build/state_codec && ctest --test-dir build/state_codec
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh state_codec
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. No dependencies, not even `chop_contracts` (it carries its own private `Result` type).
- Removal: Delete the folder; callers lose project-state composition only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
