# Migrating `chop_roles_grammar` to another project

> Generated from `module.json` by `tools/gen_module_docs.py`. Edit the manifest, not this file.

Module `chop_roles_grammar` 0.1.0 (API 0.1, state schema 1).

## 1. What to copy

Exact folders, in dependency order (minimum versions in parentheses):

- `modules/chop_contracts/` (^0.1)
- `modules/chop_roles_grammar/` (this module)

## 2. Copy

From a ChopFractal checkout (copies only the folders above, never build output):

```sh
python3 tools/transfer_module.py chop_roles_grammar /path/to/your/project
```

or copy the folders by hand, keeping any `LICENSE`/`NOTICE` files. Do not use a git submodule for the initial release.

## 3. Add to your build

```cmake
add_subdirectory(modules/chop_contracts)
add_subdirectory(modules/chop_roles_grammar)
target_link_libraries(your_target PRIVATE chopfractal::chop_roles_grammar)
```

Each module's `CMakeLists.txt` also configures standalone and pulls in its declared dependencies by relative path.

## 4. Map your types at one adapter boundary

Map your own role vocabulary to stable role IDs and keep unknown custom IDs.

Optional adapters: Role editing UI and rule panel; the composition root builds the policy and injects it into generation.

## 5. State import and export

This module stores state under module ID `chop_roles_grammar` at schema version 1. Keep the module ID and schema version when moving saved projects, and keep its migration tests. Newer-than-supported state is rejected safely rather than guessed at. Compose payloads with `state_codec` (or your own container keyed by module ID + schema version).

## 6. Minimal compile and usage example

```cpp
// Minimal consumer: assign roles, add two guided rules, and evaluate a candidate.
#include <chopfractal/chop_roles_grammar/grammar.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  roles::RoleMap map;
  map.assignRole(ChopId{1}, "kick");
  map.assignRole(ChopId{2}, "hat");

  roles::RuleSet rules;
  rules.rules.push_back(roles::templates::preserveFirst(1, "kick"));
  rules.rules.push_back(roles::templates::avoidAdjacentRepeats(2));
  if (!roles::validateRules(rules).ok()) return 1;

  roles::GrammarPolicy policy(map, rules, {ChopId{1}, ChopId{2}});
  CandidateQuery q;
  q.chop = ChopId{2};
  q.recent = {ChopId{2}};  // the previous event already used this chop
  const Decision d = policy.evaluate(q);
  if (d.allowed) return 1;
  std::printf("%s\n", policy.explain(d).c_str());
  return 0;
}
```

## 7. Tests

Standalone (no plugin, no host):

```sh
cmake -S modules/chop_roles_grammar -B build/chop_roles_grammar && cmake --build build/chop_roles_grammar && ctest --test-dir build/chop_roles_grammar
```

Clean-consumer smoke test (copies this module and its dependencies into an empty project and builds it):

```sh
tools/smoke_transfer.sh chop_roles_grammar
```

## 8. Platform assumptions and removal

- Platforms: Any C++17 compiler.
- Removal: Delete the folder; generation runs without a policy.
- Record the imported version (`0.1.0`) and any local patches in your repository; reconcile upstream fixes by cherry-pick, not by overwriting.
