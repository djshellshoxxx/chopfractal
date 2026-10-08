# Migrating `source_chop` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `source_chop` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/source_chop/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py source_chop /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/source_chop)
target_link_libraries(your_target PRIVATE chopfractal::source_chop)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Wrap your decoded buffer in `SourceInfo` + sample pointers; keep the marker coordinate rule (integer source frames).

Optional adapters: WAV/AIFF decoding, input capture, and waveform drawing hand this module decoded sample views.

## 5. State import and export

This module stores state under module ID `source_chop` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: wrap a decoded buffer, place markers, export the chop snapshot.
#include <chopfractal/source_chop/analysis.hpp>
#include <chopfractal/source_chop/chop_map.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::source;
  SourceInfo info;
  info.name = "loop";
  info.sampleRate = 48000;
  info.frames = 48000;
  auto map = ChopMap::create(info);
  if (!map.ok()) return 1;
  map.value().applyProposal(evenGrid(info.frames, {4.0, 4, GridKind::Straight}), MergeMode::Replace);
  const auto snap = map.value().snapshot();
  if (snap->chops.size() != 4) return 1;
  std::printf("%zu chops\n", snap->chops.size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/source_chop -B build/source_chop && cmake --build build/source_chop && ctest --test-dir build/source_chop
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh source_chop
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Remove `plugin_ui_adapter` and the composition root's source features first; pattern modules only need the `ChopSnapshot` contract.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
