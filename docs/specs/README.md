# ChopFractal specifications

**Status:** Draft for review  
**Product:** ChopFractal, a deterministic loop-chopping and pattern-generation VST3 audio effect  
**Design basis:** Effect-first hybrid approved 2026-10-07

## Document map

| Spec | Defines |
|---|---|
| [Product and system architecture](product-and-architecture.md) | Audience, goals, scope, terminology, system boundaries |
| [Source and chop editor](source-and-chop-editor.md) | Import, capture, analysis, marker editing, audition |
| [Pattern generation engine](pattern-generation-engine.md) | Hierarchical rhythmic generation, constraints, determinism |
| [Audio engine, effects, and routing](audio-engine-and-routing.md) | Playback voices, real-time rules, event transforms, signal flow |
| [GUI and interaction](gui-and-interaction.md) | Layout, visualization, accessibility, interaction behavior |
| [State, presets, automation, and export](state-presets-automation-export.md) | Project recall, parameter contract, presets, rendering/export |
| [Testing and acceptance](testing-and-acceptance.md) | Functional, audio, host, performance, and release gates |
| [Build plan](build-plan.md) | Ordered delivery stages, dependencies, and exit criteria |

## Implementation status

What has been built against these specs, what was verified, and what remains: [`../BUILD_STATUS.md`](../BUILD_STATUS.md).

## Review rules

These documents define intended behavior, not permission to implement it. Review the specs as a set and resolve open decisions before detailed engineering tasks or production code are started. The build plan is a product delivery roadmap; a code-level implementation plan should follow spec review.

## Product premise

ChopFractal transforms a user-provided or captured loop into new drum patterns by arranging detected or manually edited chops. A visible phrase-to-bar-to-beat-to-event hierarchy provides structured variation. A fixed seed reproduces the same pattern, and locks preserve chosen sections while other sections mutate.

The product does not claim to be the first loop slicer, sequencer, or randomized beat tool. Its intended distinction is the combination of transparent hierarchical pattern variation, repeatable seeds, granular locks, and fast source-to-pattern workflow. Market context is documented in the product spec.
## Modular component protocol

The [component portability protocol](component-portability-protocol.md) is normative for every independently reusable project element. It defines module boundaries, public contracts, dependency rules, test requirements, transfer packages, and the required migration guide for every module.

## Feature creation specs

- [Recursive Hit Zoom](recursive-hit-zoom.md)
- [Chop Roles and Grammar](chop-roles-and-grammar.md)
- [Variation Family Tree](variation-family-tree.md)
- [Modular integration and transfer plan](modular-integration-and-transfer-plan.md)

Each feature is specified as a portable component, with a separate plugin/UI adapter and explicit integration steps. The integration plan defines the order and contracts for assembling the modules.
