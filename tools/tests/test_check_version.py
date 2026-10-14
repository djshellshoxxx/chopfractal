import pathlib, sys, unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from check_version import check


class CheckVersion(unittest.TestCase):
    def test_accepts_release_and_prerelease(self):
        self.assertEqual(check("0.1.0"), [])
        self.assertEqual(check("0.2.0-beta.1"), [])

    def test_rejects_malformed(self):
        for bad in ("v0.1", "0.1", "1.0.0-alpha", "1.0.0.0"):
            self.assertTrue(check(bad), bad)

    def test_tag_must_match(self):
        self.assertEqual(check("0.1.0", "refs/tags/v0.1.0"), [])
        self.assertTrue(check("0.1.0", "refs/tags/v0.2.0"))


if __name__ == "__main__":
    unittest.main()
