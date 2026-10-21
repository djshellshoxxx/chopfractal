# Migrating `time_stretch` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `time_stretch` 0.1.0 (API 0.1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/time_stretch/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py time_stretch /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/time_stretch)
target_link_libraries(your_target PRIVATE chopfractal::time_stretch)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Hand in decoded planar float buffers and region anchors in source frames; scale your own marker data with mapPosition/mapLength using the same (in, out) pair.

Optional adapters: The composition root builds the plan from chop markers and runs stretch on a worker; tempo (source BPM) comes from the user or from clip duration, never from the audio.

## 5. State import and export

This module stores no project state.

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: stretch a 1 s tone (96 BPM loop) to 120 BPM with one anchored chop boundary.
#include <chopfractal/time_stretch/stretch.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  using namespace chopfractal::stretch;
  constexpr int sr = 48000;
  std::vector<float> tone(sr);
  for (int i = 0; i < sr; ++i) tone[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(6.283185307179586 * 220.0 * i / sr));

  const double ratio = lengthRatio(96.0, 120.0);  // 0.8: the loop gets shorter
  if (!checkRatio(ratio).ok()) return 1;
  const std::int64_t outFrames = static_cast<std::int64_t>(std::llround(sr * ratio));
  auto plan = makePlan(sr, outFrames, {sr / 2});  // a chop boundary at 0.5 s stays exactly on its scaled position
  if (!plan.ok()) return 1;

  const float* channels[1] = {tone.data()};
  auto out = stretch(Input{channels, 1, sr, sr}, plan.value(), Quality::Standard);
  if (!out.ok() || static_cast<std::int64_t>(out.value().size()) != outFrames) return 1;
  std::printf("%lld -> %lld frames, boundary at %lld\n", static_cast<long long>(sr), static_cast<long long>(outFrames),
              static_cast<long long>(plan.value().outBounds[1]));
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/time_stretch -B build/time_stretch && cmake --build build/time_stretch && ctest --test-dir build/time_stretch
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh time_stretch
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. Built with -ffp-contract=off (GCC/Clang) or /fp:precise (MSVC): the same binary gives bit-identical output; across compilers expect agreement within 1e-5.
- Removal: Remove the composition root's stretch API (composition/src/stretch.cpp) and the Tempo match section of the editor first; no other module depends on this one.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
