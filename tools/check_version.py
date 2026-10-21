#!/usr/bin/env python3
"""Validate the VERSION file and optionally that a git tag matches it.

  python3 tools/check_version.py                         # print the version, exit 0 when VERSION is well formed
  python3 tools/check_version.py --tag v0.2.0-beta.1     # also require tag == "v" + VERSION (refs/tags/ prefix allowed)
  python3 tools/check_version.py --github-output         # also write version/version_num to $GITHUB_OUTPUT
"""
import argparse
import os
import re
import sys
from pathlib import Path

PATTERN = re.compile(r"^(\d+\.\d+\.\d+)(-(?:alpha|beta|rc)\.\d+)?$")
ROOT = Path(__file__).resolve().parent.parent


def read_version(path=None):
    text = (path or ROOT / "VERSION").read_text(encoding="utf-8").strip()
    m = PATTERN.match(text)
    if not m:
        raise SystemExit(f"VERSION '{text}' must look like 1.2.3 or 1.2.3-beta.4 (alpha/beta/rc)")
    return text, m.group(1)


def check_tag(tag, version):
    tag = tag.removeprefix("refs/tags/")
    if tag != "v" + version:
        raise SystemExit(f"tag '{tag}' does not match VERSION '{version}' (expected 'v{version}')")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag")
    ap.add_argument("--github-output", action="store_true")
    a = ap.parse_args()
    full, num = read_version()
    if a.tag:
        check_tag(a.tag, full)
    print(full)
    if a.github_output and os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as f:
            f.write(f"version={full}\nversion_num={num}\n")


if __name__ == "__main__":
    main()
