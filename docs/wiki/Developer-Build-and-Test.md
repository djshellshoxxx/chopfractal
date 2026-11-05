# Developer Build and Test

## Toolchain

- CMake 3.20 (modules) or 3.22 (plugin)
- C++17 compiler. Ninja is used in the examples
- Plugin build: fetches JUCE 8 with FetchContent. Linux needs the packages in `.github/workflows/ci.yml`
- Python 3 for the `tools/` checks

## Build everything

```sh
cmake -S . -B build -G Ninja -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Build one module

```sh
cmake -S modules/pattern_engine -B build/pattern_engine
cmake --build build/pattern_engine
ctest --test-dir build/pattern_engine
```

## Build the VST3 plug-in

```sh
cmake -S . -B build-plugin -G Ninja -DCHOPFRACTAL_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-plugin --target ChopFractal_VST3
tools/validate_vst3.sh build-plugin/plugin/ChopFractal_artefacts/Release/VST3/ChopFractal.vst3
```

## Repo checks (run before a PR)

```sh
python3 tools/check_modules.py
python3 tools/gen_module_docs.py --check
python3 tools/check_includes.py
python3 tools/check_version.py
python3 -I tools/tests/test_packaging.py
```

CI also fails if `rubberband`, `soundtouch` or `elastique` appear in a build file.

## Tests

Each module has a `tests/` folder with a test executable. About 1,400 test checks are in the module test files. Run them all with `ctest`. The plugin has its own tests under `plugin/tests/`, including `version_stamp_matches_VERSION_file`.

## CI (`.github/workflows/ci.yml`)

- Repo checks job (modules, docs, includes, version, packaging tests)
- Build and test matrix: Linux GCC (Ubuntu 24.04, warnings as errors), Windows MSVC, macOS universal, each with a Release build and `ctest`
- Plugin builds on each OS
- `build-packages.yml` makes packages. `release-beta.yml` makes beta releases

## Beta release

See `docs/RELEASING.md`:

1. Set `VERSION` (for example `0.2.0-beta.2`), commit, and merge to `main`.
2. Push the tag `v<VERSION>` (must match `v*-beta.*`), or run the Release beta workflow by hand with that tag.
3. The workflow runs the version, module and portable test gates, then builds Standalone, VST3 and CLAP for Windows x64 and the other platforms in CI.

## Troubleshooting

- `-Werror` failures on GCC: the Linux CI job uses warnings as errors. Fix the warning.
- Plugin configure fails on Linux: install the JUCE packages from `ci.yml`.
- `check_includes.py` fails: a module includes a header from a module it does not declare. Add the dependency to `module.json` or remove the include.
