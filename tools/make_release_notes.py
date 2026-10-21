#!/usr/bin/env python3
"""Compose the GitHub release body for a beta: banner, download table, commit notes, checksum note.

  python3 tools/make_release_notes.py --version 0.2.0-beta.1 --tag v0.2.0-beta.1 --assets dist > notes.md

Commit notes come from shared/scripts/generate_release_notes.py (since the previous v* tag, if any).
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLATFORMS = [("windows-x64", "Windows x64"), ("linux-x86_64", "Linux x86_64"), ("macos-arm64", "macOS Apple silicon (arm64)"),
             ("macos-x86_64", "macOS Intel (x86_64)"), ("macos-universal", "macOS universal (arm64 + x86_64)")]
FORMATS = [("standalone", "Standalone"), ("vst3", "VST3"), ("clap", "CLAP")]


def previous_tag(tag):
    out = subprocess.run(["git", "tag", "--list", "v*", "--sort=-v:refname"], capture_output=True, text=True, cwd=ROOT).stdout.split()
    return next((t for t in out if t != tag), None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--assets", required=True)
    a = ap.parse_args()
    names = sorted(p.name for p in Path(a.assets).iterdir() if p.is_file() and not p.name.endswith(".sha256"))
    lines = [f"ChopFractal {a.version} is a **beta**. Expect rough edges; builds are not code signed or notarized.", "",
             "## Downloads", "", "| Platform | " + " | ".join(f for _, f in FORMATS) + " |", "|---|" + "---|" * len(FORMATS)]
    for pkey, plabel in PLATFORMS:
        row = []
        for fkey, _ in FORMATS:
            hit = [n for n in names if n.endswith(f"-{pkey}-{fkey}.zip") or n.endswith(f"-{pkey}-{fkey}.tar.gz")]
            row.append(f"`{hit[0]}`" if hit else "-")
        if any(c != "-" for c in row):
            lines.append(f"| {plabel} | " + " | ".join(row) + " |")
    lines += ["", "Every archive has a matching `.sha256` file, and `SHA256SUMS.txt` lists all of them. Each archive contains `README-INSTALL.txt` with install steps for that format.",
              "", "macOS builds are ad-hoc signed only: remove the quarantine flag after download (`xattr -dr com.apple.quarantine <file>`) as described in the README. Windows may show a SmartScreen warning.", ""]
    cmd = [sys.executable, str(ROOT / "shared/scripts/generate_release_notes.py"), "--version", a.version, "--project-root", str(ROOT)]
    prev = previous_tag(a.tag)
    if prev:
        cmd += ["--since", prev]
    gen = subprocess.run(cmd, capture_output=True, text=True)
    lines += ["## Changes", "", gen.stdout.strip() if gen.returncode == 0 and gen.stdout.strip() else "See the commit history for this tag."]
    print("\n".join(lines))


if __name__ == "__main__":
    main()
