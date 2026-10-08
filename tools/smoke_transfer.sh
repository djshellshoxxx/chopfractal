#!/usr/bin/env bash
# Clean-consumer migration smoke test (docs/specs/component-portability-protocol.md section 9).
# For each module: transfer it with its declared dependencies into an EMPTY project, build that project
# with a tiny consumer that includes the module's public header, run the module's own tests, and run the
# consumer. Proves a module builds outside this repository using only what its manifest declares.
#
#   tools/smoke_transfer.sh                # every module
#   tools/smoke_transfer.sh pattern_engine # one module
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/chopfractal-transfer.XXXXXX")"
trap 'rm -rf "$work"' EXIT

if [ "$#" -gt 0 ]; then ids=("$@"); else mapfile -t ids < <(python3 - <<EOF
import sys
sys.path.insert(0, "$here/tools")
import gen_module_docs
print("\n".join(sorted(gen_module_docs.load_all())))
EOF
); fi

failed=0
for id in "${ids[@]}"; do
  proj="$work/$id"
  mkdir -p "$proj"
  python3 "$here/tools/transfer_module.py" "$id" "$proj" >/dev/null

  header="$(python3 - <<EOF
import json
print(json.load(open("$here/modules/$id/module.json"))["public_headers"][0])
EOF
)"
  cat > "$proj/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(consumer CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
set(CHOPFRACTAL_BUILD_TESTS ON)
add_subdirectory(modules/$id)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE chopfractal::$id)
add_test(NAME consumer COMMAND consumer)
EOF
  printf '#include <chopfractal/%s/%s>\nint main() { return 0; }\n' "$id" "$(basename "$header")" > "$proj/main.cpp"

  if cmake -S "$proj" -B "$proj/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >"$proj/configure.log" 2>&1 \
     && cmake --build "$proj/build" >"$proj/build.log" 2>&1 \
     && ctest --test-dir "$proj/build" --output-on-failure >"$proj/test.log" 2>&1; then
    echo "PASS  $id  ($(grep -c 'Passed' "$proj/test.log") tests passed in a clean consumer project)"
  else
    echo "FAIL  $id"
    tail -20 "$proj/configure.log" "$proj/build.log" "$proj/test.log" 2>/dev/null || true
    failed=1
  fi
done
exit $failed
