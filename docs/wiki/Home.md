# ChopFractal Wiki

ChopFractal is a deterministic loop-chopping and pattern-generation VST3 audio effect. Load or capture a loop, chop it (automatically or by hand) and generate rhythmic variations through a phrase, bar, beat and hit hierarchy. The same seed always gives the same pattern. Locks keep the parts you like while the rest mutates. Any hit can be zoomed into to make a nested rhythm.

**Status:** developing, version 0.2.0-beta.1. Alpha builds are unsigned and published from CI. Live input capture, production GUI polish and real-host compatibility testing are not finished (see `docs/BUILD_STATUS.md`).

**Who it is for:** producers who want reproducible rhythmic variations from their own loops, and developers who want to work on the modular engine.

## Quick start

```sh
cmake -S . -B build -G Ninja -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Each module also builds on its own. The VST3 plugin build is in [[Developer-Build-and-Test]].

## Pages

**For users**
- [[User-Guide]]: the features, the phrase → bar → beat → hit model, export and what is not finished

**For developers**
- [[Developer-Overview]]: the module architecture, why it is split this way, data flow
- [[Developer-Code-Map]]: modules, headers, and where to change what
- [[Developer-Build-and-Test]]: build, tests, CI, beta release
- [[Developer-Conventions]]: portability rules, dependency rules, gotchas

**Repo docs (authoritative)**
- `docs/MODULARITY_MAP.md`, `docs/BUILD_STATUS.md`, `docs/QA_ALPHA2.md`, `docs/RELEASING.md`, `docs/LICENSING.md`
- `docs/specs/` (specifications), `IP/README.md` (public-safe IP records)
- Each module has its own `README.md` and `MIGRATION.md`
