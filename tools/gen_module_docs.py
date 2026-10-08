#!/usr/bin/env python3
"""Render each module's README.md and MIGRATION.md from its module.json.

module.json is the single source of truth; the generated documents cannot drift from it. Run
`python3 tools/gen_module_docs.py` to rewrite them, or `--check` to fail when they are stale (CI does this,
so a manifest or API change that forgets its docs breaks the build).
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MODULES = os.path.join(ROOT, "modules")


def load_all():
    mods = {}
    for name in sorted(os.listdir(MODULES)):
        path = os.path.join(MODULES, name, "module.json")
        if os.path.isfile(path):
            with open(path) as f:
                mods[name] = json.load(f)
    return mods


def closure(mods, mid, seen=None, visiting=None):
    """Dependencies first, then the module itself. Tolerates cycles and unknown ids so that the
    portability checker can report them instead of crashing."""
    seen = seen if seen is not None else []
    visiting = visiting if visiting is not None else set()
    if mid in seen or mid in visiting or mid not in mods:
        return seen
    visiting.add(mid)
    for d in mods[mid]["dependencies"]:
        closure(mods, d["id"], seen, visiting)
    visiting.discard(mid)
    seen.append(mid)
    return seen


def read(path):
    with open(path) as f:
        return f.read()


def render_readme(mods, mid):
    m = mods[mid]
    out = ["# `%s` — %s" % (mid, m["name"]), "",
           "Version %s · API %s%s · %s" % (m["version"], m["api_version"],
                                           (" · state schema %s" % m["state_schema_version"]) if m["state_schema_version"] else "",
                                           m["language_standard"]),
           "", "> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.", "",
           m["summary"], "", "## Public API", ""]
    out += ["- " + a for a in m["public_api"]]
    out += ["", "Public headers:", ""] + ["- `%s`" % h for h in m["public_headers"]]
    out += ["", "## Contract", "",
            "- **Threading:** " + m["threading"],
            "- **Deterministic:** %s · **Real-time safe:** %s" % ("yes" if m["deterministic"] else "no", "yes (see threading)" if m["real_time_safe"] else "no")]
    out += ["", "## Limits", ""] + ["- " + l for l in m["limits"]]
    out += ["", "## Dependencies", ""]
    if m["dependencies"]:
        out += ["- `%s` %s — %s" % (d["id"], d["version"], d["reason"]) for d in m["dependencies"]]
    else:
        out += ["None."]
    out += ["", "## Build and test", "", "```sh", m["standalone_test_command"], "```", "",
            "Tests live in `tests/`, a consumer example in `examples/minimal.cpp`. Migration steps: [MIGRATION.md](MIGRATION.md).", "",
            "## License", "", "`%s` — see the root `LICENSE` and `docs/LICENSING.md`. Keep the license notice when copying (the transfer script includes it)." % m["license"], ""]
    return "\n".join(out)


def render_migration(mods, mid):
    m = mods[mid]
    chain = closure(mods, mid)
    example = read(os.path.join(MODULES, mid, m["example"])).rstrip()
    out = ["# Migrating `%s` to another project" % mid, "",
           "> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.", "",
           "Module `%s` %s (API %s%s)." % (mid, m["version"], m["api_version"],
                                          (", state schema %s" % m["state_schema_version"]) if m["state_schema_version"] else ""), "",
           "## 1. What to copy", "", "Exact folders, in dependency order (minimum versions in parentheses):", ""]
    for c in chain:
        ver = "this module" if c == mid else next((d["version"] for d in mods[mid]["dependencies"] if d["id"] == c), "required by a dependency")
        out.append("- `modules/%s/` (%s)" % (c, ver))
    out += ["", "## 2. Copy", "",
            "From a ChopFractal checkout (copies only the folders above, never build output):", "",
            "```sh", "python3 tools/transfer_module.py %s /path/to/your/project" % mid, "```", "",
            "or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.", "",
            "## 3. Add to your build", "", "```cmake"]
    for c in chain:
        out.append('add_subdirectory(modules/%s)' % c)
    out += ["target_link_libraries(your_target PRIVATE chopfractal::%s)" % mid, "```", "",
            "Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.", "",
            "## 4. Map your types at one adapter boundary", "", m["adapter_notes"], "",
            "Optional adapters: " + "; ".join(m["adapters"]), "",
            "## 5. State import and export", ""]
    if m["state_schema_version"]:
        out += ["This module stores state under module ID `%s` at schema version %s. Keep the module ID and schema version when moving saved projects, "
                "and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` "
                "(or your own container keyed by module ID + schema version)." % (mid, m["state_schema_version"])]
    else:
        out += ["This module stores no project state."]
    out += ["", "## 6. Minimal compile and usage example", "", "```cpp", example, "```", "",
            "## 7. Tests", "", "Standalone (no plugin, no host):", "", "```sh", m["standalone_test_command"], "```", "",
            "Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):", "",
            "```sh", "tools/smoke_transfer.sh %s" % mid, "```", "",
            "## 8. Platform assumptions and removal", "", "- Platforms: " + m["platforms"],
            "- Removal: " + m["removal"],
            "- Record the imported version (`%s`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting." % m["version"], ""]
    return "\n".join(out)


def render_all(mods):
    files = {}
    for mid in mods:
        files[os.path.join(MODULES, mid, "README.md")] = render_readme(mods, mid)
        files[os.path.join(MODULES, mid, "MIGRATION.md")] = render_migration(mods, mid)
    return files


def main():
    mods = load_all()
    files = render_all(mods)
    stale = []
    for path, text in files.items():
        current = read(path) if os.path.exists(path) else None
        if current != text:
            stale.append(os.path.relpath(path, ROOT))
            if "--check" not in sys.argv:
                with open(path, "w") as f:
                    f.write(text)
    if "--check" in sys.argv:
        if stale:
            print("stale generated docs (run tools/gen_module_docs.py):\n  " + "\n  ".join(stale))
            return 1
        print("module docs are up to date")
        return 0
    print("rewrote %d file(s)" % len(stale))
    return 0


if __name__ == "__main__":
    sys.exit(main())
