#!/usr/bin/env python3
"""Copy one module and its declared dependencies into another project.

    python3 tools/transfer_module.py <module_id> <destination_dir>

Copies modules/<id>/ and, recursively, only the modules named in module.json (dependencies first). Never
copies build output, VCS data, or unrelated project code. Writes CHOPFRACTAL_IMPORT.md recording the
imported versions so local patches can be reconciled later. Existing destination folders are refused
unless --force is given.
"""
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import gen_module_docs  # noqa: E402

IGNORE = shutil.ignore_patterns("build", "build-*", "out", ".git", "*.o", "*.a", "*.obj", "*.lib", "*.so", "*.dylib", "*.exe", "CMakeCache.txt", "CMakeFiles", "__pycache__")


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    force = "--force" in argv
    if len(args) != 2:
        print(__doc__)
        return 2
    mid, dest = args
    mods = gen_module_docs.load_all()
    if mid not in mods:
        print("unknown module '%s'; available: %s" % (mid, ", ".join(sorted(mods))))
        return 2
    chain = gen_module_docs.closure(mods, mid)
    os.makedirs(os.path.join(dest, "modules"), exist_ok=True)
    for c in chain:
        target = os.path.join(dest, "modules", c)
        if os.path.exists(target):
            if not force:
                print("%s already exists; use --force to overwrite" % target)
                return 1
            shutil.rmtree(target)
        shutil.copytree(os.path.join(gen_module_docs.MODULES, c), target, ignore=IGNORE)
        print("copied modules/%s" % c)
    with open(os.path.join(dest, "CHOPFRACTAL_IMPORT.md"), "a") as f:
        f.write("\n## Imported %s\n\n" % time.strftime("%Y-%m-%d"))
        for c in chain:
            f.write("- `%s` %s (API %s)\n" % (c, mods[c]["version"], mods[c]["api_version"]))
        f.write("\nLocal patches: none. Reconcile upstream fixes by cherry-pick, not by overwriting.\n")
    print("\nAdd to your CMake (dependency order):")
    for c in chain:
        print("  add_subdirectory(modules/%s)" % c)
    print("  target_link_libraries(your_target PRIVATE chopfractal::%s)" % mid)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
