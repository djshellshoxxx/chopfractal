# Chop Roles and Grammar

**Status:** Creation spec for review  
**Module ID:** `chop_roles_grammar`  
**Purpose:** Give chops user-defined musical roles and use compact rules to guide generation without relying on opaque AI or genre labels.

## User behavior

The user assigns a role to each chop, such as Kick, Snare, Hat, Cymbal, Percussion, Texture, Accent, Transition, or Other. Custom role IDs are allowed. Role assignment is manual in the first version; an optional analyzer may suggest roles later, but suggestions never silently change tags.

Role-aware grammar lets a user protect or guide pattern structure:
- Preserve the first kick in each bar.
- Avoid repeating the same chop or role on adjacent events.
- Keep at least one accent in the final beat.
- Allow hats to subdivide while keeping kick/snare anchors.
- Prefer a transition-role chop at a phrase boundary.
- Exclude a role from selected bars or beats.

## Rule model

Rules are serializable data, not scripts. Each rule has an ID, enabled state, scope, condition, action, priority, and optional weight. Support:
- **Hard constraints:** preserve, require, forbid, maximum consecutive repeats, minimum/maximum count.
- **Soft preferences:** weighted candidate choice, role preference, variation likelihood.
- **Scope:** phrase, bar, beat, event, or selected region.
- **Fallback:** if hard constraints make mutation impossible, keep the previous valid content and report which rule blocked the change.

Hard constraints always win over soft preferences. Conflict resolution is deterministic. Limit active rules to a configurable safe maximum (recommended 64). Avoid a user scripting language in the first release.

## Module contract

The module owns role IDs, role metadata, rule validation, and candidate evaluation. It does not own source audio, generate the overall pattern, or render sounds. It receives immutable chop/event summaries through `chop_contracts` and implements a policy interface injected into the pattern engine.

Suggested operations:
- `assignRole(chopId, roleId)`
- `validateRules(ruleSet) -> ValidationReport`
- `evaluateCandidate(candidate, context, ruleSet, seed) -> Decision`
- `explainDecision(candidateId) -> DecisionTrace`

The decision trace identifies applied rules and whether they preserved, rejected, or weighted a candidate. It is available to the GUI as explanation text.

## Interface and integration

1. Source/chop UI edits role tags and sends role commands to the composition root.
2. Composition root stores role metadata and rule sets as module-owned versioned state.
3. Pattern engine supplies candidate choices and context to the grammar policy.
4. Grammar module returns allow/reject/weight decisions and a concise trace.
5. Pattern engine chooses deterministically from allowed choices and preserves locked events.
6. GUI shows role badges and a readable “why this stayed/changed” trace.

Role colors are optional and never the only role indicator. Keep the rules panel limited to guided controls and templates; advanced users can edit rule fields without writing code.

## Tests and acceptance

- Unknown or malformed role IDs and rules fail validation without corrupting saved state.
- Hard constraints are never violated by generated candidates.
- Soft weights are deterministic for the same seed.
- Contradictory constraints preserve the previous valid event and report the conflict.
- Locked events remain unchanged.
- Decision traces agree with the actual generation decision.
- Role changes affect only generation after the user asks to regenerate or mutate.

## Migration to another project

Copy `modules/chop_roles_grammar/` and its declared `chop_contracts` dependency. Adapt the receiving application's source IDs and candidate summaries through one adapter; keep rule evaluation portable. If that application uses different role names, map its vocabulary to stable role IDs and preserve unknown custom IDs. Retain rule schema versioning, validation, tests, module notices, and the provided `MIGRATION.md` example. The module must still compile when no UI or audio renderer is present.
