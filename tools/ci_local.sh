#!/usr/bin/env bash
# One command for the local quality gates of docs/specs/roadmap/README.md section 6, steps 1 to 5 (and 6 with --plugin).
#
#   tools/ci_local.sh                run steps 1, 2, 4 and 5 (GCC and Clang with warnings as errors, Release, ASan/UBSan,
#                                    the module/include checks and the transfer smoke test)
#   tools/ci_local.sh --quick        GCC with warnings as errors plus the module/include checks only
#   tools/ci_local.sh --tsan         also run ThreadSanitizer on the renderer (step 3; when audio-thread code changed)
#   tools/ci_local.sh --plugin       also build the JUCE shell and run step 6 (needs JUCE, see below)
#
# Environment:
#   CF_JOBS               parallel build jobs (default: all cores)
#   CHOPFRACTAL_JUCE_DIR  a local JUCE checkout (otherwise the plugin build fetches the pinned tag)
#   CHOPFRACTAL_CLAP_EXT_DIR  a local clap-juce-extensions checkout (enables the CLAP build)
#   CHOPFRACTAL_SNAPSHOT_REF  a reference editor PNG; the layout test then demands an identical render
#
# Build trees go to build/ci-* (git-ignored). The script stops at the first failing step.
set -euo pipefail
cd "$(dirname "$0")/.."

quick=0 tsan=0 plugin=0
for arg in "$@"; do
  case "$arg" in
    --quick) quick=1 ;;
    --tsan) tsan=1 ;;
    --plugin) plugin=1 ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg (see --help)" >&2; exit 2 ;;
  esac
done
jobs="${CF_JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2) )}"
step() { printf '\n==== %s ====\n' "$*"; }

build_and_test() {  # dir, compiler pair, extra cmake args...
  local dir="$1" cc="$2" cxx="$3"
  shift 3
  CC="$cc" CXX="$cxx" cmake -S . -B "$dir" -G Ninja "$@" >/dev/null
  cmake --build "$dir" -j "$jobs"
  ctest --test-dir "$dir" --output-on-failure
}

step "module and include checks (step 4)"
python3 tools/check_modules.py
python3 tools/gen_module_docs.py --check
python3 tools/check_includes.py

step "GCC, warnings as errors (step 1)"
build_and_test build/ci-gcc gcc g++ -DCMAKE_BUILD_TYPE=Debug -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON

if [ "$quick" = 0 ]; then
  if command -v clang++ >/dev/null 2>&1; then
    step "Clang, warnings as errors (step 1)"
    build_and_test build/ci-clang clang clang++ -DCMAKE_BUILD_TYPE=Debug -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON
  else
    echo "clang++ not found: skipping the Clang build" >&2
  fi
  step "Release (step 1)"
  build_and_test build/ci-release gcc g++ -DCMAKE_BUILD_TYPE=Release -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON

  step "ASan + UBSan, halting on the first finding (step 2)"
  CC=gcc CXX=g++ cmake -S . -B build/ci-asan -G Ninja -DCHOPFRACTAL_SANITIZE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
  cmake --build build/ci-asan -j "$jobs"
  ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ci-asan --output-on-failure

  step "transfer smoke test (step 5)"
  tools/smoke_transfer.sh
fi

if [ "$tsan" = 1 ]; then
  step "ThreadSanitizer on the renderer (step 3)"
  cmake -S modules/audio_renderer -B build/ci-tsan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" >/dev/null
  cmake --build build/ci-tsan --target test_audio_renderer -j "$jobs"
  TSAN_OPTIONS=halt_on_error=1 ./build/ci-tsan/test_audio_renderer
fi

if [ "$plugin" = 1 ]; then
  step "JUCE shell: build, shell tests headless and under xvfb, VST3 validator (step 6)"
  args=(-DCHOPFRACTAL_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON)
  [ -n "${CHOPFRACTAL_JUCE_DIR:-}" ] && args+=("-DCHOPFRACTAL_JUCE_DIR=$CHOPFRACTAL_JUCE_DIR")
  targets=(ChopFractal_VST3 ChopFractalShellTests)
  if [ -n "${CHOPFRACTAL_CLAP_EXT_DIR:-}" ]; then
    args+=(-DCHOPFRACTAL_BUILD_CLAP=ON "-DCHOPFRACTAL_CLAP_EXT_DIR=$CHOPFRACTAL_CLAP_EXT_DIR")
    targets+=(ChopFractal_CLAP)
  fi
  cmake -S . -B build/ci-plugin -G Ninja "${args[@]}" >/dev/null
  cmake --build build/ci-plugin -j "$jobs" --target "${targets[@]}"
  tests=build/ci-plugin/plugin/ChopFractalShellTests_artefacts/Release/ChopFractalShellTests
  env -u DISPLAY "$tests"
  if command -v xvfb-run >/dev/null 2>&1; then
    snapshot=build/ci-plugin/editor-snapshot.png
    CHOPFRACTAL_SNAPSHOT="$snapshot" CHOPFRACTAL_REQUIRE_DISPLAY=1 xvfb-run -a "$tests"
    echo "editor snapshot written to $snapshot: open it and check the layout for any UI change"
  else
    echo "xvfb-run not found: the GUI tests were skipped" >&2
  fi
  tools/validate_vst3.sh build/ci-plugin/plugin/ChopFractal_artefacts/Release/VST3/ChopFractal.vst3
fi

printf '\nAll requested gates passed.\n'
