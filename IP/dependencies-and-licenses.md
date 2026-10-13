# Dependencies and licenses

Observed 2026-10-10 from `plugin/CMakeLists.txt`, `tools/validate_vst3.sh`, `.github/workflows/ci.yml`, and `modules/*/module.json` (`third_party` is empty for all 15 modules).

| Item | Used for | How obtained | License (as understood; verify) | Obligations / open questions |
|---|---|---|---|---|
| JUCE 8.0.15 | plugin shell, GUI, audio file decoding, VST3 wrapper | CMake FetchContent from github.com/juce-framework/JUCE at tag 8.0.15 | AGPLv3 or commercial (JUCE license) | A distributed binary is either AGPLv3-compliant (source offer) or needs a JUCE commercial license. **Unresolved**; see `docs/LICENSING.md`. |
| VST3 SDK (bundled in JUCE) | VST3 interface | inside JUCE | Steinberg VST3 terms (dual GPLv3 / proprietary at the time of writing; verify current terms) | Confirm distribution terms and trademark/logo rules for "VST". |
| Steinberg VST3 SDK 3.8.0 `validator` | CI/validation only | cloned in `tools/validate_vst3.sh` (tag v3.8.0_build_66) | per Steinberg SDK license | Not shipped; tool only. |
| GitHub Actions runners / actions | CI | `.github/workflows/ci.yml` | n/a | none for source |
| Compilers (GCC, Clang, MSVC, Apple Clang), CMake, Python 3 | build/tools | system | various | none for source |
| Project code | all modules | authored in this repo | "All Rights Reserved" (`LICENSE`, `LICENSE_ID` = `LicenseRef-ChopFractal-Proprietary`) | The repo is public with no open license: viewing/forking under GitHub's terms only; no other grant. |
| Datasets / models | none | n/a | n/a | `chop_insight` is rule-based; no ML models or datasets are used. |

Reviewed for copied code: no third-party source files are vendored in the tree (only manifests/scripts reference external tools). **Not verified:** that no snippet was copied from elsewhere by an AI-assisted session; see provenance.
