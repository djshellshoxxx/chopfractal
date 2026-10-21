# Architecture and Design Rationale

The full module contracts are in [MODULARITY_MAP](../MODULARITY_MAP.md) and [specs](../specs/README.md).

- **Modules.** Fifteen portable C++17 modules under `modules/`, each with tests, a `module.json` manifest and a generated migration guide.
- **Composition root.** `composition/` contains `ProjectSession`, which wires the modules together headlessly.
- **Plugin shell.** `plugin/` is thin JUCE glue over the composition root and is optional.
- **Determinism.** A fixed seed always reproduces the same pattern. Evolve steps are reproducible from their seeds.
- **Portability.** Core modules do not depend on JUCE, so they build and test without a plugin SDK.
