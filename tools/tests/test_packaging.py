#!/usr/bin/env python3
"""Script tests for the version and packaging helpers (run in CI `portability`): python3 -I tools/tests/test_packaging.py"""
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
import check_version  # noqa: E402
import package_artifacts  # noqa: E402


class CheckVersion(unittest.TestCase):
    def test_accepts_release_and_prerelease(self):
        for v in ("0.1.0", "0.2.0-beta.1", "10.20.30-rc.12"):
            with tempfile.TemporaryDirectory() as d:
                p = Path(d) / "VERSION"
                p.write_text(v + "\n")
                self.assertEqual(check_version.read_version(p)[0], v)

    def test_rejects_malformed(self):
        for v in ("0.1", "v0.1.0", "0.1.0-beta", "0.1.0-beta.x", "0.1.0 beta.1", ""):
            with tempfile.TemporaryDirectory() as d:
                p = Path(d) / "VERSION"
                p.write_text(v + "\n")
                with self.assertRaises(SystemExit):
                    check_version.read_version(p)

    def test_tag_must_match(self):
        check_version.check_tag("v0.2.0-beta.1", "0.2.0-beta.1")
        check_version.check_tag("refs/tags/v0.2.0-beta.1", "0.2.0-beta.1")
        for bad in ("0.2.0-beta.1", "v0.2.0-beta.2", "v0.2.0"):
            with self.assertRaises(SystemExit):
                check_version.check_tag(bad, "0.2.0-beta.1")

    def test_repo_version_file_is_valid(self):
        check_version.read_version()


class Packaging(unittest.TestCase):
    def build_tree(self, root, exe_name):
        (root / "Standalone").mkdir(parents=True)
        (root / "Standalone" / exe_name).write_bytes(b"x")
        (root / "Standalone" / exe_name).chmod(0o755)
        (root / "VST3" / "ChopFractal.vst3" / "Contents").mkdir(parents=True)
        (root / "VST3" / "ChopFractal.vst3" / "Contents" / "f").write_bytes(b"y")
        (root / "CLAP").mkdir()
        (root / "CLAP" / "ChopFractal.clap").write_bytes(b"z")

    def run_pkg(self, platform, exe):
        with tempfile.TemporaryDirectory() as d:
            art, out = Path(d) / "art", Path(d) / "out"
            self.build_tree(art, exe)
            sys.argv = ["x", "--platform", platform, "--artefacts", str(art), "--out", str(out), "--version", "9.9.9-beta.3"]
            package_artifacts.main()
            return sorted(p.name for p in out.iterdir()), out, d

    def test_zip_platforms(self):
        names, _, _ = self.run_pkg("windows-x64", "ChopFractal.exe")
        for fmt in ("standalone", "vst3", "clap"):
            self.assertIn(f"ChopFractal-9.9.9-beta.3-windows-x64-{fmt}.zip", names)
            self.assertIn(f"ChopFractal-9.9.9-beta.3-windows-x64-{fmt}.zip.sha256", names)

    def test_zip_contents_and_checksum(self):
        with tempfile.TemporaryDirectory() as d:
            art, out = Path(d) / "art", Path(d) / "out"
            self.build_tree(art, "ChopFractal.exe")
            sys.argv = ["x", "--platform", "windows-x64", "--artefacts", str(art), "--out", str(out), "--version", "1.0.0"]
            package_artifacts.main()
            z = out / "ChopFractal-1.0.0-windows-x64-vst3.zip"
            with zipfile.ZipFile(z) as zf:
                names = zf.namelist()
            for need in ("LICENSE", "THIRD_PARTY_NOTICES.md", "README-INSTALL.txt", "ChopFractal.vst3/Contents/f"):
                self.assertIn(need, names)
            line = (out / (z.name + ".sha256")).read_text().split()
            self.assertEqual(line[0], package_artifacts.sha256_of(z))
            self.assertEqual(line[1], z.name)

    def test_linux_targz_keeps_exec_bit(self):
        with tempfile.TemporaryDirectory() as d:
            art, out = Path(d) / "art", Path(d) / "out"
            self.build_tree(art, "ChopFractal")
            sys.argv = ["x", "--platform", "linux-x86_64", "--artefacts", str(art), "--out", str(out), "--version", "1.0.0"]
            package_artifacts.main()
            with tarfile.open(out / "ChopFractal-1.0.0-linux-x86_64-standalone.tar.gz") as tf:
                self.assertTrue(tf.getmember("ChopFractal").mode & 0o111)

    def test_every_platform_format_has_install_text(self):
        for plat in package_artifacts.PAYLOAD:
            for fmt in ("standalone", "vst3", "clap"):
                self.assertIn("Install", package_artifacts.readme("1.0.0", plat, fmt))


if __name__ == "__main__":
    unittest.main()
