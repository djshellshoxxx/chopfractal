#!/usr/bin/env python3
"""Package built ChopFractal binaries into one archive per format (standalone, vst3, clap) plus SHA256 files.

  python3 tools/package_artifacts.py --platform windows-x64 --artefacts build-plugin/plugin/ChopFractal_artefacts/Release \
      --out dist [--version 0.2.0-beta.1] [--suffix -abc12345]

Each archive contains the payload, LICENSE, THIRD_PARTY_NOTICES.md, VERSION and a README-INSTALL.txt with install
steps for that format and platform. Archive names: ChopFractal-<version><suffix>-<platform>-<format>.(zip|tar.gz).
Windows and macOS get .zip, Linux .tar.gz. Executable bits and symlinks are preserved. Runs on any OS (stdlib only).
"""
import argparse
import hashlib
import os
import stat
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PAYLOAD = {  # platform -> format -> path under the artefacts dir
    "windows-x64": {"standalone": "Standalone/ChopFractal.exe", "vst3": "VST3/ChopFractal.vst3", "clap": "CLAP/ChopFractal.clap"},
    "linux-x86_64": {"standalone": "Standalone/ChopFractal", "vst3": "VST3/ChopFractal.vst3", "clap": "CLAP/ChopFractal.clap"},
    "macos-arm64": {"standalone": "Standalone/ChopFractal.app", "vst3": "VST3/ChopFractal.vst3", "clap": "CLAP/ChopFractal.clap"},
    "macos-x86_64": {"standalone": "Standalone/ChopFractal.app", "vst3": "VST3/ChopFractal.vst3", "clap": "CLAP/ChopFractal.clap"},
    "macos-universal": {"standalone": "Standalone/ChopFractal.app", "vst3": "VST3/ChopFractal.vst3", "clap": "CLAP/ChopFractal.clap"},
}

INSTALL = {
    ("windows-x64", "standalone"): """Extract the zip anywhere and run ChopFractal.exe. No installer is needed.
Windows may show a SmartScreen warning because this beta build is not code signed: choose More info, then Run anyway.""",
    ("windows-x64", "vst3"): """Copy the folder ChopFractal.vst3 into C:\\Program Files\\Common Files\\VST3\\ (administrator rights are needed),
or into %LOCALAPPDATA%\\Programs\\Common\\VST3\\ for the current user only. Then rescan plugins in your DAW.
Windows 10/11, 64-bit. Keep the folder structure intact: the plugin is the .vst3 folder, not a single file.""",
    ("windows-x64", "clap"): """Copy ChopFractal.clap into C:\\Program Files\\Common Files\\CLAP\\ (or %LOCALAPPDATA%\\Programs\\Common\\CLAP\\
for the current user only). Then rescan plugins in your DAW. Windows 10/11, 64-bit.""",
    ("linux-x86_64", "standalone"): """Extract the archive and run ./ChopFractal. It needs these runtime libraries: ALSA (libasound2), libx11,
libxext, libxrandr, libxinerama, libxcursor, libfreetype and libfontconfig.
On Debian/Ubuntu: sudo apt install libasound2 libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1 libfreetype6 libfontconfig1""",
    ("linux-x86_64", "vst3"): """Copy the folder ChopFractal.vst3 to ~/.vst3/ (current user) or /usr/lib/vst3/ (all users), then rescan plugins
in your DAW. Needs the same runtime libraries as the standalone app (see below). The plugin is the .vst3 folder.
Runtime libraries on Debian/Ubuntu: sudo apt install libasound2 libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1 libfreetype6 libfontconfig1""",
    ("linux-x86_64", "clap"): """Copy ChopFractal.clap to ~/.clap/ (current user) or /usr/lib/clap/ (all users), then rescan plugins in your DAW.
Runtime libraries on Debian/Ubuntu: sudo apt install libasound2 libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1 libfreetype6 libfontconfig1""",
}
MAC_INSTALL = {
    "standalone": """Drag ChopFractal.app into /Applications (or anywhere). This beta is ad-hoc signed, not notarized, so macOS Gatekeeper
blocks it the first time after a browser download. Fix: open Terminal and run
    xattr -dr com.apple.quarantine /path/to/ChopFractal.app
or right-click the app, choose Open, then confirm. macOS 11 or newer.""",
    "vst3": """Copy the folder ChopFractal.vst3 to /Library/Audio/Plug-Ins/VST3/ (all users) or ~/Library/Audio/Plug-Ins/VST3/ (current user).
This beta is ad-hoc signed, not notarized. After a browser download, remove the quarantine flag, then rescan in your DAW:
    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ChopFractal.vst3
macOS 11 or newer. The plugin is the .vst3 folder (a bundle).""",
    "clap": """Copy ChopFractal.clap to /Library/Audio/Plug-Ins/CLAP/ (all users) or ~/Library/Audio/Plug-Ins/CLAP/ (current user).
This beta is ad-hoc signed, not notarized. After a browser download, remove the quarantine flag, then rescan in your DAW:
    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/ChopFractal.clap
macOS 11 or newer.""",
}
ARCH_NOTE = {
    "macos-arm64": "This build is for Apple silicon (arm64) Macs. Intel Macs need the macos-x86_64 or macos-universal download.",
    "macos-x86_64": "This build is for Intel (x86_64) Macs. It also runs under Rosetta 2 on Apple silicon hosts that run x86_64 plugins.",
    "macos-universal": "Universal build: runs natively on Apple silicon (arm64) and Intel (x86_64) Macs.",
}


def readme(version, platform, fmt):
    if platform.startswith("macos"):
        steps = MAC_INSTALL[fmt] + "\n\n" + ARCH_NOTE[platform]
    else:
        steps = INSTALL[(platform, fmt)]
    names = {"standalone": "stand-alone application", "vst3": "VST3 plugin", "clap": "CLAP plugin"}
    return (f"ChopFractal {version} ({platform}) - {names[fmt]}\n"
            f"{'=' * 60}\n\n"
            f"This is a beta release. Please report problems at https://github.com/djshellshoxxx/chopfractal/issues\n\n"
            f"Install\n-------\n{steps}\n\n"
            f"Verify the download\n-------------------\n"
            f"Compare the SHA256 of the archive with the .sha256 file published next to it.\n\n"
            f"Licensing\n---------\nSee LICENSE and THIRD_PARTY_NOTICES.md in this archive.\n")


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def entries(stage):
    """Yield (path, arcname) for every file, directory and symlink under stage, sorted, directories first-class."""
    for dirpath, dirnames, filenames in os.walk(stage):
        dirnames.sort()
        rel = Path(dirpath).relative_to(stage)
        for d in list(dirnames):
            full = Path(dirpath) / d
            if full.is_symlink():
                dirnames.remove(d)
                yield full, (rel / d).as_posix()
            else:
                yield full, (rel / d).as_posix() + "/"
        for f in sorted(filenames):
            yield Path(dirpath) / f, (rel / f).as_posix()


def write_zip(stage, dest):
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as z:
        for full, arc in entries(stage):
            st = os.lstat(full)
            info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            if stat.S_ISLNK(st.st_mode):
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                z.writestr(info, os.readlink(full))
            elif stat.S_ISDIR(st.st_mode):
                info.external_attr = ((stat.S_IFDIR | 0o755) << 16) | 0x10
                z.writestr(info, b"")
            else:
                mode = 0o755 if (st.st_mode & 0o111) else 0o644
                info.external_attr = (stat.S_IFREG | mode) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(full, "rb") as fh:
                    z.writestr(info, fh.read())


def write_targz(stage, dest):
    with tarfile.open(dest, "w:gz") as t:
        for full, arc in entries(stage):
            t.add(full, arcname=arc.rstrip("/"), recursive=False)


def copy_tree(src, dst):
    import shutil
    if src.is_dir() and not src.is_symlink():
        shutil.copytree(src, dst, symlinks=True)
    else:
        shutil.copy2(src, dst, follow_symlinks=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", required=True, choices=sorted(PAYLOAD))
    ap.add_argument("--artefacts", required=True, help="dir containing Standalone/, VST3/, CLAP/")
    ap.add_argument("--out", required=True)
    ap.add_argument("--version", help="default: contents of VERSION")
    ap.add_argument("--suffix", default="", help="appended to the version in file names, e.g. -abc12345 for CI builds")
    ap.add_argument("--formats", default="standalone,vst3,clap")
    a = ap.parse_args()
    version = a.version or (ROOT / "VERSION").read_text().strip()
    art, out = Path(a.artefacts), Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    import shutil
    import tempfile
    for fmt in a.formats.split(","):
        src = art / PAYLOAD[a.platform][fmt]
        if not (src.exists() or src.is_symlink()):
            sys.exit(f"missing build output: {src}")
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp) / "ChopFractal"
            stage.mkdir()
            copy_tree(src, stage / src.name)
            shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
            shutil.copy2(ROOT / "THIRD_PARTY_NOTICES.md", stage / "THIRD_PARTY_NOTICES.md")
            (stage / "VERSION").write_text(version + "\n")
            (stage / "README-INSTALL.txt").write_text(readme(version, a.platform, fmt), encoding="utf-8", newline="\n")
            base = f"ChopFractal-{version}{a.suffix}-{a.platform}-{fmt}"
            if a.platform.startswith("linux"):
                dest = out / (base + ".tar.gz")
                write_targz(stage, dest)
            else:
                dest = out / (base + ".zip")
                write_zip(stage, dest)
        (out / (dest.name + ".sha256")).write_text(f"{sha256_of(dest)}  {dest.name}\n", encoding="ascii", newline="\n")
        print(dest)


if __name__ == "__main__":
    main()
