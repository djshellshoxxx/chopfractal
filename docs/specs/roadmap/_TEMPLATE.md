# <ID> <Feature name>

**Status:** Ready for development  **Size:** S | M | L (ideal engineer-days)  **Depends on:** <IDs or "none">  **Blocks:** <IDs>

## 1. Summary and user value
Two to four sentences: what it is, who it helps, why it matters (fun / usable / valuable / platform).

## 2. User stories and scope
- Stories as "As a producer I can ... so that ...".
- **In scope** (bullets). **Non-goals** (bullets, explicit).

## 3. UX
Exact controls (names, panel they live in, default values, enabled/disabled rules), interactions (mouse, keyboard, drag thresholds), empty/error/loading states, and every user-visible message string. Reference editor panels by the names defined in `docs/specs/roadmap/README.md` (WavePanel, PatternPanel, HistoryPanel, OrbitPanel, ControlsPanel, FeaturePanel, StatusBar).

## 4. Data model and state
New or changed types with field names, types, ranges, defaults. Serialization: which module payload, new schema version, backward-compat/migration rule (older files must still load; state only appears when the feature is used), size limits. Host parameters: IDs (append-only), ranges, manifest version.

## 5. Public API
C++ declarations in the repo's style (Result/Status, ticks, ids). State which module owns each (portable `modules/<id>/` vs `composition/` vs `plugin/`), threading (message thread / audio thread / worker) and real-time rules (no allocation, no locks on the audio thread).

## 6. Behavior details and edge cases
Algorithms, formulas, ordering, determinism rules (seeds, golden hashes that must not change), limits, interactions with locks / history / undo / Evolve / roles, failure and rollback behavior.

## 7. Test plan
- Unit tests (module level, names and key assertions).
- Integration tests (ProjectSession level).
- GUI tests (button-driving harness in plugin/tests; list exact clicks and assertions).
- Determinism / golden / sanitizer / TSan / real-time (allocation) checks as relevant.
- Manual QA script (numbered steps with expected result).

## 8. Acceptance criteria
Checklist of binary, measurable statements. Include "existing golden hashes unchanged" where relevant.

## 9. Files touched
Table: path | new/modified | what changes. Mark **hot files** (shared with other features) so the plan can sequence them.

## 10. Risks and mitigations
Top risks, how to detect them early, fallback.

## 11. Implementation steps
Ordered, each independently committable and passing CI.
