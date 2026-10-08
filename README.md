# ChopFractal

A deterministic loop-chopping and pattern-generation **VST3 audio effect**. Load or capture a loop, chop it
(automatically or by hand), and generate rhythmic variations through a visible phrase → bar → beat → hit
hierarchy. A fixed seed always reproduces the same pattern; locks keep the parts you like while the rest
mutates; any hit can be zoomed into to create a nested rhythm inside it; and every variation is kept as a
branch you can return to.

**Where the build stands:** see [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md). Specifications live in
[`docs/specs/`](docs/specs/README.md).

## Layout

| Path | What it is |
|---|---|
| `modules/<id>/` | Ten independently buildable, portable C++17 modules, each with tests, a manifest (`module.json`), and a generated migration guide |
| `composition/` | The headless composition root (`ProjectSession`) that wires the modules together |
| `plugin/` | The JUCE VST3 shell: thin glue over the composition root (optional build) |
| `tools/` | Portability checker, doc generator, module transfer script, clean-consumer smoke test, VST3 validator runner |

## Build and test

```sh
cmake -S . -B build -G Ninja -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Any single module builds on its own, with only its declared dependencies:

```sh
cmake -S modules/pattern_engine -B build/pattern_engine && cmake --build build/pattern_engine && ctest --test-dir build/pattern_engine
```

The VST3 plugin (fetches JUCE 8; Linux needs the packages listed in `.github/workflows/ci.yml`):

```sh
cmake -S . -B build-plugin -G Ninja -DCHOPFRACTAL_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-plugin --target ChopFractal_VST3
tools/validate_vst3.sh build-plugin/plugin/ChopFractal_artefacts/Release/VST3/ChopFractal.vst3
```

## Reusing a module elsewhere

```sh
python3 tools/transfer_module.py pattern_engine /path/to/your/project   # copies the module and only its declared dependencies
tools/smoke_transfer.sh                                                  # proves every module builds in an empty project
```

Each module's `MIGRATION.md` lists the exact steps. JUCE is AGPLv3/commercial and the project's own license
is not yet chosen; see the open decisions in [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md).
