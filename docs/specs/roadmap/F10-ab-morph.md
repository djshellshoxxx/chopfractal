# F10 A/B morph

**Status:** Ready for development  **Size:** M (4 engineer-days)  **Depends on:** F00 (FeaturePanel, EditorModel `displayPattern`), F01 (persisted A/B slots, `ab` section; morph itself
works without it but the slots would not survive a reload)  **Blocks:** none

## 1. Summary and user value
Today the editor has Store A / Recall A / Store B / Recall B: a hard A-or-B comparison. F10 adds a **Morph** slider that blends continuously between the two stored patterns: levels,
pans, pitches, effects and timing glide per event, while hits that exist in only one pattern fade in or out by probability. The producer scrubs the slider while the loop plays (the
result is audible within one audio block), and **Commit Morph** turns any slider position into a normal, undoable, family-tree variation. It is the "find the middle between two beats"
tool, and the building block for smooth transitions.

## 2. User stories and scope
- As a producer I can store two patterns as A and B and drag a slider from A to B, hearing the in-between beats, so that I can discover variations without running Mutate.
- As a producer I can commit the current slider position as a new variation, undo it, and continue editing it.
- As a producer I always know when I am hearing a preview and not my real pattern, and I can cancel it with no change to my project.
- **In scope:** pure `pattern::morph` and its matching/interpolation rules, `ProjectSession` morph preview/commit/cancel, FeaturePanel controls, `PatternSession::snapshot` accessor,
  tests.
- **Non-goals:** morphing patterns of different length, meter or grid (refused), morphing settings-driven generation (it blends two finished patterns, it never regenerates), automating
  the slider as a host parameter (reserved for a later release; no ID is claimed now), morphing nested Recursive Hit Zoom children (they switch at the midpoint, section 6), more than
  two sources, a morph "curve" setting, audio crossfading of rendered audio (the blend is in the event data).

## 3. UX
**FeaturePanel, row "Morph"** (next to the existing `Store A`, `Recall A`, `Store B`, `Recall B` buttons, which keep their text and behavior):
- ToggleButton **"Morph"** (title "Morph between A and B"). Enabled only when both slots are stored (`hasSnapshot(0) && hasSnapshot(1)`); disabled tooltip `"Store A and B first."`. On:
  starts the preview at the last slider value (default 0.5). Off: cancels the preview.
- Slider **"Morph amount"** (0 to 1, step 0.01, default 0.5, horizontal; disabled while Morph is off). Value text: `"A"` at 0, `"B"` at 1, otherwise `"37% B"` (percent toward B); small
  labels `A` and `B` at the ends. Double-click resets to 0.5. Dragging updates the audio at most every 50 ms (coalesced by the editor's 10 Hz-or-faster timer: the pending value is
  applied when it differs from the applied one by at least 0.005, and the final value is always applied on mouse-up).
- Buttons **"Commit Morph"** (enabled while the preview is active) and **"Cancel Morph"** (same).
- PatternPanel/OrbitPanel draw `ProjectSession::displayPattern()` (the morphed pattern while previewing) and PatternPanel shows the text badge **"Morph preview"** in its top-right
  corner (text, not colour only).

**Messages (StatusBar, exact):** `"Store A and B first."`; `"A and B differ in pattern length, meter or grid, so they cannot be morphed."`; `"A and B have too many different hits to
morph (560 > 512)."` (numbers filled in); `"Morphing 37% toward B. Commit Morph to keep it, or Cancel Morph."`; `"Committed the morph as a new variation."`; `"Morph cancelled; your
pattern is unchanged."`; `"Morph ended because the pattern was changed."` (when another command cancels it); `"Cancel the scene playback before morphing."` (F09 present).

**Interactions and enabling:** Store A / Store B stay enabled while morphing and refresh the preview immediately from the new slot content. Recall A/B, Generate, Mutate, Fractal, Zoom,
Collapse, direct edits (F02), Undo/Redo, family-tree activation, Evolve steps, loading a project or source all end the morph with the "ended" message. Evolve cannot be switched on while
morphing (`"Cancel the morph before switching Evolve on."`). Keyboard: Tab order Store A, Recall A, Store B, Recall B, Morph, Morph amount, Commit Morph, Cancel Morph; the slider
responds to arrow keys in 0.01 steps (JUCE default), Page Up/Down 0.1.

## 4. Data model and state
No change to any persisted format and no host parameter (`kManifestVersion` unchanged by this feature). The morph is transient: it is never saved; the A/B slots themselves are persisted
by F01's `ab` section (this spec only reads them). Loading a project always starts with the morph off.
```cpp
// modules/pattern_engine/include/chopfractal/pattern_engine/morph.hpp
namespace chopfractal::pattern {
struct MorphReport { std::size_t matched = 0; std::size_t onlyA = 0; std::size_t onlyB = 0; bool childrenDropped = false; };
// t in [0, 1]. t <= 0 returns `a` unchanged and t >= 1 returns `b` unchanged (byte-identical serialize).
// freeze = false: unmatched events remain in the result with a scaled probability (live preview, resolved at flatten time).
// freeze = true : the same draws are resolved now; events that would be absent are removed and base probabilities restored.
Result<Pattern> morph(const Pattern& a, const Pattern& b, double t, const ChopSnapshot& chops, bool freeze = false, MorphReport* report = nullptr);
}
// PatternSession (session.hpp):
std::shared_ptr<const Pattern> snapshot(std::size_t slot) const;   // nullptr when empty or slot >= 2
// chop_contracts/event.hpp (refactor of existing flatten code, identical behavior):
std::uint64_t probabilitySeedFor(std::uint64_t patternSeed);                                 // hashCombine(patternSeed, 0x50524F42ull)
bool probabilityPasses(std::uint64_t probabilitySeed, EventId id, float probability);       // probability >= 1 || draw(seed, id) < probability
```
`pattern::flatten` uses `probabilitySeedFor(p.settings.seed)` and `flattenInto` calls `probabilityPasses` in place of its inline test (event.cpp line with `probabilityDraw`); results
are bit-identical.

## 5. Public API
Composition (`composition/src/morph.cpp`; declarations in `project_session.hpp`, hot; message thread under `withSession`):
```cpp
Status beginMorph(double t = 0.5);          // both slots required; builds the preview and installs its playback
Status setMorph(double t);                  // 0..1, NaN -> InvalidArgument; recomputes and republishes; no history, no undo entry
bool morphActive() const;  double morphAmount() const;
const pattern::Pattern* displayPattern() const;   // morph preview if active (or the F09 scene override), else pattern()
Status commitMorph(const std::string& label = {}); // freeze=true result via finalize(): one history node + one undo step, ends the morph
void cancelMorph();                         // republish() the real pattern; no notice
```
`storeSnapshot` (currently inline in the header) additionally calls `refreshMorph()`. Every path that changes `patterns_` or the playback source ends the morph: `finalize`, `undo`,
`redo`, `recallSnapshot`, `applyActivation`, `loadState`, `loadSource`, `clearSource`, `reset`, `setEvolve(enabled)`; each gets one line `endMorph(true)` (notice flag). Audio side: no
renderer change; the preview uses `installPlayback(morphed)` (flatten + `makePlayback` + `Mailbox::publish`), so the swap happens at the next audio block, sounding voices finish
naturally, and with F08 present the Chaos overlay is applied on top of the morph (`morph -> chaos -> flatten`). Plugin: the FeaturePanel calls the commands `commands.beginMorph()`,
`setMorph(t)`, `commitMorph()`, `cancelMorph()`; no processor change.

## 6. Behavior details and edge cases
**Preconditions (`morph`).** `a` and `b` must have equal `settings.bars`, `timeSignature` and `grid`, else `ErrorCode::Conflict` with the first UX message. Both must satisfy `validate`.
Total distinct hits `matched + onlyA + onlyB` must be <= `min(a.maxEvents, b.maxEvents)` else `ErrorCode::LimitExceeded` ("A and B have too many different hits to morph (N > M).").
Chops come from the current `ChopSnapshot`; an event whose chop is unknown is dropped from its side before matching (patterns are `sanitize`d first by the session). **Event matching**
(per bar index; events never match across bars; top-level events only; `g = gridTicks(settings.grid)`):
1. *Identity pass:* A event `ea` and B event `eb` pair when `ea.id == eb.id`, `ea.chop == eb.chop` and `|ea.start - eb.start| <= g`. (Ids alone are not trusted: two branches that both
   continued from one ancestor reuse the same new ids for unrelated events.)
2. *Proximity pass:* among still-unmatched events of the same bar compute `cost = |ea.start - eb.start| + (ea.chop != eb.chop ? g : 0)`; candidates with `cost <= g` are sorted by
   `(cost, index in A, index in B)` where an index is the position after sorting the bar's events by `(start, id)`; greedily assign, each event at most once. (A different chop therefore
   only matches at identical start.) Result sets: matched pairs, A-only, B-only. Deterministic: no hashing or unordered iteration is used. **Interpolation (`t` strictly inside (0, 1);
   `lerp(x, y) = x + (y - x) * t` in double, then cast; "switch" = `t < 0.5 ? a : b`):**
| Field (matched pair) | Rule |
|---|---|
| `tx.level` | lerp (a fade from/to a quiet hit works naturally) |
| `tx.pan` | lerp |
| `tx.pitchSemitones` | lerp, clamp +-24 |
| `tx.reverse` | switch |
| `tx.retrigger` | `clamp(llround(lerp), 1, 8)` (halves round up) |
| `fx.filter` | equal -> that; one side Off -> the other side's type for all t in (0,1); both non-Off and different -> switch |
| `fx.cutoff` | lerp, an Off side counts as neutral (LowPass 1.0, HighPass 0.0) |
| `fx.resonance`, `fx.glideSemitones`, `fx.crush` | lerp (absent = 0) |
| `chop` | switch |
| `region` | both empty -> empty; same chop and both set -> lerp of `start` and `end` (`llround`, clamped inside the chop range, `end > start` enforced); otherwise the chosen side's region |
| `start`, `duration` | `llround(lerp)`, `duration >= 1`, `start` clamped to `[0, barTicks - duration]` |
| `probability` | lerp |
| `enabled`, `sourceOverride`, `childActive`, `child` | switch (an event that has a child on the chosen side takes that side's `start`, `duration`, `chop`, `region` too, so the child window stays valid) |
| `locked`, `userOwned` | `locked` from A; `userOwned = a.userOwned || b.userOwned` |
The matched event keeps A's `id`. **Unmatched events** (presence crossfade by probability): A-only keep their id and get `probability = pA * (1 - t)`; B-only are re-identified with
fresh ids `max(a.nextId, b.nextId) + k` (k = rank by `(bar, start, original id)`, so ids are the same for every `t`) and get `probability = pB * t`. Because `probabilityPasses` compares
a fixed per-id draw `u(id)` with the probability, the set of audible A-only events only shrinks and the set of B-only events only grows as `t` rises (monotone, no flicker). A B-only
event that has a child tree keeps the tree; if the final `validate` reports a duplicate id (derived child ids can collide with another event's), all children of B-only events are
dropped and `report->childrenDropped = true`.

**Result structure.** Output = copy of `a` with every beat's events cleared, then each result event inserted into the beat containing its
`start` (`start / beatTicks`, last beat clamped) sorted by `(start, id)`. Scope ids, `locked`, `copyOf` come from `a`; `userOwned` of a bar/beat = either side's. Settings: `bars`,
`timeSignature`, `grid`, `seed` (= `a.settings.seed`, so the probability draws of A's own events are stable while sliding), `swing`, `density`, `variation.*`, `fxIntensity` = lerp;
`allow*` flags = OR; `pitchRange`, `maxRetrigger`, `maxShiftTicks` = max; `maxEvents` = min; `engineVersion` as both. `nextId` = base + number of B-only events. The result must pass
`validate`.

**Endpoints.** `t <= 0` returns `a`, `t >= 1` returns `b`, byte-identical (a test compares `serialize`). Morphing a pattern with itself gives itself for any `t` (every event
matches, all lerps are identity, no unmatched events).

**Commit.** `commitMorph(label)`: `q = morph(a, b, t, chops, freeze = true)`; with `freeze` an unmatched event is kept iff
`probabilityPasses(probabilitySeedFor(q.settings.seed), newId, weight)` for its scaled weight, and kept events get their base probability back (A-only `pA`, B-only `pB`; matched events
keep the interpolated value); `finalize(Result(q), label.empty() ? "Morph A>B 37%" : label)` records the family-tree node (seed = A's seed, parent = active node) and one undo step; the
morph preview ends and the committed pattern is what plays (identical to the preview: same ids, same draws; test asserts equal flattened id lists and event fields). Percent in the label
= `lround(t * 100)`.

**Live preview mechanics.** `setMorph` runs on the message thread: `morph` (O(events^2) per bar, under 1 ms for 512 events) + `installPlayback`. Rapid slider moves
publish at most every 50 ms; `Mailbox::publish` frees superseded playbacks only after the audio thread acknowledged newer ones, so there is no unbounded growth. A failed `setMorph` (for
example a limit after Store A with a denser pattern) keeps the previous preview, shows the error, and leaves the toggle on.

**Missing source / no chops.** The preview needs a loaded
source with matching chops; without them `beginMorph` returns `InvalidArgument` "load a source first". If chops changed since the slots were stored, `sanitize` drops dead events on each
side before matching (same repair rule as variation activation).

**Interactions.** Locks: locks in A's structure are copied as metadata; the morph blends two finished snapshots and does
not enforce locks inside the blend (documented in the tooltip of "Morph"). Roles/rules: not consulted (no generation happens). Undo history and the family tree are untouched until
Commit. Evolve and scenes: mutually exclusive (see UX). Chaos: layered on top for playback only. Determinism: same `(a, b, t, chops)` -> byte-identical `serialize(result)` within a
platform and build (float lerps use plain `+ - *` and `llround`; compilers may fuse operations differently across platforms, which can differ in the last float bit; this is acceptable
because committed results are stored as data and never recomputed).

## 7. Test plan
**Unit, `modules/pattern_engine/tests/test_morph.cpp`:** `endpoints_return_a_and_b_byte_identical`; `morphing_a_pattern_with_itself_is_identity_for_any_t`;
`matched_events_interpolate_numeric_fields` (A: level 1.0, pan -1, pitch 0, filter LowPass cutoff 1.0 res 0, glide 0, crush 0; B: level 0.5, pan 1, pitch 12, filter LowPass cutoff 0.2
res 0.8, glide 12, crush 0.4; at t = 0.25 expect 0.875, -0.5, 3, cutoff 0.8, res 0.2, glide 3, crush 0.1, all within 1e-6); `discrete_fields_switch_at_half` (reverse, chop, enabled; t =
0.49 -> A, 0.5 -> B); `retrigger_rounds_halves_up` (A 1, B 2, t = 0.5 -> 2; A 2, B 5, t = 0.4 -> 3); `filter_type_follows_the_non_off_side_with_neutral_cutoff` (A Off, B HighPass 0.6: t
= 0.5 -> HighPass cutoff 0.3); `start_and_duration_interpolate_and_stay_inside_the_bar`; `region_interpolates_only_for_the_same_chop_and_stays_in_range`;
`matching_prefers_identical_id_then_nearest_start_and_never_crosses_bars`; `different_chop_matches_only_at_identical_start`;
`id_collisions_between_branches_do_not_merge_different_events` (A and B each add a new event with id 41 but different chop/position: both appear, counts onlyA = onlyB = 1);
`unmatched_events_fade_monotonically` (for t = 0, 0.05, ..., 1: the set of flattened A-only ids is non-increasing, B-only non-decreasing; at t = 0.5 the expected count is within the
binomial 3-sigma band for 200 events); `b_only_ids_are_stable_across_t`; `refuses_different_length_meter_or_grid_with_conflict`;
`refuses_over_cap_with_limit_exceeded_and_exact_numbers`; `child_events_take_the_chosen_sides_timing_and_child`; `duplicate_derived_child_ids_fall_back_to_dropping_children`;
`result_validates_and_flattens_for_random_pairs` (200 pairs generated with different seeds and settings, t in {0.1, 0.5, 0.9}); `freeze_matches_preview_flatten` (ids, starts, levels
equal between `flatten(morph(..., false))` and `flatten(morph(..., true))` for 50 pairs); `morph_is_deterministic_run_to_run`; the four golden hashes pass untouched.
**`modules/chop_contracts/tests`:** `probability_helpers_match_flatten_behavior` (events with probability 0.3, 0.7, 1.0: `probabilityPasses` agrees with the presence of the event in
`flattenInto` output for 1000 ids); existing event tests unchanged.

**Integration, `composition/tests/test_morph.cpp`:** `begin_requires_both_slots_and_a_source`;
`preview_publishes_a_playback_without_touching_pattern_history_or_undo` (pattern bytes, `history().listAll().size()`, `canUndo()` equal before/after `setMorph`);
`preview_audio_at_the_endpoints_equals_a_and_b` (`renderOffline` of the preview at t = 0 equals the render of A byte for byte, t = 1 equals B);
`commit_creates_a_node_labelled_with_the_percent_and_one_undo_step` (label `"Morph A>B 37%"`, `undo()` restores the pre-commit pattern, `pattern()` equals the frozen result);
`other_commands_end_the_morph` (loop over Generate, Mutate, undo, recall, activate, loadState, setEvolve: `morphActive()` false and the notice present);
`store_slot_while_morphing_refreshes_the_preview`; `failed_set_morph_keeps_previous_preview`; `morph_survives_marker_edits_via_sanitize`; `ab_slots_persist_and_morph_works_after_reload`
(needs F01 `ab`); `display_pattern_returns_the_preview_while_active`; `chaos_layers_on_top_of_the_morph` (when F08 is present). **GUI, `plugin/tests/test_morph_gui.cpp`** (xvfb, F00
helpers): `morph_controls_enable_with_both_slots_and_commit_adds_a_node`: load `drumLoop()`, Detect Chops, click "Generate", "Store A", "Mutate", "Store B"; before storing B assert
`findButton("Morph")->isEnabled()==false`; after: `static_cast<juce::ToggleButton*>(...)->setToggleState(true, sendNotificationSync)`, pump 100 ms; assert `s.morphActive()` and
StatusBar text contains `"Morphing 50% toward B"`; `findSlider("Morph amount")->setValue(0.25, sendNotificationSync)`, pump 120 ms, assert `morphAmount()` within 0.005 of 0.25 and the
PatternPanel badge component `"Morph preview"` `isVisible()`; click "Commit Morph"; assert `morphActive()==false`, history node count +1, newest label `"Morph A>B 25%"`, `canUndo()`;
click "Undo" and assert pattern bytes equal the pre-commit bytes; click "Morph" on again then "Cancel Morph": bytes unchanged, message `"Morph cancelled; your pattern is unchanged."`.
Also `slider_coalesces_rapid_changes` (50 `setValue` calls within 20 ms cause <= 3 session `setMorph` applications, counted through a test hook on the command layer) and the
accessibility-title check.

**Determinism / sanitizers / real-time:** ASan/UBSan on morph fuzz; no audio-thread code changes, so the allocation test stays as is; the preview path is
message-thread only (a TSan run of the existing mailbox stress covers publication).

**Manual QA:** (1) Generate with seed 1, Store A; Generate with another seed, Store B. (2) Enable
Morph, loop playing: at A the loop equals A (Recall A and compare by ear), at B equals B. (3) Drag slowly: hits fade in/out one by one, levels and filters glide, no clicks beyond normal
voice behavior. (4) Commit at 40 %, then play: identical to the preview; Undo returns to the previous pattern; the family tree shows "Morph A>B 40%". (5) Press Store A while morphing:
the preview updates immediately. (6) Press Generate while morphing: the preview ends and the message appears.

## 8. Acceptance criteria
- [ ] `morph(a, b, 0)` and `morph(a, b, 1)` are byte-identical to `a` and `b`; `morph(a, a, t)` equals `a`.
- [ ] Event matching follows the two-pass rule exactly, including the id-collision case, and never crosses bars; pinned by tests.
- [ ] Every field follows the interpolation table; discrete fields switch at `t >= 0.5`; the pinned numeric example matches within 1e-6.
- [ ] Unmatched events fade monotonically; B-only ids are independent of `t`; committed results flatten to the same events as the preview.
- [ ] The preview changes only the playback: `pattern()`, history, undo depth, A/B slots and saved state are identical before and after any number of `setMorph` calls.
- [ ] Commit creates exactly one family-tree node (label with percent) and one undo step; Cancel and every other command end the preview with the specified messages.
- [ ] Different length/meter/grid and over-cap cases are refused with the exact messages and leave state untouched.
- [ ] The slider's audible effect arrives within one audio block plus 50 ms of the last value; at most one `setMorph` application per 50 ms during drags.
- [ ] No serialized format, host parameter or manifest change; the four golden hashes `2397844821793184813`, `12084884237330071892`, `13710596758976548913`, `18237151833431327128` are
      unchanged (the `flattenInto` refactor is behavior-identical).
- [ ] All new controls have titles and keyboard operation; the preview is flagged by text.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/pattern_engine/include/.../morph.hpp`, `src/morph.cpp`, `tests/test_morph.cpp` | New | `morph` |
| `modules/pattern_engine/include/.../session.hpp`, `src/serialize.cpp` | Mod | `PatternSession::snapshot` |
| `modules/pattern_engine/src/edit.cpp` | Mod | `flatten` uses `probabilitySeedFor` |
| `modules/chop_contracts/include/.../event.hpp`, `src/event.cpp`, `tests/` | Mod | `probabilitySeedFor`, `probabilityPasses`; test |
| `composition/src/morph.cpp`, `composition/tests/test_morph.cpp` | New | session methods |
| `composition/include/.../project_session.hpp`, `composition/src/project_session.cpp` | Mod **(hot)** | declarations, `endMorph` lines in the listed entry points, inline `storeSnapshot` -> `refreshMorph`, `morph_` member, `displayPattern` |
| `composition/CMakeLists.txt` | Mod | sources/tests |
| `plugin/src/panels/FeaturePanel.h/.cpp`, `PatternPanel.cpp` | Mod | Morph row, badge, draw `displayPattern()` (F00 `EditorModel` change: one accessor swap) |
| `plugin/CMakeLists.txt` | Mod **(hot)** | add `tests/test_morph_gui.cpp` |
| `plugin/tests/test_morph_gui.cpp` | New | |
| `docs/specs/variation-family-tree.md`, `gui-and-interaction.md`, `BUILD_STATUS.md` | Mod | document morph |

## 10. Risks and mitigations
- **Morph sounds unmusical when patterns are unrelated.** Expected: most hits are unmatched and fade by probability; QA step 3 and the tooltip explain; matching tolerance (one grid
  cell) is a named constant in `morph.cpp` for tuning.
- **Id collisions across branches** are real in this codebase (`nextId` is per pattern); covered by explicit test and by not trusting ids alone.
- **`flattenInto` refactor touching shared code.** Keep it mechanical; golden test and `probability_helpers_match_flatten_behavior` are the guard; revert if any hash moves.
- **Preview diverges from commit.** Same builder with `freeze`; test `freeze_matches_preview_flatten`.
- **Stale preview after external changes.** Central `endMorph` calls at every mutation entry point; a unit test iterates all of them; add new mutators to the list when written.
- **Performance of rapid republish.** Coalescing + O(n^2) with n <= 512 per pattern (n per bar far smaller); fallback: raise the coalescing interval to 100 ms.
- **Two features define `displayPattern()` (F09, F10).** Both specs state the same contract; the second PR to merge reuses the existing function and only adds its override.

## 11. Implementation steps
1. `probabilitySeedFor`/`probabilityPasses` refactor + `PatternSession::snapshot` + tests (goldens unchanged). 2. `morph.hpp/.cpp`: matching, interpolation, structure rebuild,
   validation fallback + unit tests. 3. Freeze mode + equality-with-preview test. 4. Session: `beginMorph/setMorph/commitMorph/cancelMorph`, `displayPattern`, `endMorph` hooks +
   integration tests. 5. FeaturePanel controls, badge, EditorModel accessor swap, coalescing + GUI tests. 6. Docs, QA, snapshot review.
