# Migrating `input_capture` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `input_capture` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/input_capture/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py input_capture /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/input_capture)
target_link_libraries(your_target PRIVATE chopfractal::input_capture)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

The captured samples are planar floats at the host rate; wrap them in your own source type.

Optional adapters: Call `process()` from the audio callback before anything overwrites the input buffer, passing the host transport; poll `status()` and call `reap()` from a UI timer; call `take()` on a worker thread.

## 5. State import and export

This module stores state under module ID `input_capture` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: record two bars from the next bar line at 120 BPM, then take the audio.
#include <chopfractal/input_capture/capture.hpp>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::capture;
  CaptureEngine e;
  ArmSettings s;
  s.bars = 2;
  if (!e.arm(s)) return 1;
  std::vector<float> block(512, 0.25f);
  const float* in[2] = {block.data(), block.data()};
  CaptureTransport t;
  t.playing = t.positionValid = true;
  for (std::int64_t n = 0; e.status().state != CaptureState::Captured && n < 1000000; n += 512) {
    t.ppq = n * 120.0 / (60.0 * 48000.0);
    e.process(in, 2, 512, t);
  }
  auto audio = e.take();
  if (!audio) return 1;
  std::printf("captured %lld frames\n", static_cast<long long>(audio.value().frames));
  return audio.value().frames > 0 ? 0 : 1;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/input_capture -B build/input_capture && cmake --build build/input_capture && ctest --test-dir build/input_capture
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh input_capture
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. No JUCE and no host types.
- Removal: Delete the folder; callers lose live input capture only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
