# Releasing a beta

`VERSION` (repo root) is the only place the version is typed, e.g. `0.2.0-beta.1`. CMake reads it: the numeric part
becomes the plugin/bundle version (`CFBundleShortVersionString`, VST3/CLAP version), the full string goes into
`BuildInfo.h` (shown top right in the editor, checked by the shell test `version_stamp_matches_VERSION_file`) and into
every archive name.

## Cut a beta
1. Set `VERSION` (for example `0.2.0-beta.2`), commit, merge to `main`.
2. Either push the tag `git tag v0.2.0-beta.2 && git push origin v0.2.0-beta.2`, or run the **Release beta** workflow
   manually and enter the tag. The tag must be `v` + `VERSION`, and match `v*-beta.*`.
3. The workflow gates (version, module checks, portable tests), builds Standalone, VST3 and CLAP for Windows x64, Linux
   x86_64, macOS arm64, macOS x86_64 and a macOS universal merge, runs the shell tests on each, creates a **draft**
   prerelease, uploads 15 archives, 15 `.sha256` files and `SHA256SUMS.txt`, downloads them again and compares
   checksums, and only then publishes it as a prerelease (never "latest").
4. A failed run leaves at most a draft; re-running replaces the draft. A published release for a tag is never replaced:
   bump `VERSION` and use a new tag.

Each CI run on every push/PR (`ci.yml`, job `packages`) builds the same artifacts, named `ChopFractal-<version>-<sha8>-...`,
kept 14 days.

## Signing
Without secrets: macOS bundles are ad-hoc signed (so they run on Apple silicon; users remove the quarantine flag, see
`README-INSTALL.txt` in each archive), Windows binaries are unsigned (SmartScreen warns). Optional secrets switch the
signing steps on: `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, `MACOS_SIGN_IDENTITY` (Developer ID Application
identity name), `WIN_CERT_PFX_BASE64`, `WIN_CERT_PASSWORD`. Notarization and installers (`.pkg`, Inno Setup) are not
part of this pipeline yet; they need Apple/Windows credentials (see `docs/specs/roadmap/F15-cross-platform-and-packaging.md`).

## Linux runtime libraries
Build needs: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
libfreetype-dev libfontconfig1-dev` (plus `xvfb` for the editor tests, `libgtk-3-dev libgtkmm-3.0-dev` for the validator
build). Users need the matching runtime libraries (listed in each Linux archive's `README-INSTALL.txt`).

## Licensing
Releases are still all rights reserved. Settle the JUCE license route in `docs/LICENSING.md` before a public beta.
