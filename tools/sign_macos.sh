#!/usr/bin/env bash
# Sign macOS bundles/apps. With MACOS_SIGN_IDENTITY set (a Developer ID Application identity in an unlocked keychain)
# it signs with the hardened runtime and a secure timestamp. Without it, it ad-hoc signs ("-") so the binaries at least
# carry a valid signature and run on Apple silicon (unsigned arm64 code is killed by the kernel).
#   tools/sign_macos.sh ChopFractal.vst3 ChopFractal.clap ChopFractal.app
set -euo pipefail
[ "$#" -ge 1 ] || { echo "usage: $0 <bundle>..." >&2; exit 2; }
identity="${MACOS_SIGN_IDENTITY:-}"
for b in "$@"; do
  if [ -n "$identity" ]; then
    codesign --force --deep --options runtime --timestamp --sign "$identity" "$b"
  else
    codesign --force --deep --sign - "$b"
  fi
  codesign --verify --strict --deep --verbose=2 "$b"
done
