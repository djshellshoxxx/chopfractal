# Developer Conventions

## Style

- C++17 in modules. Keep to the standard library in `modules/`.
- Headers live under `include/chopfractal/<module>/`. Include paths are the public names.
- Warnings are errors on the Linux CI job. Do not hide warnings with flags.
- Use `Result` and `Status` (from `chop_contracts`) for failures. Do not throw across module boundaries.

## Dependency and portability rules

- A module includes only what its `module.json` declares. `tools/check_includes.py` enforces this.
- Modules do not depend on JUCE. JUCE is only in `plugin/`.
- No time-stretch libraries (`rubberband`, `soundtouch`, `elastique`). CI fails on them. Use the project's own `time_stretch` module.
- Modules keep their own tests. Adding a module means `module.json`, `MIGRATION.md` (generated), tests and a root CMake entry.

## Audio thread

- The audio thread reads snapshots from `render::Mailbox`. Do not give it a reference to editable project state.
- Publish a new snapshot after every edit that affects playback. Validate before publishing.

## Determinism

- Use the RNG from `chop_contracts`. Do not use `std::rand` or time-based seeds in generation.
- A change to generation output changes saved patterns. Note it in the module README and in `docs/QA_ALPHA2.md`.

## Saved state

- Patterns, presets and state use the byte codec and state codec. Changing the format needs a version bump and a migration note.

## Gotchas

- `MIGRATION.md` and the docs in `docs/` may be generated. Check for a generator before editing.
- Live input capture is not implemented. Do not assume the source comes from the host input yet.
- `plugin/` is a thin shell. Put new behaviour in `composition/` or a module, then call it from the shell.

## How to add a feature

1. Decide the module. If none fits, add one with `module.json`.
2. Put the behaviour in the module with tests.
3. Wire it through `ProjectSession` in `composition/`.
4. Add the editor control in `plugin/` only if the UI needs it.
5. Run the repo checks and `ctest`. Update the module README and `docs/BUILD_STATUS.md`.

## Known limitations

From `docs/BUILD_STATUS.md` and the README:

- Live input capture, production GUI polish and real-host compatibility testing are not finished.
- Installers and signing are not done. Alpha artefacts are unsigned.
- The repo is a beta in development, not a finished plug-in.
