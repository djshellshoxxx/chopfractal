#!/usr/bin/env python3
"""Switch the project license identifier in one step.

    python3 tools/set_license.py <SPDX-id-or-LicenseRef>     e.g. MIT, Apache-2.0, LicenseRef-ChopFractal-Commercial

Updates LICENSE_ID and the `license` field of every module.json, then regenerates the module docs. You still
replace the text of LICENSE yourself (see docs/LICENSING.md); tools/check_modules.py fails if the identifier and
the manifests ever disagree.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def main(argv):
    if len(argv) != 2 or not argv[1].strip():
        print(__doc__)
        return 2
    ident = argv[1].strip()
    with open(os.path.join(ROOT, "LICENSE_ID"), "w") as f:
        f.write(ident + "\n")
    modules = os.path.join(ROOT, "modules")
    for name in sorted(os.listdir(modules)):
        path = os.path.join(modules, name, "module.json")
        if os.path.isfile(path):
            with open(path) as f:
                doc = json.load(f)
            doc["license"] = ident
            with open(path, "w") as f:
                json.dump(doc, f, indent=2)
                f.write("\n")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "gen_module_docs.py")])
    print("license identifier is now %s; remember to replace the text of LICENSE" % ident)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
