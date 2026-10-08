# Migrating `evolve` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `evolve` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/evolve/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py evolve /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/evolve)
target_link_libraries(your_target PRIVATE chopfractal::evolve)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

The seed and amount are inputs to whatever mutate function you have.

Optional adapters: Call `onLoopMidpoint()` from your own transport once per loop (at the middle of the loop) and apply the returned step.

## 5. State import and export

This module stores state under module ID `evolve` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: step every 2 loops and print the seeds a host would pass to its mutate function.
#include <chopfractal/evolve/evolve.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::evolve;
  Settings s;
  s.enabled = true;
  s.everyLoops = 2;
  Controller c;
  if (!c.start(s)) return 1;
  int steps = 0;
  for (int loop = 0; loop < 6; ++loop)
    if (auto st = c.onLoopMidpoint()) {
      std::printf("step %llu seed %llu amount %.2f\n", static_cast<unsigned long long>(st->index), static_cast<unsigned long long>(st->seed), st->amount);
      ++steps;
    }
  return steps == 3 ? 0 : 1;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/evolve -B build/evolve && cmake --build build/evolve && ctest --test-dir build/evolve
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh evolve
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. No dependencies at all.
- Removal: Delete the folder; callers lose Evolve mode only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
