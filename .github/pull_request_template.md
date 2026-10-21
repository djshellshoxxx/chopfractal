## What and why

<!-- One paragraph. Link the spec in docs/specs/roadmap/ if there is one. -->

## Quality gates (docs/specs/roadmap/README.md section 6)

Run `tools/ci_local.sh` (add `--plugin` when plugin/ changed, `--tsan` when audio-thread code changed) and tick what you ran.

- [ ] Warnings-as-errors build + `ctest`, GCC and Clang, and a Release build
- [ ] ASan + UBSan build + `ctest` with `UBSAN_OPTIONS=halt_on_error=1`
- [ ] TSan on the renderer (only if audio-thread code changed)
- [ ] `python3 tools/check_modules.py && python3 tools/check_includes.py`; `tools/gen_module_docs.py` committed if manifests changed
- [ ] `tools/smoke_transfer.sh` (only if a portable module was added or its dependencies changed)
- [ ] Plugin: `ChopFractal_VST3` + `ChopFractalShellTests` built; shell tests pass with `env -u DISPLAY` and under `xvfb-run`; `tools/validate_vst3.sh` passes; editor snapshot rendered and looked at for any UI change
- [ ] Golden pattern hashes unchanged; allocation-free `process()` test passes; new audio-thread code has an allocation test

## Definition of done

- [ ] Every acceptance criterion in the spec is checked off below or explained
- [ ] Regression test for every bug found
- [ ] `docs/BUILD_STATUS.md`, README feature list and module docs updated; spec status updated
- [ ] Hot files touched (and why): <!-- PluginProcessor, project_session.hpp, parameters.cpp, CMakeLists, ci.yml -->
- [ ] Registry claims (parameter ids, state sections, event flag bits): <!-- or "none" -->
