# Migrating `plugin_ui_adapter` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `plugin_ui_adapter` 0.1.0 (API 0.1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/source_chop/` (^0.1)
- `modules/pattern_engine/` (^0.1)
- `modules/variation_history/` (^0.1)
- `modules/plugin_ui_adapter/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py plugin_ui_adapter /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/source_chop)
add_subdirectory(modules/pattern_engine)
add_subdirectory(modules/variation_history)
add_subdirectory(modules/plugin_ui_adapter)
target_link_libraries(your_target PRIVATE chopfractal::plugin_ui_adapter)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Draw the view structs with your toolkit; route user actions to the composition root, not into the models.

Optional adapters: The JUCE editor draws these structures and sends commands to the composition root.

## 5. State import and export

This module stores no project state.

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: build a pattern view model and an accessible description for each hit.
#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  ChopSnapshot chops;
  chops.sourceFrames = 48000;
  chops.sampleRate = 48000;
  chops.chops.push_back({ChopId{1}, {0, 24000}, 0, 0, 0});
  chops.chops.push_back({ChopId{2}, {24000, 48000}, 0, 0, 0});

  pattern::Settings settings;
  settings.bars = 1;
  settings.density = 1.0;
  auto p = pattern::generate(chops, settings);
  if (!p.ok()) return 1;

  const ui::PatternView view = ui::buildPatternView(p.value(), chops);
  if (view.rects.empty()) return 1;
  std::printf("%zu hits; first: %s\n", view.rects.size(), view.rects.front().description.c_str());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/plugin_ui_adapter -B build/plugin_ui_adapter && cmake --build build/plugin_ui_adapter && ctest --test-dir build/plugin_ui_adapter
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh plugin_ui_adapter
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; the editor loses its view logic only.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
