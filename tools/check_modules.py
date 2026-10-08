#!/usr/bin/env python3
"""Portability gate for the modular architecture (docs/specs/component-portability-protocol.md).

Verifies, for every module under modules/:
  * module.json has the required fields and matches its folder name
  * declared dependencies exist, the graph is acyclic, and CMake links exactly the declared set
  * every #include <chopfractal/X/...> in source, headers, tests and examples names the module itself or a
    declared dependency (no reaching into undeclared modules)
  * no JUCE / VST3 / GUI header leaks into a portable module, and no include escapes the module folder
  * declared public headers exist, README/MIGRATION exist and are not stale
Exit status 0 = all gates pass.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MODULES = os.path.join(ROOT, "modules")
sys.path.insert(0, os.path.dirname(__file__))
import gen_module_docs  # noqa: E402

REQUIRED = ["id", "name", "version", "api_version", "state_schema_version", "language_standard", "build_target", "public_headers",
            "dependencies", "license", "notices", "third_party", "threading", "platforms", "deterministic", "real_time_safe",
            "migration_guide", "standalone_test_command"]
FORBIDDEN = re.compile(r'#\s*include\s*[<"](juce_|JuceHeader|juce/|pluginterfaces|public\.sdk|vst3|windows\.h|Cocoa|AppKit|gtk|Qt)', re.I)
INCLUDE = re.compile(r'#\s*include\s*([<"])([^>"]+)[>"]')

errors = []


def err(msg):
    errors.append(msg)


def source_files(mod_dir):
    for sub in ("include", "src", "tests", "examples"):
        for base, _, files in os.walk(os.path.join(mod_dir, sub)):
            for f in files:
                if f.endswith((".hpp", ".h", ".cpp", ".cc")):
                    yield os.path.join(base, f)


def main():
    mods = gen_module_docs.load_all()
    folders = sorted(d for d in os.listdir(MODULES) if os.path.isdir(os.path.join(MODULES, d)))
    for d in folders:
        if d not in mods:
            err("%s: missing module.json" % d)
    for mid, m in mods.items():
        mdir = os.path.join(MODULES, mid)
        for key in REQUIRED:
            if key not in m:
                err("%s: module.json missing '%s'" % (mid, key))
        if m.get("id") != mid:
            err("%s: id '%s' does not match the folder name" % (mid, m.get("id")))
        if m.get("language_standard") != "C++17":
            err("%s: behavior modules use C++17" % mid)
        declared = {d["id"] for d in m.get("dependencies", [])}
        for d in declared:
            if d not in mods:
                err("%s: dependency '%s' is not a module" % (mid, d))
        for h in m.get("public_headers", []):
            if not os.path.isfile(os.path.join(mdir, h)):
                err("%s: public header '%s' not found" % (mid, h))
        for f in ("CMakeLists.txt", "README.md", "MIGRATION.md", m.get("example", "examples/minimal.cpp")):
            if not os.path.isfile(os.path.join(mdir, f)):
                err("%s: missing %s" % (mid, f))
        if not os.path.isdir(os.path.join(mdir, "tests")):
            err("%s: no tests/ directory" % mid)

        # CMake must link exactly the declared dependencies.
        cmake = open(os.path.join(mdir, "CMakeLists.txt")).read()
        linked = set(re.findall(r"chopfractal::([a-z_]+)", cmake)) - {mid}
        if linked != declared:
            err("%s: CMake dependencies %s differ from module.json %s" % (mid, sorted(linked), sorted(declared)))

        for path in source_files(mdir):
            rel = os.path.relpath(path, ROOT)
            with open(path, errors="replace") as fh:
                text = fh.read()
            if FORBIDDEN.search(text):
                err("%s: forbidden host/GUI include in portable module" % rel)
            for kind, target in INCLUDE.findall(text):
                m2 = re.match(r"chopfractal/([a-z_]+)/", target)
                if m2:
                    owner = m2.group(1)
                    if owner != mid and owner not in declared:
                        err("%s: includes <%s> but '%s' is not a declared dependency" % (rel, target, owner))
                    if owner in mods and not os.path.isfile(os.path.join(MODULES, owner, "include", target)):
                        err("%s: includes <%s> which does not exist" % (rel, target))
                elif kind == '"' and (target.startswith("..") or "/src/" in target):
                    err("%s: quoted include '%s' escapes the module or reaches into private sources" % (rel, target))

    # Acyclic dependency graph.
    state = {}

    def visit(n, path):
        if state.get(n) == 1:
            err("dependency cycle: " + " -> ".join(path + [n]))
            return
        if state.get(n) == 2 or n not in mods:
            return
        state[n] = 1
        for d in mods[n]["dependencies"]:
            visit(d["id"], path + [n])
        state[n] = 2

    for n in mods:
        visit(n, [])

    # Generated docs must be current.
    for path, text in gen_module_docs.render_all(mods).items():
        if not os.path.exists(path) or open(path).read() != text:
            err("%s: stale (run tools/gen_module_docs.py)" % os.path.relpath(path, ROOT))

    if errors:
        print("%d portability error(s):" % len(errors))
        for e in errors:
            print("  - " + e)
        return 1
    print("portability gates pass for %d modules: %s" % (len(mods), ", ".join(sorted(mods))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
