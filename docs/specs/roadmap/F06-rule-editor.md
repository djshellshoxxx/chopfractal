# F06 Rule editor

**Status:** Ready for development  **Size:** M (5 engineer-days)  **Depends on:** F00 (panel classes, EditorModel, command layer, recursive `findButton` test helper)  **Blocks:** none
(F03 factory presets reuse `roles::describe` and the template builders)

## 1. Summary and user value
`chop_roles_grammar` already evaluates roles and rules (Forbid, Require, Preserve, MaxCount, MaxConsecutiveRepeats, Prefer) and `ProjectSession::setRules` already validates and stores
them, but the only GUI today is the "Set Role" button; rules cannot be created, and a blocked event is only visible as a generic notice ("Rule 4 blocked a change in bar 2, beat 3"). F06
adds a RulesPanel where a producer manages roles per chop, creates and edits rules (with one-click templates), sees every validation problem before applying, tests the rule set without
changing the pattern, and asks "why is this hit here / why was this change blocked". This is what turns the grammar from a hidden engine into a usable "keep my kick on 1, never two hats
in a row" tool.

## 2. User stories and scope
- As a producer I can give every chop a role in one table so that rules can talk about "kick" or "hat" instead of chop numbers.
- As a producer I can add a rule from a template, or from scratch, edit every field in plain language, switch it off, duplicate and delete it, so that I control what
  Generate/Mutate/Evolve may do.
- As a producer I see errors and conflicts as I type, and Apply is disabled while an error exists, so I never lose a rule set to a rejected save.
- As a producer I can press "Test Rules" to see how many changes the rules would block with the current settings, without touching my pattern.
- As a producer I can select a hit and press "Why?" to read why it is allowed, weighted, protected or (for hand-placed hits) in conflict with a rule, and see the list of blocks from the
  last Generate/Mutate/Evolve step.
- **In scope:** RulesPanel (tabs Rules, Roles, Problems and Why), `ui::RuleEditorModel` (headless), `roles::describe`, `GrammarPolicy::preservingRule`, `pattern::candidateQueryFor`,
  session methods `clearRole`, `lastRuleNotes`, `explainEvent`, `previewRules`, plain-language rule text, tests.
- **Non-goals:** a scripting or free-text rule language (rules stay data), new rule kinds or conditions, per-bar visual rule painting on PatternPanel, undo/redo of rule edits (Revert
  returns to the last applied set only), rule import/export (F03 owns presets), changing the engine's evaluation order, enforcing rules on hand-placed edits.

## 3. UX
**Entry:** FeaturePanel gets a toggle button **"Rules"** (title "Open the rules and roles editor"). It shows/hides RulesPanel, an overlay child of the editor that covers the
PatternPanel/HistoryPanel/OrbitPanel region (no separate window, no modal loop; plugin hosts dislike both). Esc or **"Close"** hides it; unapplied edits are kept while hidden
(`"Unapplied rule changes"` is shown in StatusBar).

**RulesPanel layout** (component name `"RulesPanel"`, header text `"Rules and roles"`): a `juce::TabbedButtonBar` with tabs **"Rules"** and **"Roles"**, a bottom problems list, and a
footer. Footer buttons in Tab order: **"Apply"**, **"Revert"**, **"Apply and Mutate"**, **"Test Rules"**, **"Why?"**, **"Close"**.

**Rules tab.** Left: `juce::ListBox` (name `"Rule list"`), rows sorted exactly like the engine evaluates them (priority descending, then id), row text `"[x] #3  Forbid hat in bars 3-4
P10"` (`[x]`/`[ ]` is the enabled checkbox, `P10` the priority; a trailing `"  Error"` or `"  Warning"` word appears if the rule has an issue, never colour alone). Row text is the rule
label when non-empty, else `roles::describe(rule)`; the tooltip is always `describe(rule)`. Above the list: buttons **"Add Rule"**, **"Duplicate"**, **"Delete"** (Duplicate/Delete
disabled without a selection; Add disabled at 64 rules with tooltip `"The limit is 64 rules."`). Template strip below those, label `"Templates:"`, ComboBox **"Template role"** (items:
the 9 built-in roles plus custom roles in use; default first role "kick"), two TextEditors **"Template bar from"** / **"Template bar to"** (digits only, default `1` and `2`, used only
by the last template), and six buttons: **"Keep first"** (`templates::preserveFirst`), **"No repeats"** (`avoidAdjacentRepeats(SameChop)`), **"No role repeats"**
(`avoidAdjacentRepeats(SameRole)`), **"Need in final beat"** (`requireInFinalBeat`), **"Prefer at boundaries"** (`preferAtPhraseBoundary`, weight 3.0), **"Exclude from bars"**
(`excludeRole(role, from-1, to-1)`). Each template button adds the rule, selects it and focuses its Label field.

Right: the edit form for the selected rule (all controls have `setTitle`):

| Control (title) | Rule field | Values / rules |
|---|---|---|
| ToggleButton "Rule enabled" | `enabled` | default on |
| ComboBox "Action" | `action.kind` | Preserve = "Keep", Require = "Require", Forbid = "Forbid", MaxConsecutiveRepeats = "Limit repeats", MaxCount = "Limit count per bar", Prefer = "Prefer / avoid" |
| ComboBox "Role" | `when.role` | "(any role)" = empty, built-ins, custom roles; typing a custom id is allowed and validated with `isValidRoleId` |
| ComboBox "Chop" | `when.chop` | "(any chop)" or "Chop N" (N = 1-based position in the chop snapshot) |
| ComboBox "Position" | `when.position` | "Anywhere", "First in bar", "Final beat", "Phrase boundary" |
| TextEditors "Bar from" / "Bar to" | `when.barMin` / `when.barMax` | shown 1-based; `to` blank = unbounded (`barMax = -1`); stored = shown - 1; digits only, 1..8 |
| TextEditors "Beat from" / "Beat to" | `when.beatMin` / `when.beatMax` | same, 1..32 |
| TextEditor "Count" | `action.count` | only for Limit repeats (1..16) and Limit count per bar (1..512) |
| ComboBox "Repeat by" | `action.repeatBy` | "Same chop" / "Same role"; only for Limit repeats |
| Slider "Weight" | `weight` | only for Prefer; 0.1 to 10, logarithmic, default 2.0, text `"x2.0"`; values below 1 mean avoid; the engine allows up to 100 and the model accepts typed values up to 100 |
| Slider "Priority" | `priority` | -1000..1000 integer, default 0; tooltip `"Rules are checked highest priority first. A Require of higher priority overrides a Forbid of lower priority."` |
| TextEditor "Label" | `label` | max 128 characters, empty allowed |

`scope` has no control: it is derived from the action so the module's own checks pass (Preserve = Bar, Require = Beat, Forbid = Region, MaxConsecutiveRepeats = Phrase, MaxCount = Bar,
Prefer = Event; identical to the template builders). Fields that do not apply to the chosen action are hidden, not disabled.

**Roles tab.** `ListBox` `"Chop roles"`, one row per chop of the current `ChopSnapshot` (enabled chops only), columns: `"Chop N"`, time range `"0:01.250 - 0:01.500"`, an editable
ComboBox (title `"Role for chop N"`; items "(none)", built-ins, custom roles in use; free text validated), and a small button **"Audition"** (calls `ChopFractalProcessor::audition`).
Changes apply immediately (`assignRole` / `clearRole`) and take effect at the next Generate or Mutate, exactly like today's "Set Role". Empty state when no chops: `"Detect chops first;
roles are assigned to chops."`.

**Problems and Why area** (bottom of the panel, two sub-lists, each with a count label): `ListBox` `"Problems"` rows `"Error: rule 4: count limits are evaluated per bar; use Bar scope"`
/ `"Warning: rule 2: rule 2 forbids what rule 5 requires"`; clicking a row selects that rule. Count label `"No problems"` or `"2 problems (1 error)"`. Second list `"Last blocked
changes"` rows from `lastRuleNotes()`, e.g. `Bar 2, beat 3: rule 4 ("no hats in bars 3-4") blocked a change; the previous content was kept.`; clicking a row asks the PatternPanel (via
the F00 command layer `selectBeat(bar, beat)`) to highlight that beat. Empty text `"Nothing was blocked by the last Generate, Mutate or Evolve step."`.

**Footer behavior.** Apply: enabled only when the draft differs from the applied rules **and** has no Error; calls `setRules`; StatusBar `"Rules applied. They take effect at the next
Generate or Mutate."`. Disabled tooltip: the first error text. Revert: restores the draft to the applied rules, StatusBar `"Reverted to the applied rules."`. Apply and Mutate: Apply,
then runs the same command as the Mutate button but with `MutateOptions.amount = 1.0` and label `"Mutate (rules)"`, so every unlocked, non-hand-placed beat is re-evaluated; the existing
notice `"N locked or edited section(s) were kept."` still appears. Test Rules: `previewRules(draft, currentPatternSettings)`; StatusBar `"Test: 3 changes would be blocked by rule 4, 1
by rule 2. Nothing was changed."` or `"Test: no rule blocks anything with the current settings."`; with no pattern yet the test runs on default `pattern::Settings` (density from the
ControlsPanel slider) and the message starts `"Test (default settings): "` instead of `"Test: "`. Why?: enabled when PatternPanel has a selected hit; opens a `juce::CallOutBox` with
`EventExplanation::text` and sends the same text to StatusBar; with no selection the status is `"Select a hit first; Why? explains that hit."` (matches the style of the existing "Select
a hit" refusals).

Other strings: Delete of a rule that other rules do not depend on has no confirmation. `"Clear All"` is not offered. Add at limit: `"The limit is 64 rules."`. Unapplied edits when the
panel is closed: StatusBar suffix `"Unapplied rule changes"`.

## 4. Data model and state
No change to any serialized format. Rules and roles stay in the `chop_roles_grammar` module payload (schema `roles::kSchemaVersion = 1`, written by `roles::serialize`, loaded by
`roles::deserialize` + `validateRules`), so older and newer builds read the same bytes, and projects that never open the editor are byte-identical. The draft (`RuleEditorModel`) and the
"last blocked changes" list are transient UI/session state, not saved. Limits come from the module: 64 rules, label 128 chars, priority +-1000, weight (0, 100], 256 role assignments,
role ids `[a-z0-9_-]{1,32}`.
```cpp
// plugin_ui_adapter/rule_editor.hpp  (portable, headless, UI thread)
namespace chopfractal::ui {
enum class TemplateKind : std::uint8_t { KeepFirst, NoRepeats, NoRoleRepeats, NeedInFinalBeat, PreferAtBoundaries, ExcludeFromBars };
struct RuleIssueView { std::uint32_t rule = 0; bool error = false; std::string text; };
struct RuleRowView { std::uint32_t id; bool enabled; std::string text; std::string tooltip; int priority; bool hasError, hasWarning; };
class RuleEditorModel {
 public:
  void reset(const roles::RuleSet& applied, const roles::RoleMap& roles, int patternBars, const std::vector<ChopId>& chops);
  const roles::RuleSet& draft() const;
  bool dirty() const;                       // draft != applied (field-wise)
  bool canApply() const;                    // dirty() && no error issue
  bool canAdd() const;                      // size < roles::kMaxRules
  std::uint32_t addRule();                  // Prefer, weight 2.0, empty condition; returns its id
  std::uint32_t addFromTemplate(TemplateKind, const roles::RoleId& role, int barFrom1, int barTo1);  // 1-based bars; returns id or 0
  std::uint32_t duplicate(std::uint32_t id);  // copy gets next id, label + " (copy)" cut to 128
  bool remove(std::uint32_t id);
  bool update(const roles::Rule& edited);   // normalizes scope from the action; false if id unknown
  std::vector<RuleRowView> rows() const;    // engine order: priority desc, id asc
  std::vector<RuleIssueView> issues() const;  // validateRules(draft, &roles) + UI-level warnings
  std::uint32_t nextId() const;             // max(id) + 1, at least 1
};
}
```
UI-level warnings added by `issues()` beyond `validateRules`: `"Rule N: bars A-B are outside this P-bar pattern."` (when `barMin >= patternBars`; patternBars from the current pattern, 0
= skip) and `"Rule N: chop #V no longer exists."` (`when.chop` valid but absent from `chops`). Host parameters: none (manifest unchanged, `kManifestVersion` stays 2).

## 5. Public API
Portable additions (no JUCE):
```cpp
// modules/chop_roles_grammar/include/.../grammar.hpp
std::string describe(const Rule& r);   // plain English, see section 6
// GrammarPolicy:
std::uint32_t preservingRule(const CandidateQuery& q) const;   // id of the first enabled Preserve rule (in file order) that matches, else 0
// modules/pattern_engine/include/.../pattern.hpp (impl moved from Gen::query in generate.cpp, unchanged logic, shared via internal.hpp)
CandidateQuery candidateQueryFor(const Pattern& p, EventId id);   // empty chop (invalid) if the id is not a top-level event
```
Composition (`composition/src/rules.cpp`, declarations appended to `ProjectSession`; threading: message thread under `withSession`):
```cpp
struct RuleNote { int bar = 0; int beat = 0; std::uint32_t rule = 0; std::string text; };
struct EventExplanation { bool allowed = true; bool preserved = false; bool handPlaced = false; double weight = 1.0; std::vector<std::uint32_t> rules; std::string text; };
struct RulePreview { int events = 0; int blocked = 0; std::vector<std::pair<std::uint32_t, int>> blockedByRule; };  // sorted by rule id
Status clearRole(ChopId chop);                                   // NotFound for an unknown chop; no-op Ok when unassigned
const std::vector<RuleNote>& lastRuleNotes() const;              // BlockedByRule notes of the last Generate/Mutate/Evolve step; cleared by loadState/reset/clearSource
Result<EventExplanation> explainEvent(EventId id) const;         // InvalidArgument without pattern/chops, NotFound for unknown id
Result<RulePreview> previewRules(const roles::RuleSet& draft, const pattern::Settings& settings) const;  // pure: pattern::generate into a scratch pattern; never commits
```
`noteReport()` (project_session.cpp) gains one call `recordRuleNotes(report)` (defined in rules.cpp); `reset()`/`loadState`/`clearSource` clear the list. Existing `setRules` is
unchanged and remains the only writer of `rules_`. The editor calls these only through the F00 command layer (`commands.applyRules(draft)`, `commands.explainSelection()`, ...); no rule
logic lives in the panel.

## 6. Behavior details and edge cases
**`describe(rule)`** (lowercase role names as stored; bars/beats printed 1-based; `weight` printed with up to 2 significant digits): `{Verb} {target}{position}{range}` with Verb = Keep
/ Require / Forbid / Prefer (weight > 1) / Avoid (weight < 1; weight == 1 prints "Prefer"); target = role, else `chop #N` (the numeric `ChopId` value), else `any chop`, both set:
`{role} chop #N`; position = ` first in each bar` / ` in the final beat` / ` at phrase boundaries`; range = ` in bar 3` / ` in bars 3-4` / ` from bar 3` / ` on beat 2` / ` on beats 1-2`
/ ` from beat 2` (barMax < 0 with barMin > 0 is "from"); Prefer/Avoid append ` (x3)`. Limits: `Allow at most {count} of the same {chop|role} in a row{range}` (target ignored when any;
else `Allow at most {count} {target} of the same ... in a row`), `Allow at most {count} {target} per bar{range}`. Examples pinned by tests: `templates::excludeRole(7,"hat",2,3)` ->
`Forbid hat in bars 3-4`; `requireInFinalBeat(1,"kick")` -> `Require kick in the final beat`; `preferAtPhraseBoundary(2,"snare")` -> `Prefer snare at phrase boundaries (x3)`;
`avoidAdjacentRepeats(3)` -> `Allow at most 1 of the same chop in a row`; `preserveFirst(4,"kick")` -> `Keep kick first in each bar`.

**Template ranges.** `ExcludeFromBars` with from > to or outside 1..8 is refused by the model (`addFromTemplate` returns 0) and the panel shows `"Bar from must be 1 to 8 and not after
Bar to."`. Other templates ignore the bar fields.

**Draft and Apply.** `RuleEditorModel` works on a copy; `update` re-derives `scope` from `action.kind` and clamps nothing silently (out-of-range values stay and surface as module
errors, so the user sees the real message). Apply calls `ProjectSession::setRules(draft)`; module Warnings are re-emitted by `setRules` as session Notices (existing behavior) and also
shown in "Problems". After a successful Apply the model's baseline becomes the draft. A loaded project or Generate/Mutate never overwrites an unapplied draft; if the session's rules
changed underneath (loadState) the panel resets the draft and says `"The project's rules were reloaded; unapplied changes were discarded."`.

**When rules act.** Unchanged engine semantics: Generate/Mutate/Evolve steps evaluate the new rules at their next run (`makePolicy()` is built per call, so Apply during Evolve takes
effect at the next step). Hand-placed (userOwned) and locked content is never removed by a rule; `explainEvent` says so. `makePolicy()` still returns null when there are no roles and no
rules, so projects without rules take the exact old code path and the four pattern_engine golden hashes (2397844821793184813, 12084884237330071892, 13710596758976548913,
18237151833431327128) are unchanged. The `candidateQueryFor` refactor must not change `Gen::query` output: the golden test is the guard.

**`explainEvent(id)`.** Builds `q = candidateQueryFor(pattern, id)`, `policy = makePolicy()` (null policy: `text = "No rules or roles are set, so nothing constrains this hit."`), `d =
policy->evaluate(q)`, `rule = policy->preservingRule(q)`, `handPlaced = event.userOwned`. Text = `capitalize(policy->explain(d))` (existing format, e.g. `Kept; rule 2 (prefer "x")
weighted it`), then, in order: for each Weighted trace ` Rule N multiplies its weight by xW.` (W from the rule), if `rule != 0` ` Mutate keeps it because rule N ("label") protects it.`,
if `handPlaced && !d.allowed` ` You placed this hit by hand, so Mutate keeps it even though the rules reject it.`, and if the trace is empty and not preserved ` No rule applies to this
hit.`. `weight = d.weight`, `rules` = ids from trace plus the preserving rule, deduplicated, ascending.

**`previewRules`.** Uses the draft with the session's current `roles_`, calls `pattern::generate(*chops_, settings, &policy, &report)` (pure, no state change, no notices, no history),
counts `Note::Kind::BlockedByRule` by rule (rule 0 counted under id 0 and printed as "a rule"). Settings passed by the editor: the current pattern's `settings` (seed included, so the
result is reproducible and equals what Mutate-with-this-seed would hit), or default settings + density slider when no pattern exists. Cost: one generation (< 5 ms for 8 bars); runs on
the message thread.

**`lastRuleNotes` text** is built in `recordRuleNotes` from `Note{kind,rule,bar,beat}` with the same wording as `noteReport` plus the rule label: `Bar {bar+1}, beat {beat+1}: rule {id}
("{label}") blocked a change; the previous content was kept.` (rule 0 or unknown: `a rule`). Capped at 64 entries (newest kept) so a pathological set cannot grow the list.

**Roles.** `assignRole` errors pass through verbatim (`"Roles use lowercase letters, digits, '_' and '-' (1-32 chars)."`, `"too many role assignments"`). Disabling a chop keeps its role
(existing test `roles_survive_disabling_and_re_enabling_a_chop`). Rules whose role no chop has produce the module warning `"no chop has the role 'x'"` live as roles change.

**Failure/rollback.** `setRules` is all-or-nothing; on error the draft is untouched and the first error is shown. Apply and Mutate: if Mutate fails (for example the event cap), rules
stay applied and the failure text is shown (they are two independent commands).

## 7. Test plan
**Unit, `modules/chop_roles_grammar/tests/test_grammar.cpp`:** `describe_renders_every_action_kind_in_plain_english` (the five pinned strings above plus MaxCount `Allow at most 3 hat
per bar`, Preserve with chop id, beat range, Avoid wording at weight 0.5 -> `Avoid ... (x0.5)`); `preserving_rule_returns_first_matching_enabled_preserve_rule_or_zero` (disabled rule
skipped; none -> 0); `describe_never_exceeds_200_chars_and_is_deterministic` (fuzz 500 random valid rules). **Unit, `modules/pattern_engine/tests/test_pattern_engine.cpp`:**
`candidate_query_for_matches_the_query_used_during_generation` (spy `ICandidatePolicy` records every `CandidateQuery`; for each placed event find the spy query with equal chop,
`startInBar`, bar; assert `recent`, `barSoFar`, `firstInBar`, `scope` equal `candidateQueryFor`); `candidate_query_for_unknown_id_returns_invalid_chop`; the existing
`golden_output_is_pinned_across_compilers_and_platforms` must pass untouched. **Unit, `modules/plugin_ui_adapter/tests/test_rule_editor.cpp`:**
`add_rule_uses_next_free_id_and_never_reuses_after_delete_within_session` (ids 1,2,3; delete 2; add -> 4); `templates_build_the_same_rules_as_the_module_builders` (compare field-wise to
`templates::*`); `exclude_template_converts_one_based_bars_and_rejects_bad_ranges`; `update_derives_scope_from_action`; `rows_are_in_engine_order`;
`issues_include_module_errors_and_ui_warnings` (count limit 0 -> error text equals the module message; barMin 5 on a 4-bar pattern -> the outside-pattern warning);
`apply_is_blocked_by_errors_and_enabled_by_fixes`; `limit_of_64_disables_add`; `duplicate_copies_fields_and_truncates_label_to_128`. **Integration, `composition/tests/test_rules.cpp`:**
`explain_event_reports_weighting_protection_and_hand_placed_conflicts` (Prefer x2 on kick, Preserve first kick, hand-placed hat under Forbid hat: assert the three texts and `rules`);
`explain_event_without_policy_says_nothing_constrains_it`; `last_rule_notes_record_blocks_and_clear_on_reset_and_load` (Forbid every chop but one at one bar -> notes with rule id, bar,
beat; `reset()` empties); `preview_rules_counts_blocks_without_changing_state` (pattern bytes, history size, undo depth, notices unchanged before/after);
`preview_rules_matches_a_real_generate_report` (same settings, same blocked counts); `clear_role_removes_assignment_and_persists_through_save_load`;
`rules_state_bytes_are_unchanged_by_this_feature` (`decode(saveState())` roles payload == `roles::serialize(roleMap(), rules())`, schema 1); `rule_notes_are_capped_at_64`. **GUI,
`plugin/tests/test_rules_gui.cpp`** (registered in `plugin/CMakeLists.txt`; skipped without DISPLAY; uses the F00 recursive `findButton`/`findCombo`/`findComponent(name)` helpers):
1. `rules_panel_template_apply_and_generate`: load `drumLoop()`, click "Detect Chops"; via session assign roles (chop 0 "kick", chop 1 "hat", rest "snare"); click "Rules"; assert
   `findComponent("RulesPanel")->isVisible()`; set combo "Template role" to "hat", editors "Template bar from"=`1`, "Template bar to"=`2`; click "Exclude from bars"; assert
   `RuleEditorModel` row count 1 and the Problems count label text `"No problems"`; click "Apply"; assert `s.rules().rules.size()==1`, `kind==Forbid`, `when.role=="hat"`, `barMin==0`,
   `barMax==1`, status contains `"Rules applied."`; click "Generate" (F00 ControlsPanel); assert no event in bars 0-1 has the hat chop; click "Rules" again, assert "Last blocked
   changes" list is non-empty or shows the empty text.
2. `invalid_rule_blocks_apply_and_revert_restores`: click "Add Rule"; set "Action" to "Limit count per bar" and "Count" to `0`; assert Problems label starts `"1 problem (1 error)"` and
   its row text contains `"count limit must be at least 1"`; assert "Apply" `isEnabled()==false`; click "Revert"; assert row count 0 and session rules empty.
3. `why_button_explains_the_selected_hit`: Generate with a Prefer rule applied; select the first event via the F00 `EditorModel::select(id)`; click "Why?"; assert status text starts
   with `"Kept"` or `"Rejected"` and contains `"rule"`; with no selection assert `"Select a hit first; Why? explains that hit."`.
4. `roles_tab_assigns_and_clears`: switch tab "Roles", set the combo for chop 1 to "snare", assert `roleMap().roleOf(chop)=="snare"`, set "(none)", assert empty.
5. Accessibility: every RulesPanel button/ComboBox/Slider/TextEditor has a non-empty title (same loop as the existing test's last block). **Other checks:** the four golden hashes
   unchanged; `process()` allocation test unchanged (no audio-thread change); ASan/UBSan over the new tests including a truncation/byte-flip sweep of a rules payload through `loadState`
   (reuse the hostile-file helper in `test_composition.cpp`). **Manual QA:** (1) Load a drum loop, Detect Chops, Smart Setup, Accept Roles. (2) Open Rules; add "Keep first" for kick and
   "No repeats"; Apply. Expected: status line as above. (3) Press Generate five times with different seeds; expected: a kick opens every bar, no chop repeats back-to-back. (4) Add
   Forbid hat bars 1-2 via the template, delete bar-to text to leave it blank, expected row becomes `Forbid hat from bar 1`. (5) Type `0` in Count of a count rule; expected the error
   appears at once and Apply greys out. (6) Test Rules; expected the blocked counts line and an unchanged pattern (press Undo: nothing to undo). (7) Select a hit, Why?; read text. (8)
   Save the project, reload, reopen Rules; expected the same rows.

## 8. Acceptance criteria
- [ ] A producer can create all six action kinds from the form and all six templates from the strip; each produces a rule that `validateRules` accepts and that survives save/reload
      byte-identically.
- [ ] Apply is disabled whenever `issues()` contains an Error, and the first error text is the Apply tooltip.
- [ ] Row text and tooltips come from `roles::describe`; the five pinned example strings match exactly.
- [ ] Test Rules changes no session state (pattern bytes, history, undo depth, notices) and its blocked counts equal a real Generate report with the same settings.
- [ ] `explainEvent` returns the three pinned text shapes (weighted, protected, hand-placed conflict) and `No rule applies to this hit.` when appropriate.
- [ ] "Last blocked changes" lists every `BlockedByRule` note of the last Generate/Mutate/Evolve step (max 64) and clicking a row highlights that bar/beat.
- [ ] Roles tab assigns, changes and clears roles for all chops (up to the 256 limit) and shows the module's exact error for a bad role id.
- [ ] No serialized format changed; a project saved without opening the editor is byte-identical to the previous build's output; golden hashes `2397844821793184813`,
      `12084884237330071892`, `13710596758976548913`, `18237151833431327128` unchanged.
- [ ] Every new control has a non-empty accessible title; Tab order follows section 3; no information is conveyed by colour only.
- [ ] All tests in section 7 pass under GCC, Clang, ASan+UBSan, and xvfb; `check_modules.py`, `check_includes.py` pass with the new `plugin_ui_adapter -> chop_roles_grammar` dependency
      declared.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/chop_roles_grammar/include/.../grammar.hpp`, `src/grammar.cpp` | Mod | `describe`, `GrammarPolicy::preservingRule`; tests added to `tests/test_grammar.cpp` |
| `modules/pattern_engine/include/.../pattern.hpp`, `src/generate.cpp`, `src/internal.hpp` | Mod | `candidateQueryFor`; `Gen::query` body moved to `detail::buildQuery` unchanged |
| `modules/plugin_ui_adapter/include/.../rule_editor.hpp`, `src/rule_editor.cpp`, `tests/test_rule_editor.cpp` | New | headless model |
| `modules/plugin_ui_adapter/module.json`, `CMakeLists.txt` | Mod | declare dependency on `chop_roles_grammar`; regenerate module docs |
| `composition/src/rules.cpp` | New | session methods |
| `composition/include/chopfractal/composition/project_session.hpp` | Mod **(hot)** | declarations from section 5, `std::vector<RuleNote> ruleNotes_` member |
| `composition/src/project_session.cpp` | Mod **(hot)** | one call in `noteReport`, clear in `reset`/`loadState`/`clearSource` |
| `composition/CMakeLists.txt` | Mod | add `rules.cpp` and `tests/test_rules.cpp` |
| `plugin/src/panels/RulesPanel.h/.cpp` | New | panel (F00 location) |
| `plugin/src/panels/FeaturePanel.cpp` | Mod | the "Rules" toggle (small announced change) |
| `plugin/src/PluginEditor.cpp` | Mod **(hot until F00 lands; after F00 only the panel registration line)** | mount/hide RulesPanel |
| `plugin/CMakeLists.txt` | Mod **(hot)** | add `RulesPanel.cpp`, `tests/test_rules_gui.cpp` |
| `plugin/tests/test_rules_gui.cpp` | New | GUI tests |
| `docs/specs/chop-roles-and-grammar.md`, `docs/BUILD_STATUS.md` | Mod | document the editor |

## 10. Risks and mitigations
- **Refactoring `Gen::query` could change generation.** Mitigation: move the code verbatim in the first commit and run the golden test before anything else; revert if any hash moves
  (never re-pin).
- **`explain()` wording drifts from the real decision.** `GrammarPolicy::explain` already derives from the trace; `explainEvent` only appends, and a test asserts agreement of `allowed`
  with `evaluate`.
- **Hand-placed hits contradicting rules confuse users.** The explicit sentence in the text plus the QA step; Non-goal to enforce rules on edits (would surprise F02 direct editing).
- **Overlay vs host windows.** An in-editor overlay avoids modal-loop problems; detect early by opening the panel in REAPER and Bitwig during F15 host testing.
- **Draft/applied divergence after project load.** Handled by reset-on-reload with the message above; covered by a composition test of the signature check (`rules()` equality).
- **UI-level warnings duplicating module messages.** Keep them to the two listed; anything else belongs in `validateRules`.

## 11. Implementation steps
1. `candidateQueryFor` refactor + test (golden unchanged). 2. `describe` and `preservingRule` + tests. 3. `RuleEditorModel` + tests and the `plugin_ui_adapter` manifest/CMake dependency
   change. 4. `composition/src/rules.cpp` (`clearRole`, notes, `explainEvent`, `previewRules`) + composition tests. 5. RulesPanel Rules tab (list, form, templates) and footer
   Apply/Revert/Apply and Mutate + GUI tests 1-2. 6. Roles tab and Problems/Why area + GUI tests 3-5. 7. Docs, module manifest regeneration, snapshot review (`CHOPFRACTAL_SNAPSHOT`),
   manual QA run.
