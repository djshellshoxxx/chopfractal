#!/usr/bin/env bash
# Merge an arm64 and an x86_64 build tree (Standalone/, VST3/, CLAP/ under each) into one universal tree.
# Every Mach-O file found in the arm64 tree is lipo-merged with its twin; everything else is taken from arm64.
# The result is ad-hoc (or Developer ID, see sign_macos.sh) signed by the caller afterwards.
#   tools/make_universal_macos.sh <arm64-artefacts-dir> <x86_64-artefacts-dir> <out-dir>
set -euo pipefail
[ "$#" -eq 3 ] || { echo "usage: $0 <arm64-dir> <x86_64-dir> <out-dir>" >&2; exit 2; }
arm="$1"; x86="$2"; out="$3"
rm -rf "$out"; mkdir -p "$out"
cp -R "$arm"/. "$out"/
merged=0
while IFS= read -r f; do
  rel="${f#"$out"/}"
  [ -f "$x86/$rel" ] || { echo "no x86_64 twin for $rel" >&2; exit 1; }
  lipo -create "$arm/$rel" "$x86/$rel" -output "$f"
  echo "universal: $rel ($(lipo -archs "$f"))"
  merged=$((merged + 1))
done < <(find "$out" -type f ! -type l -exec sh -c 'file -b "$1" | grep -q "Mach-O" && echo "$1"' _ {} \;)
[ "$merged" -gt 0 ] || { echo "no Mach-O files found to merge" >&2; exit 1; }
