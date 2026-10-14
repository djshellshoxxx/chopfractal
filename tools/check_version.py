#!/usr/bin/env python3
"""Checks the single-source product VERSION. Usage: check_version.py [--tag refs/tags/vX.Y.Z]"""
import re, sys, pathlib

PATTERN = re.compile(r"^\d+\.\d+\.\d+(-(beta|rc)\.\d+)?$")


def check(version: str, tag: str | None = None) -> list[str]:
    errors = []
    if not PATTERN.match(version):
        errors.append(f"VERSION '{version}' must be MAJOR.MINOR.PATCH with an optional -beta.N or -rc.N")
    if tag is not None and tag != f"refs/tags/v{version}":
        errors.append(f"tag '{tag}' does not match VERSION 'v{version}'")
    return errors


def main(argv: list[str]) -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    version = (root / "VERSION").read_text().strip()
    tag = argv[argv.index("--tag") + 1] if "--tag" in argv else None
    errors = check(version, tag)
    if "VERSION" not in (root / "CMakeLists.txt").read_text():
        errors.append("root CMakeLists.txt does not read VERSION")
    for e in errors:
        print("error:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
