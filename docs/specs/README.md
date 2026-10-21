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
| [GUI redesign](gui-redesign.md) | From-scratch GUI spec: research, diagnosis, layout grid, tokens, LookAndFeel, screens, implementation steps |
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
- [Fractal Rhythm](fractal-rhythm.md)
- [Evolve Mode](evolve-mode.md)
- [Smart Setup](smart-setup.md)
- [Orbit View](orbit-view.md)
- [Event effects: Filter, Tape Glide, Crunch](effects-filter-glide-crunch.md)
- [WAV export and Producer Kit](export-wav-and-producer-kit.md)
- [Modular integration and transfer plan](modular-integration-and-transfer-plan.md)

Each feature is specified as a portable component, with a separate plugin/UI adapter and explicit integration steps. The integration plan defines the order and contracts for assembling the modules.

## Advanced and pass-3 specs

- [Advanced section effects (families FX01 to FX14)](advanced-section-effects.md)
- [Pass 3 features (shipped: Chaos, drag-out, presets, direct hit editing basics)](pass3/pass3-features.md)
- [Build plan](build-plan.md) and [testing and acceptance](testing-and-acceptance.md)

## Roadmap specs (roadmap/)

Read [roadmap/README.md](roadmap/README.md) first (order, merge rules, file ownership, manifest policy, state tag registry), then [roadmap/_INDEX.md](roadmap/_INDEX.md). Each spec starts with a section 0 status block saying what is already built.

| Wave | Specs |
|---|---|
| 0 Foundation | [F00 editor decomposition](roadmap/F00-editor-decomposition.md), [F01 state v2](roadmap/F01-state-v2-and-persistence.md) |
| 1 Usable | [F02 direct editing](roadmap/F02-direct-editing.md), [F04 keyboard and layout](roadmap/F04-keyboard-and-layout.md), [F05 meters and feedback](roadmap/F05-meters-and-feedback.md), [F15 packaging](roadmap/F15-cross-platform-and-packaging.md) |
| 2 | [F03 presets](roadmap/F03-presets-and-favorites.md), [F06 rule editor](roadmap/F06-rule-editor.md), [F10 A/B morph](roadmap/F10-ab-morph.md), [F13 MIDI drag-out](roadmap/F13-midi-drag-out.md) |
| 3 Audio engine | [F07 performance pads](roadmap/F07-performance-pads.md), [F08 chaos and gestures](roadmap/F08-chaos-and-gestures.md), [F09 evolve scenes](roadmap/F09-evolve-scenes.md), [F11 multi-output stems](roadmap/F11-multi-output-stems.md), [F12 input capture](roadmap/F12-input-capture.md), [F14 tempo match](roadmap/F14-tempo-match-stretch.md) |
| 4 New | [F16 level match and limiter](roadmap/F16-level-match-and-safety-limiter.md), [F17 groove lab](roadmap/F17-groove-lab.md), [E01 echo](roadmap/E01-echo.md), [E02 tremolo-pan](roadmap/E02-tremolo-pan.md), [F18 arrangement export](roadmap/F18-arrangement-export.md), [F19 command palette and quick start](roadmap/F19-command-palette-and-quick-start.md) |

Template for new specs: [roadmap/_TEMPLATE.md](roadmap/_TEMPLATE.md).
