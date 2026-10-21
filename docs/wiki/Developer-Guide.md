# Developer Guide

## Portable core build

These commands were run in a documentation pass on Linux with Ninja. The result was 36 of 36 CTest tests passing.

```sh
cmake -S . -B build -G Ninja -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Single module

Each module under `modules/` builds alone with only its declared dependencies:

```sh
cmake -S modules/pattern_engine -B build/pattern_engine && cmake --build build/pattern_engine && ctest --test-dir build/pattern_engine
```

## VST3 plugin

The plugin target fetches JUCE 8 and is optional (`-DCHOPFRACTAL_BUILD_PLUGIN=ON`). On Linux, install the packages listed in `.github/workflows/ci.yml`. This pass did not build the plugin. Validation uses `tools/validate_vst3.sh`.

## Reuse

`python3 tools/transfer_module.py <module> <project>` copies a module and its declared dependencies. `tools/smoke_transfer.sh` checks that every module builds in an empty project. Each module has a `MIGRATION.md`.

## Licence

All rights reserved for now. See [LICENSING](../LICENSING.md). JUCE is AGPLv3 or commercial.
