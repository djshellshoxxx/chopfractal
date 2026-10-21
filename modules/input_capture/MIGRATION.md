# Migrating `input_capture` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `input_capture` 0.1.0 (API 0.1).

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

The engine never touches a file or the project; the caller turns CapturedAudio into its own source type.

Optional adapters: Call process() from the host's audio callback before anything that overwrites the input buffer, forward the transport (playing, ppq, bpm), and run take() on a worker thread.

## 5. State import and export

This module stores no project state.

## 6. Minimal compile and usage example

```cpp
// Arms a free-running capture, feeds it one second of a ramp as a host would, stops it and takes the audio.
#include <chopfractal/input_capture/capture.hpp>

#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::capture;
  CaptureEngine engine;
  ArmSettings settings;
  settings.mode = CaptureMode::Free;
  settings.channels = 1;
  settings.maxSeconds = 10.0;
  if (!engine.arm(settings).ok()) return 1;
  (void)engine.startNow();
  std::vector<float> block(512);
  const float* in[1] = {block.data()};
  CaptureTransport transport;
  for (int b = 0; b < 94; ++b) {
    for (std::size_t i = 0; i < block.size(); ++i) block[i] = static_cast<float>(b * 512 + static_cast<int>(i)) / 48128.f;
    engine.process(in, 1, 512, transport);
  }
  engine.requestStop();
  engine.process(in, 1, 512, transport);
  auto audio = engine.take();
  if (!audio.ok()) return 1;
  std::printf("captured %lld frames at %d Hz\n", static_cast<long long>(audio.value().frames), audio.value().sampleRate);
  return audio.value().frames == 94 * 512 ? 0 : 1;
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
