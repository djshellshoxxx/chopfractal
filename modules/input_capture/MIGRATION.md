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

The module never talks to a host, a file or a thread API. Captured audio is plain planar floats.

Optional adapters: Your app calls process() from its audio callback with the host's transport and keeps the audio with take().

## 5. State import and export

This module stores state under module ID `input_capture` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Records two bars of a test tone at 120 BPM (4/4, 48 kHz), starting on a bar line, and prints what was captured.
#include <chopfractal/input_capture/capture.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::capture;
  Capture cap;
  Options o;
  o.channels = 1;
  o.sampleRate = 48000.0;
  o.targetFrames = framesForBars(2, 4.0, 120.0, 48000.0);  // 192000 frames
  o.waitForBar = true;
  if (!cap.arm(o).ok()) return 1;

  std::vector<float> block(512);
  double ppq = 3.9;  // the host is just before a bar line
  for (int b = 0; b < 400 && cap.state() != State::Done; ++b) {
    for (int i = 0; i < 512; ++i) block[static_cast<std::size_t>(i)] = 0.5f * std::sin(0.05f * static_cast<float>(b * 512 + i));
    const float* in[1] = {block.data()};
    Transport t;
    t.playing = true;
    t.positionValid = true;
    t.ppq = ppq;
    cap.process(in, 1, 512, t);
    ppq += 512.0 * 120.0 / (60.0 * 48000.0);
  }
  auto r = cap.take();
  if (!r.ok()) return 1;
  std::printf("captured %lld frames (%d channel, %d Hz)\n", static_cast<long long>(r.value().frames), r.value().channels, r.value().sampleRate);
  return r.value().frames == o.targetFrames ? 0 : 1;
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

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; callers lose input capture only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
