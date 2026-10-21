# Migrating `level_match` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `level_match` 0.1.0 (API 0.1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/level_match/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py level_match /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/level_match)
target_link_libraries(your_target PRIVATE chopfractal::level_match)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

The module does no rendering and holds no state; caching and scheduling are the caller's job.

Optional adapters: Render the pattern offline with the loop's own sample rate, measure both, then feed the two `Loudness` values to `computeMatch`.

## 5. State import and export

This module stores no project state.

## 6. Minimal compile and usage example

```cpp
// Measures a 1 kHz tone at two levels and computes the gain that brings the quieter one to the louder one.
#include <chopfractal/level_match/level_match.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  const int rate = 48000;
  std::vector<float> loud(rate * 3), quiet(rate * 3);
  for (int i = 0; i < rate * 3; ++i) {
    const float s = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * i / rate));
    loud[static_cast<std::size_t>(i)] = 0.5f * s;
    quiet[static_cast<std::size_t>(i)] = 0.2f * s;
  }
  const float* l[1] = {loud.data()};
  const float* q[1] = {quiet.data()};
  const auto a = chopfractal::levelmatch::measure(l, 1, rate * 3, rate);
  const auto b = chopfractal::levelmatch::measure(q, 1, rate * 3, rate);
  const auto m = chopfractal::levelmatch::computeMatch(a, b, 0.0, true, -1.0);
  std::printf("loud %.2f LUFS, quiet %.2f LUFS, gain %.1f dB\n", a.lufs, b.lufs, m.gainDb);
  return m.valid && std::fabs(m.gainDb - 8.0) < 0.2 ? 0 : 1;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/level_match -B build/level_match && cmake --build build/level_match && ctest --test-dir build/level_match
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh level_match
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler. No dependencies at all.
- Removal: Delete the folder; callers lose Level match only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
