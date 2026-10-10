# F03 Presets and favorites

**Status:** Ready for development  **Size:** M (6 engineer-days)  **Depends on:** F00 (ControlsPanel, HistoryPanel), F01 (`ProjectExtras` incl. grid and tree-filter prefs, `setExtras`)  **Blocks:** F09 (scenes reuse the preset apply path), F10

## 1. Summary and user value
The plugin starts blank: no factory presets (BUILD_STATUS "Known gaps") and no way to keep a setup. F03 adds a portable `preset_store` module (preset file format, 12 deterministic factory presets, a user-folder library), a Presets menu in ControlsPanel (apply, save, rename, delete, favorite, undo), and a favorites filter plus search on the variation tree in HistoryPanel. Producers get a quick start, can keep their own recipes, and can find the good variations in a long family tree.

## 2. User stories and scope
- As a producer I pick "Boom Bap Basics" and press Generate to get a musical result without touching 12 controls.
- As a producer I save my current settings (density, swing, effects, seed, Fractal motif, rules) as a named preset and reuse it in any project.
- As a producer I star presets and variations, then show only the favorites.
- As a producer I type part of a variation's name and see only matching nodes.

**In scope:** `modules/preset_store` (format, validation, factory data, library on a folder); `ProjectSession::applyPreset`; processor helper to set parameters; Presets menu; preset undo (one level); tree filter function and HistoryPanel overlay; double-click to favorite a node; star glyph for favorites.
**Non-goals:** presets that contain audio, markers, chop roles or a source reference (a preset is a generation starting point; spec `state-presets-automation-export.md`); cloud/sharing; MIDI program changes (`getNumPrograms()` stays 1); changing `dry_mix`, `output_gain_db`, `effect_enable` (never touched by presets); a rule editor (F06); preset morphing (F10); import/export of preset packs.

## 3. UX
**ControlsPanel row 2** (F00 layout: Seed label 50, seed 110, New Seed 90, Embed 210 = 460 px used; Length(bars) combo is right-aligned) gains, left to right after Embed: `ComboBox` titled `Pattern grid` (84 px, `reduced(2)`; items `1/4`, `1/8`, `1/16` (default), `1/32`, `1/8T`, `1/16T`; writes `ProjectExtras::gridDivision/gridTriplet`; `Generate` uses it) and `TextButton` `Presets` (100 px, `reduced(2)`, title `Presets`).
**Presets menu** (`juce::PopupMenu`, exact structure and strings):
```
Undo Preset                        (disabled unless an applied preset can be undone)
Apply and Generate                 (checkable, default on; stored as the APVTS state property `presetAutoGenerate`, the same mechanism as `embedSource`)
---
Favorites >                        (submenu; empty = disabled item "No favorites yet")
Hip-hop > ...  Breaks > ...  Glitch > ...  Fractal > ...  Minimal > ...      (factory, alphabetical by name, star prefix "* " on favorites)
User >                             (user presets alphabetical; disabled "No user presets" when empty)
---
Save Preset...                     (AlertWindow "Save preset": text field "Name", combo "Category", buttons "Save" / "Cancel")
Rename Preset >                    (submenu of user presets -> AlertWindow "Rename preset")
Delete Preset >                    (submenu of user presets -> confirm "Delete preset?" "Delete" / "Cancel")
Toggle Favorite >                  (submenu of all presets)
Show Presets Folder
```
Selecting a preset applies it. Messages (status line, exact): applied `Preset "<name>" applied.`; applied+generated `Preset "<name>" applied and generated.`; saved `Preset "<name>" saved.`; exists confirm (AlertWindow) `A preset named "<name>" already exists. Replace it?` with `Replace` / `Cancel`; invalid name `Preset names are 1 to 48 characters and cannot contain \ / : * ? " < > |`; newer file `"<name>" was made by a newer ChopFractal and cannot be loaded.` (the item is listed greyed); unknown settings `"<n>" setting(s) from a newer version were ignored.`; undo `Preset undone.`; folder error `Could not write the preset folder: <reason>`.
**Factory behavior:** applying sets the 11 generation parameters (below), the seed field, the generation grid, Fractal motif and depth, Evolve every/amount/ramp/ceiling (Evolve stays off), and replaces the project's rule set with the preset's rule template (empty template clears rules). It never touches `dry_mix`, `output_gain_db`, `effect_enable`, markers, roles, the pattern or history until Generate runs.
**HistoryPanel (724 x 190) overlay** (top-right, 4 px from edges): `ToggleButton` `Favorites` (title `Show favorites only`, 84 x 20) and `TextEditor` (title `Search variations`, 130 x 20, empty-text hint `Search`, max 32 chars). Filtered view keeps: nodes that match, the active node always, and re-parents children of hidden nodes to their nearest visible ancestor so the tree stays connected. Empty result text: `No variations match. Clear the search or turn off Favorites.` Favorite nodes additionally draw a 5-point star (outline 8 px radius) right of the dot so favorite state is not colour-only. Double-click on a node toggles favorite (`setFavorite`). Existing click-to-activate and right-click menu are unchanged. Search matches case-insensitively against the label, the decimal node id and the decimal seed.

## 4. Data model and state
```cpp
// modules/preset_store/include/chopfractal/preset_store/preset.hpp
namespace chopfractal::preset {
constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::size_t kMaxFileBytes = 256 * 1024;
struct ParamValue { std::string id; double value = 0.0; };          // manifest ids; plain (not normalized) values; a Choice stores its index
struct Preset {
  std::string name;                // 1..48 UTF-8 bytes, no \ / : * ? " < > | or control chars
  std::string category = "User";   // one of Hip-hop, Breaks, Glitch, Fractal, Minimal, User
  std::string description;         // 0..120 bytes
  std::vector<ParamValue> params;  // <= 32 entries; duplicates rejected
  std::uint64_t seed = 1;          // 0..999999999999
  int gridDivision = 16;  bool gridTriplet = false;
  std::string motif = "x.xx";  int fractalDepth = 2;               // as ProjectExtras
  evolve::Settings evolve;                                          // enabled is forced false on apply
  roles::RuleSet rules;                                             // <= 64 rules, validated by roles::validateRules
};
Status validate(const Preset&);
std::vector<std::uint8_t> serialize(const Preset&);                 // codec container, modules: preset_meta, preset_params, preset_gen, preset_rules (each schema 1)
Result<Preset> deserialize(const std::uint8_t*, std::size_t);       // UnsupportedVersion for newer, Corrupt for malformed, size-checked before allocating
const std::vector<Preset>& factoryPresets();                        // 12, order as in the table, built once, immutable
}
```
File extension `.cfpreset`; payload is the `state_codec` container (`codec::encode` with `CodecLimits{maxTotalBytes = 256 KiB}`), so CRC and size validation come for free. Rules use `roles::serialize(RoleMap{}, rules)` bytes as the `preset_rules` payload (so the rule schema version rides along). No change to any project state format; F01's `ProjectExtras` already carries seed/motif/depth/grid. Favorites for presets: `favorites.txt` in the preset folder, UTF-8, one key per line, `factory:<name>` or `user:<file stem>`, max 200 lines (extra lines ignored). Host parameters: none added.

**Factory presets** (generation params; `bars` is the bar count; the stored `pattern_bars` value is the choice index (1 -> 0, 2 -> 1, 4 -> 2, 8 -> 3, see `host::barsFromChoice`); unspecified toggles false; `fxI` = `fx_intensity`; all `dry/gain/enable` untouched). Rules: `backbeat` = `templates::preserveFirst(1,"kick")` + `templates::requireInFinalBeat(2,"snare")`; `norepeat` = `templates::avoidAdjacentRepeats(1, SameChop)`; `build` = `templates::preferAtPhraseBoundary(1,"cymbal",3.0)` + `avoidAdjacentRepeats(2, SameRole)`; `-` = empty.
| # | Name | Category | dens | swing | var | bars | rev | pitch | retrig | filt | glide | crunch | fxI | motif | depth | grid | rules | seed |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Boom Bap Basics | Hip-hop | 0.45 | 0.30 | 0.25 | 2 | - | - | - | - | - | - | 0.5 | x.xx | 2 | 16 | backbeat | 101 |
| 2 | Dusty Swing | Hip-hop | 0.38 | 0.55 | 0.30 | 2 | - | yes | - | - | - | - | 0.4 | x.x. | 2 | 8 | backbeat | 102 |
| 3 | Chopped Soul | Hip-hop | 0.50 | 0.20 | 0.35 | 4 | - | yes | - | - | - | - | 0.5 | xaxx | 2 | 16 | backbeat | 103 |
| 4 | Break Shuffle | Breaks | 0.65 | 0.35 | 0.40 | 2 | yes | - | yes | - | - | - | 0.5 | x.xx | 2 | 16 | norepeat | 104 |
| 5 | Jungle Roll | Breaks | 0.80 | 0.10 | 0.55 | 2 | - | - | yes | - | - | - | 0.6 | xxax | 3 | 16 | norepeat | 105 |
| 6 | Half-Time Drag | Breaks | 0.30 | 0.15 | 0.20 | 4 | - | - | - | - | - | - | 0.5 | x... | 1 | 8 | backbeat | 106 |
| 7 | Stutter Garden | Glitch | 0.70 | 0.00 | 0.70 | 1 | yes | yes | yes | - | - | yes | 0.7 | xxxx | 3 | 32 | - | 107 |
| 8 | Tape Melt | Glitch | 0.45 | 0.00 | 0.50 | 2 | - | yes | - | - | yes | yes | 0.8 | x.x. | 2 | 16 | - | 108 |
| 9 | Filter Sweep Loop | Glitch | 0.55 | 0.10 | 0.45 | 4 | - | - | - | yes | - | - | 0.6 | x.xx | 2 | 16 | build | 109 |
| 10 | Sierpinski Pulse | Fractal | 0.50 | 0.00 | 0.30 | 4 | - | - | - | - | - | - | 0.5 | x.x | 3 | 16 | - | 110 |
| 11 | Cantor Gaps | Fractal | 0.40 | 0.00 | 0.35 | 8 | - | - | - | - | - | - | 0.5 | xa.x | 2 | 16 | - | 111 |
| 12 | Sparse Minimal | Minimal | 0.20 | 0.00 | 0.10 | 2 | - | - | - | - | - | - | 0.5 | x... | 1 | 8 | - | 112 |
Evolve in every factory preset: `everyLoops 4, amount 0.2, ramp 0, rampCeiling 0.8, startSeed = seed`. Names are unique; the list is the contract: changing a value requires a new test constant.

## 5. Public API
```cpp
// modules/preset_store/include/chopfractal/preset_store/library.hpp (portable, std::filesystem, message/worker thread, never audio thread)
struct PresetEntry { std::string key; std::string name; std::string category; bool factory = false; bool loadable = true; std::string reason; };   // key: "factory:<name>" | "user:<stem>"
class PresetLibrary {
 public:
  explicit PresetLibrary(std::string userDirectory);
  Result<std::vector<PresetEntry>> list() const;                              // factory first, then user; <= 500 user files, each <= kMaxFileBytes; unreadable files listed with loadable=false
  Result<Preset> load(const std::string& key) const;
  Status save(const Preset&, bool overwrite) const;                           // creates the folder; atomic write (temp file + rename); Conflict if exists && !overwrite
  Status rename(const std::string& userKey, const std::string& newName) const;
  Status remove(const std::string& userKey) const;
  std::vector<std::string> favorites() const;  Status setFavorite(const std::string& key, bool on) const;
};
std::string fileStemFor(const std::string& presetName);                       // replaces forbidden chars with '_', trims, max 48
```
`composition/include/.../project_session.hpp` (**HOT**): `Status applyPreset(const preset::Preset&)` — validates, then in one all-or-nothing step: `setExtras` (seed, motif, depth, grid), `setEvolve` with `enabled=false`, `setRules` (validation warnings become notices). Adds `preset_store` to `CHOPFRACTAL_COMPOSITION_DEPS` in `composition/CMakeLists.txt` and to the module list in the top-level `CMakeLists.txt` (**HOT**, before `composition`).
`plugin/src/PluginProcessor.h` (**HOT**): `int applyPresetParams(const std::vector<chopfractal::preset::ParamValue>&)` — message thread; for each known manifest id, `snapValue` then `param->setValueNotifyingHost(normalize(...))`; skips `dry_mix`, `output_gain_db`, `effect_enable`; returns the count of ignored ids. `std::vector<preset::ParamValue> snapshotGenerationParams() const`.
`EditorCommands` (F00): `applyPreset(const preset::Preset&, bool generate)`, `savePresetAs(name, category, overwrite)`, `undoPreset()`, `presetMenu()` (builds the menu; the callback holds a `SafePointer`). `PresetUndo { std::vector<ParamValue> params; ProjectExtras extras; roles::RuleSet rules; evolve::Settings evolve; }` is stored in `EditorCommands` (one level, cleared by a project load/`Change::Extras` from outside).
`modules/plugin_ui_adapter` (`views.hpp`): `struct TreeFilter { bool favoritesOnly = false; std::string text; };` `std::vector<TreeNodeView> layoutHistoryTreeFiltered(const std::vector<history::NodeSummary>&, history::NodeId active, const TreeFilter&);` (calls `layoutHistoryTree` on the reduced, re-parented list).

## 6. Behavior details and edge cases
- **Apply order:** (1) capture `PresetUndo`; (2) `applyPresetParams`; (3) `ProjectSession::applyPreset` (inside `proc.withSession`); (4) model refresh (`Change::Extras`); (5) if `Apply and Generate`, run `EditorCommands::generate()` which reads `proc.currentParams()` (already updated: `setValueNotifyingHost` stores the atomic synchronously) and the seed from extras. `Generate` with `grid` from extras: default 16/straight reproduces today's output exactly.
- **Determinism:** presets contain only data; Generate stays deterministic from (settings, seed, chops, rules). Test pins the factory list hash and "preset then Generate equals manual Generate with identical settings". Pattern_engine golden hashes (2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128) are untouched: the engine and its serializer are unchanged.
- **Failure/rollback:** invalid preset (`validate` fails) -> nothing changes, message is the validation text. `applyPresetParams` cannot fail; `applyPreset` failing after params were set triggers `undoPreset()` automatically (parameters restored from `PresetUndo`).
- **Locks/history/Evolve:** applying a preset never edits the pattern, locks, history or the Evolve running state (`enabled` forced false only in the stored settings; if Evolve is running it stays running with the new every/amount at its next step, so the editor stops Evolve first and says `Evolve stopped to apply the preset.` when it was running).
- **Rules:** preset rules replace the project's rules; roles remain. A rule referring to a role nobody has is valid (no effect) and reported by `validateRules` as a warning notice, as today.
- **File safety:** names are sanitized by `fileStemFor`; a stem collision between two names (`A:B` vs `A_B`) is a Conflict. Reading rejects files > 256 KiB before reading. Directory listing ignores non-`.cfpreset` files, symlinks that leave the folder, and entries beyond 500 (notice `Only the first 500 user presets are shown.`). Windows reserved names (`CON`, `NUL`, ...) get `_` appended.
- **Favorites filter edge cases:** the active node is always shown even if it does not match; a filter that hides every node but the active one still draws it; filtering never changes selection or activation. Filter state persists via `ProjectExtras::ui.favoritesOnly/search` (F01).
- **Threading:** library calls run on the message thread (small files; listing 500 files is < 100 ms) and are performed when the menu opens, not per paint.

## 7. Test plan
**Unit, `modules/preset_store/tests/test_preset_store.cpp`**
- `factory_presets_are_valid_unique_and_stable`: count 12; names unique; each passes `validate`; every `params` id exists in `host::parameterManifest()` (checked in the composition test to keep the module free of the host adapter); `fnv1a` of all `serialize(p)` concatenated equals a pinned constant (fill in at implementation, then frozen).
- `preset_round_trip`: serialize/deserialize equals; rules round-trip through `roles::serialize`.
- `hostile_preset_files_are_rejected`: truncated, CRC flipped, schema 2, 300 KiB, 65 rules, 33 params, duplicate param id, name with `/`, name 49 bytes, control characters -> specific `ErrorCode`s, no crash, no large allocation (run under ASan).
- `library_save_list_rename_delete_in_a_temp_dir`: save two, list = 12 + 2, overwrite refused without flag, rename updates key and file, delete removes, favorites persist across a new `PresetLibrary` instance, atomic write leaves no `.tmp` on success.
- `file_stem_sanitizing`: forbidden chars, reserved names, trimming, collisions.
- `unsupported_newer_preset_is_listed_but_not_loadable`: `loadable=false`, reason `made by a newer ChopFractal`.
**Unit, plugin_ui_adapter**: `tree_filter_keeps_matches_active_and_reparents` (chain 1-2-3-4, only 4 favorite, active 1 -> nodes {1,4}, node 4 parent 1); `tree_filter_text_matches_label_id_and_seed_case_insensitively`; `tree_filter_off_equals_layoutHistoryTree`.
**Integration, composition (`test_presets.cpp`)**
- `applying_a_preset_then_generate_equals_manual_generate`: for each of the 12 factory presets: apply (params applied to a `host::ParamValues`, session `applyPreset`), `generate(settingsFromParams(...) + seed + grid)` bytes == hand-built `Settings` generate bytes in an independent session.
- `preset_does_not_touch_dry_gain_or_effect_enable` (shell test with real APVTS: values before == after).
- `preset_apply_is_all_or_nothing`: invalid rules in a hand-built preset -> extras/rules/evolve unchanged.
- `undo_preset_restores_params_extras_rules_and_evolve`.
- `unknown_param_ids_are_ignored_and_counted`.
**GUI (xvfb, `plugin/tests/test_presets_gui.cpp`)**: `presets_button_menu_has_the_documented_structure` (call `ControlsPanel::buildPresetMenu(menu)`; assert item texts incl. the five factory categories and `Save Preset...`); `choosing_boom_bap_basics_sets_controls_and_generates` (apply by key `factory:Boom Bap Basics`, `density` param 0.45, seed field `101`, pattern exists); `favorites_toggle_filters_the_family_tree` (create 3 nodes via Generate/Mutate, favorite the 2nd, toggle `Favorites`: `HistoryPanel::visibleNodeCount()==2` (favorite + active)); `search_field_filters_nodes_by_label` (rename node "keeper", type `kee`: 1 + active); `double_click_toggles_favorite`. Existing `editor_buttons_drive_the_session` stays green.
**Determinism/sanitizer**: ASan on preset hostile-file test; pattern_engine tests unchanged. **Manual QA:** (1) Open Presets > Hip-hop > Boom Bap Basics with a loop loaded: sliders move, seed shows 101, pattern generated; Undo Preset restores slider values. (2) Save Preset... "My Groove": file appears in the folder shown by Show Presets Folder; restart the DAW: preset listed under User. (3) Rename and Delete work; deleting asks for confirmation. (4) Favorite 2 of 6 variations, turn on Favorites: only they and the active node remain, with stars. (5) Type a name fragment: tree filters live.

## 8. Acceptance criteria
- [ ] 12 factory presets with the exact names/values above; the pinned hash test passes.
- [ ] Apply never changes `dry_mix`, `output_gain_db`, `effect_enable`, markers, roles, pattern or history.
- [ ] Apply + Generate output is byte-identical to a manual Generate with the same settings (all 12 presets).
- [ ] Save/load/rename/delete/favorite work on a temp folder; writes are atomic; hostile files never crash or allocate more than 1 MiB.
- [ ] Undo Preset restores parameters, seed, motif, depth, grid, Evolve settings and rules exactly.
- [ ] Favorites-only view shows exactly {favorites} union {active}; search narrows further; both survive project save/reload (F01).
- [ ] Favorites are marked by a star glyph as well as colour.
- [ ] Pattern_engine golden hashes unchanged; no project state version change beyond F01.
- [ ] `docs/BUILD_STATUS.md` "No factory presets" gap removed; `modules/preset_store` has `module.json`, `README.md`, `MIGRATION.md`, example and passes `tools/smoke_transfer.sh`.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| modules/preset_store/** (CMakeLists, module.json, README, MIGRATION, include, src/preset.cpp, factory.cpp, library.cpp, tests, examples) | New | portable module |
| CMakeLists.txt | Mod, **HOT** | add `preset_store` to the module loop |
| composition/CMakeLists.txt | Mod, **HOT** | add dep |
| composition/include/.../project_session.hpp, composition/src/features.cpp | Mod, **HOT** (header) | `applyPreset` (implementation in features.cpp to keep project_session.cpp free) |
| composition/tests/test_presets.cpp | New | integration |
| modules/plugin_ui_adapter/include/.../views.hpp, src/views.cpp, tests/test_views.cpp | Mod | `TreeFilter`, filtered layout |
| plugin/src/PluginProcessor.h/.cpp | Mod, **HOT** | `applyPresetParams`, `snapshotGenerationParams`, `presetAutoGenerate` property |
| plugin/src/ControlsPanel.*, HistoryPanel.* | Mod | grid combo, Presets button/menu, overlay, star, double-click |
| plugin/src/EditorCommands.*, EditorModel.* | Mod, **HOT** | preset commands, undo state |
| plugin/tests/test_presets_gui.cpp | New | GUI tests |
| docs/BUILD_STATUS.md | Mod | gaps |

## 10. Risks and mitigations
- Preset sound quality is subjective: the table is data-only and each row can be tuned by changing numbers plus the pinned hash in one commit; QA listens to all 12 on the drum fixture and a real break before release.
- Preset folder location differs per OS and may be read-only in sandboxed hosts: failures surface the `Could not write the preset folder` message; menu still works for factory presets.
- Applying parameters via `setValueNotifyingHost` creates 11 automation events in hosts that are recording automation: documented; `Apply and Generate` is one undo step in the plugin but the host sees parameter writes. Mitigation: use `beginChangeGesture/endChangeGesture` around each write.
- Name/stem collisions and reserved names: covered by tests and `Conflict` handling.
- Tree filter hides the node a user expects: the active node is always visible and the empty text explains how to clear.

## 11. Implementation steps
1. `preset_store` module: types, validate, serialize, hostile-file tests, CMake/module scaffolding.
2. Factory table + pinned hash test.
3. `PresetLibrary` + tests.
4. `ProjectSession::applyPreset` + processor `applyPresetParams` + integration tests (needs F01 `setExtras`).
5. Tree filter function + tests; HistoryPanel overlay, star, double-click.
6. ControlsPanel grid combo, Presets menu, commands, GUI tests.
7. Docs, QA listening pass.
