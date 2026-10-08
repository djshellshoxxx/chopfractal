# Migrating `chop_insight` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `chop_insight` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/chop_insight/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py chop_insight /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/chop_insight)
target_link_libraries(your_target PRIVATE chopfractal::chop_insight)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Role names match the built-in role IDs of `chop_roles_grammar` but this module does not depend on it.

Optional adapters: Pass decoded planar sample pointers wrapped in `SourceView`; apply accepted suggestions in your own role-assignment UI.

## 5. State import and export

This module stores state under module ID `chop_insight` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: classify a synthetic kick and suggest loop lengths for an 8 second clip.
#include <chopfractal/chop_insight/insight.hpp>
#include <cmath>
#include <cstdio>

int main() {
  using namespace chopfractal::insight;
  std::vector<float> kick(7200);
  for (std::size_t i = 0; i < kick.size(); ++i)
    kick[i] = static_cast<float>(std::sin(2.0 * 3.14159265 * 55.0 * static_cast<double>(i) / 48000.0) * std::exp(-static_cast<double>(i) / 1800.0));
  const float* ch[1] = {kick.data()};
  SourceView view{ch, 1, static_cast<std::int64_t>(kick.size()), 48000};
  auto f = analyzeChop(view, {0, view.frames});
  if (!f.ok() || classify(f.value()) != Role::Kick) return 1;
  auto loops = suggestLoop(8.0);
  if (loops.empty()) return 1;
  std::printf("kick detected; best loop: %s\n", loops[0].note.c_str());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/chop_insight -B build/chop_insight && cmake --build build/chop_insight && ctest --test-dir build/chop_insight
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh chop_insight
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; callers lose Smart Setup suggestions only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
