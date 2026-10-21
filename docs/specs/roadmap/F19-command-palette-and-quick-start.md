# F19 Command Palette and Quick Start

**Status:** Ready for development  **Size:** M (6 engineer-days)  **Depends on:** F04 (shortcut table and focus order; F19 reuses its action registry), F00 (editor split, so the palette can address panels). Soft: F03 (presets as palette entries), F05 (notices for checklist hints), F07/F08/F16/F17/F18/E01/E02 (their actions register themselves)  **Blocks:** none
**Category:** ease of use. No audio, pattern, state-file or manifest changes.

## 1. Summary and user value
A **command palette** (Ctrl+K on Windows/Linux, Cmd+K on macOS) opens a search box over every action and every parameter in the plugin: type `swing`, `export`, `density`, `reverse`, press Enter. Parameters can be set from the palette (`density 0.6`). A **Quick Start checklist** guides first-time users through the five steps that make sound: load a loop, detect chops, generate, play, export; each step ticks itself when the user does it and offers a button that does it. Both are pure UI: no manifest rows, no project state, golden hashes and renderer untouched.

## 2. User stories and scope
- New user: sees a short checklist, follows it, and has exported a pattern in under two minutes.
- Power user: opens the palette with the keyboard, types a few letters, runs any command without hunting through panels; can see each command's shortcut.
- User: sets a numeric parameter by typing (`chaos 35`), toggles a toggle, picks a choice (`rate 1/8`).
- User: dismisses or reopens the checklist; it never reappears after completion unless reset.
**In scope:** action registry, fuzzy matcher (portable module), palette component, parameter commands, recents, quick-start checklist component and tracker, per-user persistence file, tests. **Non-goals:** natural-language commands, plugin-wide undo of palette actions beyond existing undo, user-defined macros, mouse-recording, cloud sync, usage telemetry (nothing is sent anywhere; the tracker is local), tutorial overlays pointing at controls, localized strings (English only now; strings live in one table for later).

## 3. UX
### 3.1 Palette
- Open: Ctrl/Cmd+K anywhere in the editor, or `TextButton` "?" in StatusBar (title `Command palette`, tooltip `Search every action and parameter (Ctrl+K).`). Esc or click outside closes. The editor must have keyboard focus (hosts that swallow shortcuts still get the button).
- Layout: centered overlay 520 x 360 (smaller editors: width = editor width - 40): `TextEditor` (title `Command search`, placeholder `Type a command or parameter...`), `ListBox` of up to 8 visible results (scrolls), a footer label `Enter run, Up/Down choose, Esc close`.
- Result row: `Category   Name   [value or shortcut]`, e.g. `Generate   Generate new pattern   G`, `Parameter   Swing   0.35`, `Preset   Dusty 90s   (F03)`. Category chips are text, not colour only. The matched characters are bold.
- Ranking: empty query shows Recents (last 8 run, newest first) then Suggested (top actions for the current state: `Load loop` when empty, `Detect chops`, `Generate`, `Play hint`, `Export WAV`). Typing ranks by fuzzy score (6.2). Ties by category order `Action, Parameter, Preset, Help` then alphabetical.
- Running: Enter runs the selected command; actions close the palette. Parameter commands: `Swing` row Enter replaces the query with `swing ` and shows `Value (0 to 1, now 0.35): ` hint; typing a number and Enter sets it (clamped, shown in the StatusBar `Swing set to 0.60.`). Toggles run immediately; choices list their entries as sub-rows. Out-of-range or unparsable input shows `0 to 1 expected.` in the footer and keeps the palette open.
- Disabled commands appear dimmed with reason (`Generate (load a loop first)`) and cannot run (Enter shows the reason in the footer).
- Empty results: `No command matches "xyz".`
### 3.2 Quick Start
- A card (300 x 150) docked in the WavePanel's empty state (when no source is loaded) and reachable any time through `Help` palette command `Show quick start` and a `TextButton` "Quick start" in the StatusBar (shown until completed or dismissed). Contents: title `Quick start`, five rows with a check mark glyph (text `done` for screen readers), step name, and an action button:
  1. `Load a loop` - button `Load...` (opens the loop chooser) 
  2. `Detect chops` - button `Detect`
  3. `Generate a pattern` - button `Generate`
  4. `Play it` - button `Audition` (plays the hint/audition path; in hosts the text says `Press play in your DAW`)
  5. `Export` - button `Export WAV` (opens the existing export flow)
  Footer: `Skip` (title `Skip quick start`) and `Do not show again` toggle (title `Hide quick start`). Completed: `You are set. Press Ctrl+K to find everything else.` for 5 s, then the card collapses to the StatusBar button until dismissed.
- Steps tick automatically from observed events, not from button clicks alone (see 6.3). They can complete out of order. A step's button is disabled with a reason until its prerequisite is met (`Load a loop first.`).
**Messages:** `Quick start: loop loaded.`, `Quick start: 5 of 5 done.` (Info, quiet). **Accessibility:** palette is a modal text-search pattern with `ListBox` row text read in full, Up/Down moves selection while focus stays in the editor; checklist rows have titles `Quick start step 1 of 5: Load a loop, done`.

## 4. Data model and state
### 4.1 Action registry (`plugin/src/CommandRegistry.h/.cpp`, JUCE side; plus portable matcher)
```cpp
struct Command {
  std::string id;            // stable, e.g. "action.generate", "param.swing", "preset.<name>"
  std::string title;         // "Generate new pattern"
  std::string category;      // "Action" | "Parameter" | "Preset" | "Help"
  std::vector<std::string> keywords;     // "make", "new", "roll"
  std::string shortcut;      // display string from F04, empty if none
  std::function<bool(std::string& whyNot)> enabled;   // null = always
  std::function<void(const std::string& arg)> run;    // arg empty for plain actions
  enum class ArgKind : std::uint8_t { None, Number, Choice, Bool } arg = ArgKind::None;
  double min = 0, max = 1;   std::vector<std::string> choices;
  std::function<std::string()> currentValue;           // for display
};
class CommandRegistry { public: void add(Command); const std::vector<Command>& all() const; /* built at editor construction, message thread only */ };
```
Every parameter in the manifest (`ParameterTable` rows, including future ones) is registered automatically by a loop over the table, using the row name, kind, range and choices, so new parameters need no palette code. Actions are registered by their owner components in one `registerCommands()` per panel (F00), listed in section 6.1.
### 4.2 Portable matcher: `modules/fuzzy_match` (C++17, no JUCE; namespace `chopfractal::fuzzy`)
```cpp
struct Match { int score = 0; std::vector<std::uint8_t> positions; bool matched = false; };
Match score(std::string_view query, std::string_view candidate);       // case-insensitive, ASCII fold
std::vector<std::pair<int, int>> rank(std::string_view query, const std::vector<std::string_view>& candidates, int limit);
```
### 4.3 Per-user persistence (not project state, not host parameters)
File `<user data dir>/ChopFractal/quickstart.json` (same root as F03 presets and `UiPrefs`; written atomically via temp+rename):
```json
{"version":1,"dismissed":false,"steps":{"load":true,"detect":false,"generate":false,"play":false,"export":false},"recents":["action.generate","param.swing"]}
```
Missing, corrupt or unreadable file: defaults (a fresh checklist, no recents), no error shown, a log line only. Recents capped at 8, stored ids that no longer exist are dropped on load. The file never contains project paths or any content. `Reset quick start` is a palette command (Help). Nothing in `.chopfractal` project state changes, and the plugin state blob is untouched.

## 5. Public API
```cpp
class QuickStartTracker {                         // plugin/src/QuickStartTracker.h/.cpp, message thread
 public:
  enum Step { Load, Detect, Generate, Play, Export, kCount };
  void notify(Step);                              // idempotent; saves file (debounced 500 ms); emits onChange
  bool done(Step) const; int doneCount() const; bool dismissed() const;
  void setDismissed(bool); void reset();
  std::function<void()> onChange;
};
```
Hook points (one line each, in existing code, all on the message thread): after a source is accepted -> `Load`; after `detectChops` returns Ok -> `Detect`; after a successful Generate/Mutate -> `Generate`; first audition/hint play or first transport-driven event batch observed by `pollAudioFlags()` (the processor already sees "playing") -> `Play`; after a successful WAV/MIDI/kit export -> `Export`. Palette API: `CommandPalette::show()`, `hide()`, `isShowing()`.
Threading: message thread only. The audio thread is not involved (the "playing" observation uses the existing atomic polled at 30 Hz).

## 6. Behavior details and edge cases
**6.1 Actions registered (initial list; owner registers more later).** Load loop; Clear source; Detect chops; Generate; Mutate; Undo; Redo; Lock/Unlock selection; Audition selected; Export WAV; Export MIDI; Export Producer Kit; Open presets (F03); Save preset; Next/Previous variation; Toggle Evolve; Reseed; Fractal on/off; Toggle each Allow flag (retrigger, reverse, pitch, filter, glide, crunch; echo, tremolo, level match, limiter, groove as they land); Open Groove, Output, Arrangement dialogs; Show quick start; Reset quick start; Show keyboard shortcuts (F04); Show help for the focused control (`ControlHelp`). Parameters: all manifest rows automatically. Presets: factory and user presets by name (F03) with `Load preset <name>`.
**6.2 Matcher.** Query and candidate are lower-cased ASCII; non-ASCII bytes pass through unfolded. Subsequence match required (all query characters in order). Score = sum over matched characters: +16 base, +8 if at word start (after space, `_`, `-`, `/` or at index 0), +6 if consecutive with the previous match, +4 for exact case match, -1 per skipped candidate character after the first match (capped at -12 total), +24 if the whole query is a prefix of the title, +40 if it equals the title. Ties broken by shorter candidate then lower registry index. Candidates scored: `title`, and each `keyword` (keyword score minus 4). Whitespace in the query splits tokens that must each match (AND), in any order. Deterministic, no randomness, no locale.
**6.3 Quick-start tracking rules.** Steps are marked done when the real event occurs, even if the user used the normal UI. `Play` requires at least one audible event batch (host playing with the plugin enabled, or an audition); merely pressing a button without sound does not count. State is global per user, not per project: opening a project with a loop loaded counts `Load` the first time ever. `dismissed` hides the card and the StatusBar button; `reset()` clears steps and recents? **Decision:** reset clears steps and the dismissal only, recents stay.
**6.4 Interaction and focus.** While the palette is open it captures the keyboard; host key forwarding is blocked for its text editor only. Palette commands that open dialogs close the palette first. Running a command never bypasses its own enable checks; the palette reuses the same functions the buttons call, so behavior, undo entries and messages are identical. Shortcut display strings come from F04's table (single source), `Ctrl` shown as `Cmd` on macOS.
**6.5 Edge cases.** Rapid typing: ranking is O(commands x length) with at most 300 commands, done synchronously (under 1 ms measured). Window smaller than 520 px: overlay shrinks, rows truncate with ellipsis, full text in a tooltip. Parameter commands act through the `AudioProcessorValueTreeState`/parameter objects with `beginChangeGesture`/`endChangeGesture`, so host automation recording and undo behave like a UI drag. Toggling a parameter that does not exist in an older manifest (e.g. before F16 merged) simply is not registered (registration reads the table). Unwritable preference directory: tracker keeps state in memory for the session. Two plugin instances: the file is last-writer-wins on whole file (steps are only ever set to true, so merges converge by re-reading before writing).
**6.6 Neutrality.** No change to the manifest, project state, pattern, renderer or exports. Golden hashes and all regression hashes are unchanged by construction; the CI checks that `git diff` of those test fixtures is empty.

## 7. Test plan
Unit (`modules/fuzzy_match/tests/test_fuzzy.cpp`): `subsequence_required`; `prefix_beats_scattered_match`; `word_start_bonus_ranks_swing_amount_above_song_wing`; `consecutive_bonus`; `case_insensitive`; `multi_token_and_semantics`; `empty_query_matches_all_with_score_zero`; `ranking_is_stable_and_deterministic_across_runs`; `positions_returned_in_order_and_within_bounds`; `non_ascii_bytes_do_not_crash`; `limit_respected`; a golden table of 20 (query, candidates, expected order) pairs.
Tracker/registry (`plugin/tests/test_quickstart.cpp`, `test_command_registry.cpp`): `steps_tick_from_real_events_not_clicks` (drive `ProjectSession` calls and assert); `step_is_idempotent_and_persisted_atomically`; `corrupt_file_falls_back_to_defaults`; `unwritable_dir_keeps_memory_state`; `reset_clears_steps_not_recents`; `recents_cap_8_and_unknown_ids_dropped`; `all_manifest_parameters_register_as_commands` (count equals table size, names match); `every_action_command_has_title_category_and_enabled_reason_when_disabled`; `command_ids_are_unique_and_stable` (golden list of ids committed).
GUI (xvfb, `plugin/tests/test_palette_gui.cpp`): `ctrl_k_opens_and_esc_closes` (and Cmd on macOS CI by simulated modifier); `typing_filters_and_enter_runs_generate_changing_the_pattern`; `parameter_command_sets_value_with_clamp_and_gesture`; `invalid_number_shows_footer_hint_and_stays_open`; `disabled_command_shows_reason`; `question_mark_button_opens_palette`; `empty_state_text`; `quick_start_card_shows_five_rows_and_buttons_gated_by_prerequisites`; `completing_all_five_collapses_the_card`; `titles_and_tooltips_present_for_every_new_control` (F04 walk).
Sanitizers: ASan/UBSan; no TSan-sensitive code beyond the existing polled atomic. Manual QA: first-run flow with a drum loop reaches 5 of 5 in under two minutes; palette usable by keyboard only; screen reader reads rows; Ctrl+K works in REAPER and Live where the host allows it (F15 matrix; the `?` button is the fallback).

## 8. Acceptance criteria
- [ ] Ctrl/Cmd+K opens the palette; every action in 6.1 and every manifest parameter is reachable by name; running a command is equivalent to its button (same undo entry and message).
- [ ] Matcher is deterministic and passes the golden ranking table; typing latency under 16 ms with 300 commands (benchmark logged).
- [ ] Parameter commands clamp, show errors in the footer, and use change gestures; disabled commands show their reason.
- [ ] Quick Start ticks on real events in any order, persists per user in `quickstart.json`, survives a corrupt file, can be dismissed and reset.
- [ ] No change to manifest, plugin state, project files, renderer, generator, golden hashes; feature-free state bytes identical.
- [ ] Palette and checklist are fully keyboard operable, with titles, tooltips and text (not colour-only) status; docs and BUILD_STATUS updated.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/fuzzy_match/**` (CMake, module.json, README, MIGRATION, header, src, tests, example) | New | matcher |
| `plugin/src/CommandRegistry.h/.cpp`, `CommandPalette.h/.cpp`, `QuickStartCard.h/.cpp`, `QuickStartTracker.h/.cpp` | New | UI and logic |
| `plugin/src/PluginEditor.cpp/.h` (or F00 panels' `registerCommands`) | Mod **(hot)** | registry build, Ctrl+K key handling, StatusBar buttons, tracker hooks |
| `plugin/src/PluginProcessor.cpp` | Mod **(hot)** | "playing observed" notification via `pollAudioFlags()` |
| `plugin/src/PresetManager.cpp` (F03) | Mod | user data dir helper reuse, preset names for the palette |
| `plugin/CMakeLists.txt`, root `CMakeLists.txt` | Mod **(hot)** | sources, module |
| `plugin/tests/test_palette_gui.cpp`, `test_quickstart.cpp`, `test_command_registry.cpp` | New | tests |
| docs: `BUILD_STATUS.md`, `docs/specs/gui-and-interaction.md` shortcut table, roadmap README | Mod | docs |

## 10. Risks and mitigations
- **Hosts swallow Ctrl/Cmd+K.** Mitigation: the `?` StatusBar button and a `Help` entry in the existing menu; tested in the host matrix.
- **Registry drift: actions added without registration.** Mitigation: a test that every `TextButton`/`ToggleButton` with a title has a command or is on an allowlist (`test_every_button_has_a_command`).
- **First-run checklist annoys experts.** Mitigation: `Skip` and `Do not show again`, dismissal is persistent.
- **Per-user file shared between plugin instances.** Mitigation: set-only steps and re-read before write.
- **Row budget in StatusBar (74 px) is tight.** Mitigation: the two small buttons (`?` 24 px, `Quick start` 90 px) go in the existing right-side group; verified by the F00 layout test.
- **Parameter commands bypass F04 focus conventions.** Mitigation: they use the same attachment/gesture path as sliders.

## 11. Implementation steps
1. Module `fuzzy_match` with golden table. 2. `CommandRegistry` with automatic parameter registration and unit tests. 3. Palette component, key handling, `?` button, recents file. 4. Action registration per panel and the allowlist test. 5. `QuickStartTracker`, hooks and persistence. 6. Quick Start card and StatusBar button. 7. GUI tests, docs, registries, QA.
