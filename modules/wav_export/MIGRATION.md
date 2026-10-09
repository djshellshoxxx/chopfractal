# Migrating `wav_export` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `wav_export` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/wav_export/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py wav_export /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/wav_export)
target_link_libraries(your_target PRIVATE chopfractal::wav_export)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Pass planar float buffers (one vector per channel).

Optional adapters: Your app picks the destination path and confirms overwrites; this module only produces bytes and writes them atomically.

## 5. State import and export

This module stores state under module ID `wav_export` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: encode one second of a sine to a 24-bit WAV in memory and read it back.
#include <chopfractal/wav_export/wav.hpp>
#include <cmath>
#include <cstdio>

int main() {
  using namespace chopfractal::wav;
  std::vector<std::vector<float>> audio(1, std::vector<float>(48000));
  for (std::size_t i = 0; i < audio[0].size(); ++i) audio[0][i] = 0.5f * std::sin(0.05f * static_cast<float>(i));
  auto bytes = encode(audio, Options{});
  if (!bytes.ok()) return 1;
  auto back = decode(bytes.value().data(), bytes.value().size());
  if (!back.ok() || back.value().planar[0].size() != 48000) return 1;
  std::printf("wav: %zu bytes\n", bytes.value().size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/wav_export -B build/wav_export && cmake --build build/wav_export && ctest --test-dir build/wav_export
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh wav_export
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; callers lose export only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
