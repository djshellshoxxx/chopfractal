# Releasing ChopFractal

Status: build-side work of F15 is done (version stamp, license gate, logging). Signing, notarization, installers and the
release workflow need maintainer secrets and decisions and are **not built yet**.

## Done in the repo
- `VERSION` (one line, `MAJOR.MINOR.PATCH[-beta.N|-rc.N]`) is the only product version. The root `CMakeLists.txt` reads it;
  the plugin gets `VERSION`, `BUILD_VERSION` (`-DCHOPFRACTAL_BUILD_NUMBER`), bundle id `com.chopfractal.ChopFractal`, and a
  generated `BuildInfo.h` (version, build number, git sha, release flag). `tools/check_version.py [--tag refs/tags/vX.Y.Z]` runs in CI.
- License gate: `-DCHOPFRACTAL_RELEASE=ON` fails unless `-DCHOPFRACTAL_JUCE_LICENSE=COMMERCIAL` or `AGPL` (AGPL also needs
  `LICENSE_ID` to contain `AGPL-3.0`). See `docs/LICENSING.md`.
- Log: `plugin/src/Log.*` writes `<app data>/ChopFractal/logs/chopfractal-YYYYMMDD.log` (1 MiB files, 7 kept, flushed per line,
  unclean-exit marker). No process-wide crash handler is installed.
- CI builds unsigned VST3/CLAP alpha packages on Linux, Windows and macOS.

## Needs the maintainer (not done)
1. Apple Developer ID Application + Installer certificates and an App Store Connect API key (signing, notarization, `.pkg`).
2. Windows signing route (Azure Trusted Signing or an OV/EV certificate) and an Inno Setup installer.
3. JUCE license decision, final `LICENSE`/EULA text, publisher name.
4. `release.yml` (tag-triggered, `environment: release`), `tools/package_*`, `verify_bundle.py`, `docs/third_party/NOTICES.md` +
   `check_notices.py`, About box, manual DAW matrix (`docs/COMPATIBILITY.md`). Full design: `docs/specs/roadmap/F15-cross-platform-and-packaging.md`.
5. MSVC `/W4` warnings are not yet fatal in CI.
