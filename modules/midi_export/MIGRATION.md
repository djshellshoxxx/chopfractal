# Migrating `midi_export` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `midi_export` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/midi_export/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py midi_export /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/midi_export)
target_link_libraries(your_target PRIVATE chopfractal::midi_export)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Pitch is just a number; map chops to notes however your sampler expects.

Optional adapters: Your app maps its own events to `Note` values and writes the bytes to disk.

## 5. State import and export

This module stores state under module ID `midi_export` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: write two notes to a MIDI file in memory and read them back.
#include <chopfractal/midi_export/midi.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::midi;
  FileSpec spec;
  spec.bpm = 96;
  spec.notes = {{36, 100, 0, 480, 0}, {38, 90, 960, 480, 0}};
  auto bytes = encode(spec);
  if (!bytes.ok()) return 1;
  auto back = decode(bytes.value().data(), bytes.value().size());
  if (!back.ok() || back.value().notes.size() != 2) return 1;
  std::printf("midi: %zu bytes\n", bytes.value().size());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/midi_export -B build/midi_export && cmake --build build/midi_export && ctest --test-dir build/midi_export
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh midi_export
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; callers lose MIDI export only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
