#!/usr/bin/env python3
"""Include-what-you-use audit: every standard header a file relies on must be reachable through includes the project
controls, not through whatever a particular standard library happens to pull in transitively (which is how code
that builds on GCC fails on Clang or MSVC)."""
import os, re, sys
root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SYMS = {  # header -> regex of symbols that require it
 "algorithm": r"std::(sort|stable_sort|find_if|find|min|max|lower_bound|upper_bound|reverse|fill|copy|any_of|all_of|none_of|remove_if|unique|clamp|equal|swap_ranges|max_element|min_element|count|count_if|transform)\b",
 "numeric": r"std::(accumulate|iota|gcd|lcm)\b",
 "cstring": r"std::(memcpy|memcmp|memset|memmove|strlen|strcmp)\b",
 "cmath": r"std::(sin|cos|exp|exp2|pow|floor|ceil|fabs|sqrt|llround|lround|round|isfinite|isnan|nan|nanf|log|log10|fmod|trunc)\b",
 "cstdlib": r"std::(llabs|labs|getenv|malloc|free|exit|abort)\b|std::abs\(",
 "limits": r"std::numeric_limits",
 "functional": r"std::(function|hash|bind)\b",
 "map": r"std::map\b", "set": r"std::set\b", "unordered_set": r"std::unordered_set\b", "unordered_map": r"std::unordered_map\b",
 "string": r"std::(string|to_string|stoi|stoll)\b", "vector": r"std::vector\b", "array": r"std::array\b",
 "atomic": r"std::atomic\b", "mutex": r"std::(mutex|lock_guard|unique_lock)\b", "thread": r"std::thread\b",
 "optional": r"std::(optional|nullopt)\b", "memory": r"std::(shared_ptr|unique_ptr|make_shared|make_unique|weak_ptr)\b",
 "utility": r"std::(move|pair|swap|forward|exchange|make_pair)\b", "cstdint": r"std::u?int(8|16|32|64)_t\b",
 "cstddef": r"std::(size_t|ptrdiff_t|nullptr_t)\b", "cassert": r"\bassert\(", "cstdio": r"std::(printf|fprintf|snprintf)\b",
 "new": r"std::(bad_alloc|nothrow)\b", "iterator": r"std::(next|prev|distance|begin|end|back_inserter)\b",
 "tuple": r"std::(tuple|tie|get<)", "initializer_list": r"std::initializer_list",
}
INC = re.compile(r'#\s*include\s*([<"])([^>"]+)[>"]')
def resolve(path, spec, kind, roots):
    if kind == '"':
        c = os.path.join(os.path.dirname(path), spec)
        if os.path.isfile(c): return c
    for r in roots:
        c = os.path.join(r, spec)
        if os.path.isfile(c): return c
    return None
roots = []
for m in os.listdir(os.path.join(root, "modules")):
    roots.append(os.path.join(root, "modules", m, "include"))
roots.append(os.path.join(root, "composition", "include"))
def closure(path, seen=None):
    seen = seen if seen is not None else {}
    if path in seen: return seen
    text = open(path, errors="replace").read()
    direct = set(i[1] for i in INC.findall(text) if i[0] == "<")
    seen[path] = direct
    for kind, spec in INC.findall(text):
        r = resolve(path, spec, kind, roots + [os.path.dirname(path)])
        if r: closure(r, seen)
    return seen
bad = 0
files = []
for base in ("modules", "composition", "plugin"):
    for d, _, fs in os.walk(os.path.join(root, base)):
        if "/build" in d: continue
        for f in fs:
            if f.endswith((".cpp", ".hpp", ".h")) and f != "chop_test.hpp": files.append(os.path.join(d, f))
for f in sorted(files):
    text = open(f, errors="replace").read()
    code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    code = re.sub(r'"(\\.|[^"\\])*"', '""', code)
    have = set()
    for h in closure(f).values(): have |= h
    for hdr, rx in SYMS.items():
        m = re.search(rx, code)
        if hdr == 'cstdlib' and m and m.group(0).startswith('std::abs') and 'cmath' in have:
            continue  # std::abs is declared in either header
        if m and hdr not in have:
            # std::min/max/abs/copy/find can come from several headers; only flag when clearly algorithm-style
            line = code[:m.start()].count("\n") + 1
            print("%s:%d uses %s but never includes <%s>" % (os.path.relpath(f, root), line, m.group(0), hdr)); bad += 1
print("include audit: %d missing direct includes" % bad)
sys.exit(1 if bad else 0)
