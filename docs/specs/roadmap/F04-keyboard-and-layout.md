# F04 Keyboard and layout

**Status:** Ready for development  **Size:** M (6 engineer-days)  **Depends on:** F00 (panels, StatusBar), F01 (`ProjectExtras::ui` persistence), F02 (Delete / Duplicate commands)  **Blocks:** F05 (StatusBar widths), F06 (editor space)

## 1. Summary and user value
The editor is a fixed 940x860 mouse-only window: no keyboard shortcuts, no tooltips, painted areas invisible to screen readers, the third Feature row is clipped (1054 px of controls in 920 px), and the window cannot be resized or scaled. F04 adds shortcuts (G, M, Z, Shift+Z, L, undo/redo, arrows, Enter, Delete), a defined focus order, tooltips with value/range/units, a resizable layout with a minimum size and an interface scale, accessibility handlers for the four painted panels, double-click reset on sliders, and a Simple/Advanced view split. Producers work faster; keyboard and screen-reader users can operate the plugin; the plugin fits small laptop screens and large monitors.

## 2. User stories and scope
- As a producer I press G to generate, M to mutate, Ctrl/Cmd+Z to undo, and arrows to walk through hits, so I never leave the keyboard.
- As a producer I hover any control and see its current value, range, default and shortcut.
- As a producer I resize the plugin window (and choose 75 % to 200 % scale) and nothing is clipped or overlapping.
- As a new user I switch to Simple view and see only Generate, Mutate, Density, Variation, Swing, Seed and the essential file actions.
- As a blind or low-vision user, a screen reader announces each panel, the selected hit and the status messages.

**In scope:** key map and dispatch; hit-neighbour navigation; tooltips; resizable layout engine; FeaturePanel flow; Simple/Advanced; interface scale; focus order and focus ring; accessibility titles/descriptions/announcements; slider double-click reset; persistence of size/scale/view through `ProjectExtras::ui` (F01).
**Non-goals:** user-rebindable keys, Space transport control (the host owns transport), MIDI-learn, localization, a new theme, touch gestures, changing any control's function, popping the editor out into a separate window.

## 3. UX
**Shortcuts** (`Cmd` = Ctrl on Windows/Linux, Command on macOS, via `juce::ModifierKeys::commandModifier`). A shortcut is ignored when the focused component is a `juce::TextEditor`, or a ComboBox popup is open. Keys not listed (or listed with extra modifiers) are not consumed, so host shortcuts keep working.
| Key | Action (`EditorAction`) | Calls | Notes |
|---|---|---|---|
| G | Generate | `cmd.generate()` | |
| M | Mutate | `cmd.mutate()` | |
| Z | Zoom into hit | `cmd.zoomInSelected()` | needs a selection (existing refusal message) |
| Shift+Z | Collapse | `cmd.collapseSelected()` | |
| L | Lock/unlock bar | `cmd.toggleLockSelectedBar()` | |
| Cmd+Z | Undo | `cmd.undo()`; `cmd.undoMarkers()` when WavePanel has focus | |
| Cmd+Y, Cmd+Shift+Z | Redo | `cmd.redo()`; `cmd.redoMarkers()` when WavePanel has focus | |
| Left / Right | Previous / next hit in time | `neighborHit(.., Previous/Next)` then `model.select` | canvas focus only |
| Up / Down | Hit in the adjacent lane, nearest in time | `neighborHit(.., LaneUp/LaneDown)` | canvas focus only |
| Enter | Audition the selected hit's chop | `proc.audition(chopId)` | |
| Delete, Backspace | Delete selected hit; selected marker when WavePanel has focus | `cmd.deleteHit`, `cmd.deleteMarker` | |
| Cmd+D | Duplicate selected hit after itself | `cmd.duplicateHit(id, r.bar, r.startInBar + r.durationTick)` | |
| Esc | Clear selection; cancel a drag | `model.select({})` | |
| Home, + / - | Fit waveform; zoom waveform | WavePanel | WavePanel focus only |
| Space | Not consumed (returns false). First press per editor instance shows `Playback follows your DAW's transport. Press play in the host to hear the pattern; Enter auditions the selected hit.` | `cmd.say` | |
| Tab / Shift+Tab | Next / previous control | JUCE traverser | focus order below |
"Canvas focus" = the focused component is WavePanel, PatternPanel, OrbitPanel or HistoryPanel (or the editor itself). Focus is shown by a 2 px outline `0xff7cf29a` inset 1 px on the focused canvas.

**Focus order** (`setExplicitFocusOrder`): ControlsPanel 1, FeaturePanel 2, WavePanel 3, PatternPanel 4, OrbitPanel 5, HistoryPanel 6, StatusBar 7. ControlsPanel and FeaturePanel are `FocusContainerType::keyboardFocusContainer`; canvases are `setWantsKeyboardFocus(true)`.

**Tooltips:** `juce::TooltipWindow` owned by the editor, 600 ms delay. Exact texts (shortcut shown with the platform modifier):
Load Loop `Load a WAV or AIFF loop (up to 60 seconds).`; Detect Chops `Find chop markers using the detection mode at the bottom row.`; Generate `Generate a new pattern from the chops (G).`; Mutate `Change the unlocked, unedited parts of the pattern (M).`; Undo `Undo the last pattern change (Ctrl+Z).`; Redo `Redo (Ctrl+Y).`; Zoom In `Subdivide the selected hit into a nested pattern (Z).`; Collapse `Remove the selected hit's zoom (Shift+Z).`; Lock Bar `Lock or unlock the bar of the selected hit (L).`; New Seed `Pick a random seed. Generate and Mutate use it.`; Embed `Store the audio inside the project (larger project file).`; Smart Setup `Suggest roles and a loop tempo for the loaded loop.`; Accept Roles `Give the suggested roles to chops that have none.`; Use Tempo `Use the suggested tempo as the manual fallback tempo.`; Fractal `Replace unlocked bars with a fractal groove built from the motif.`; Motif `Fractal motif: x = hit, a = accent, . = rest.`; Evolve `Mutate the pattern automatically every N loops.`; Keep `Save the current evolved pattern as a family-tree branch.`; Export WAV `Render the pattern to a WAV file.`; Export Kit `Write chop slices, a MIDI file and kit.txt to a folder.`; Locate Source `Find the project's original audio file.`; Store A / Store B `Store the current pattern in slot A (B).`; Recall A / Recall B `Recall slot A (B). Undoable.`; Set Role `Give the selected hit's chop the role shown at left.`
Host-parameter sliders/toggles/combos: `host::describeParam(def, plainValue)` (new, portable, section 5), using the manifest names, e.g. `Density: 50% (range 0% to 100%, default 50%)`, `Output Gain: -6.00 dB (range -60 to 12 dB, default 0 dB)`, `Pattern Length: 4 bars (choices: 1, 2, 4, 8; default 4)`, `Effect Enable: on (default on)`. The value part updates while hovering (the slider overrides `getTooltip()`).
Canvas tooltips: WavePanel `Click to audition a chop. Double-click to add a marker.` (F02 behavior); PatternPanel `Click a hit to select it. Arrow keys move the selection.`; HistoryPanel `Click a variation to switch to it. Right-click for more.`; OrbitPanel `One ring per chop. Click a hit to select it.`

**Resizing:** `setResizable(true, true)` with corner grip; limits min 720x700, max 1920x1400; default 940x860 (or the project's saved size, clamped). Layout (`EditorLayout`, below) distributes space; nothing is clipped at any legal size. **Interface scale:** StatusBar `ComboBox` titled `Interface scale` (72 x 20, items `75%`, `100%`, `125%`, `150%`, `200%`, default 100%) calls `setScaleFactor(pct/100.0f)`; the editor's logical size is unchanged. **View mode:** StatusBar `TextButton` titled `View mode` (84 x 20) labelled `Simple view` while in Advanced and `Advanced view` while in Simple. Both are right-aligned in StatusBar left of any export Cancel button (F01) and persisted in `ProjectExtras::ui`.
**Simple view shows:** ControlsPanel row 1 (all buttons), row 2 (Seed, New Seed, Embed, Length), sliders Density, Variation, Swing, Dry mix, Output gain (hides FX intensity and the seven effect toggles; height 140); FeaturePanel groups Setup (Smart Setup, Accept Roles, Use Tempo), Files (Export WAV, Export Kit, Locate Source) and Detect; Wave, Pattern, History, Orbit. **Hidden in Simple:** Fractal group, Evolve group, export Format/Rate/Loops, A/B group, Role group, effect toggles. Hidden controls keep their values and keep working through shortcuts; hidden controls are removed from the focus order.
**Sliders:** double-click resets a host-parameter slider to its manifest default (`Slider::setDoubleClickReturnValue(true, def.defaultValue)`).
**Accessibility strings:** panel titles `Waveform and chop markers`, `Pattern grid`, `Variation family tree`, `Orbit view`, `Status`, `Controls`, `Features`. Descriptions (updated on `modelChanged`): WavePanel `<n> chops, <m> markers.` or `No audio loaded.`; PatternPanel/OrbitPanel `<bars> bars, <k> hits. Selected: <EventRect.description>` or `No hit selected.`; HistoryPanel `<n> variations. Active: <label or id>.`. Announcements (`AccessibilityHandler::postAnnouncement`, `AnnouncementPriority::medium`): the selected hit's `description` when selection changes by keyboard; every status message whose first character is not empty (F05 adds severity).

## 4. Data model and state
No new persisted fields: `ProjectExtras::ui.viewMode`, `scalePercent`, `width`, `height` (F01, defaults 1, 100, 940, 860). Size is written 500 ms after the last resize (timer), scale and view immediately, through `EditorModel::setExtras` + `updateHostDisplay`. New transient state: `EditorModel::focusedCanvas`, spoken-once flag for the Space hint. No host parameters. No change to any module state format; pattern_engine golden hashes (2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128) untouched.

## 5. Public API
```cpp
// plugin/src/EditorKeys.h  (JUCE, message thread; pure mapping so it is unit-testable)
enum class EditorAction { Generate, Mutate, ZoomIn, Collapse, ToggleLock, Undo, Redo, PrevHit, NextHit, LaneUp, LaneDown,
                          Audition, Delete, Duplicate, Escape, FitWave, WaveZoomIn, WaveZoomOut, SpaceHint };
std::optional<EditorAction> mapKey(const juce::KeyPress&, bool canvasFocus);   // nullopt = not ours; pure
// plugin/src/PluginEditor.h
bool ChopFractalEditor::keyPressed(const juce::KeyPress&) override;           // dispatches mapKey -> EditorCommands/model; returns false for SpaceHint and unmapped keys
// plugin/src/EditorLayout.h
struct LayoutInput  { juce::Rectangle<int> bounds; bool advanced = true; int featureRows = 4; };
struct LayoutResult { juce::Rectangle<int> controls, feature, wave, pattern, history, orbit, status; };
LayoutResult computeLayout(const LayoutInput&);                                // pure
// FeaturePanel: flow layout with fixed groups
int FeaturePanel::rowsFor(int widthPx, bool advanced) const;                   // greedy wrap, 8 px gap between groups
```
`modules/plugin_ui_adapter/include/.../views.hpp` (portable):
```cpp
enum class HitDirection { Previous, Next, LaneUp, LaneDown };
EventId neighborHit(const PatternView&, EventId current, HitDirection);        // top-level rects only
```
`neighborHit`: sort top-level rects (`depth == 0`) by `(startTick, chopIndex, id.value)` (F02 adds `startTick`). `Previous/Next`: index +/- 1, no wrap (returns `current` at the ends; with no selection `Next` returns the first rect, `Previous` the last). `LaneUp/LaneDown`: nearest lane with hits in that direction (`chopIndex` -1/+1, skipping empty lanes); in that lane the rect minimizing `|startTick - currentStartTick|`, ties to the earlier. Invalid/unknown `current` behaves as no selection.
`modules/plugin_host_adapter/include/.../parameters.hpp` (portable, additive, **no manifest change**): `std::string describeParam(const ParamDef&, double plainValue);` Rules: Float with unit `%` (the manifest stores 0..1) shows `round(v*100)` plus `%`, range and default likewise (`50%`, `0% to 100%`); other Float units show the value with 2 decimals and the unit after a space (`-6.00 dB`), range/default in the shortest form with the unit once at the end of the range and after the default; Choice shows the label plus the unit (`4 bars`) and lists all labels; Bool shows `on`/`off`. Format: `"<name>: <value> (range <min> to <max>, default <d>)"`, `"<name>: <value> (choices: <a, b, c>; default <label>)"`, `"<name>: on|off (default on|off)"`.
Layout algorithm (`computeLayout`, margin 10, gaps 6):
```
status   = {10, H-24, W-20, 24}
y        = 10
controls = {10, y, W-20, advanced ? 172 : 140};            y += controls.h + 6
feature  = {10, y, W-20, featureRows*26};                   y += feature.h + 6
avail    = (H - 10 - 26) - y - 12                           // two 6 px gaps; 26 px reserved above the status row as today
wave     = floor(avail*120/540) (>= 80); pattern = floor(avail*230/540) (>= 140); bottom = avail - wave - pattern (>= 120)
if any minimum is violated: raise it to the minimum and take the difference from the largest of the others
orbit    = square s = min(bottom, bottomWidth/2) at the right edge; history = rest minus a 6 px gap
```
At 940x860 Advanced the Feature panel has 4 rows (section 6), so wave/pattern/bottom = 114/218/182 px; this is the one intended visible change from F00's layout (the clipped row is gone).
Threading: all message thread; nothing near the audio thread.

## 6. Behavior details and edge cases
- Dispatch order in `keyPressed`: (1) if a TextEditor is focused return false; (2) `mapKey`; (3) Cmd+Z/Y route to marker undo when `wavePanel.hasKeyboardFocus(true)`, else pattern undo; (4) refusals reuse the existing messages (`Select a hit in the pattern first.` etc.); (5) return true when handled.
- FeaturePanel groups (never split across rows), in order: G1 Setup (Smart Setup, Accept Roles, Use Tempo; 312 px), G2 Fractal (Motif label 50, motif 90, Depth label 50, depth 56, Fractal 90; 336), G3 Evolve (Evolve 90, Every label 44, combo 90, amount 190, Keep 80; 494), G4 Export settings (Format label 50, combo 110, rate 90, Loops label 50, combo 56; 356), G5 Files (Export WAV, Export Kit, Locate Source; 330), G6 Detect (combo 190), G7 A/B (Store A, Recall A, Store B, Recall B; 312), G8 Role (combo 110, Set Role 80; 190). Greedy rows with 8 px gaps: width 920 gives rows {G1,G2}, {G3,G4}, {G5,G6,G7}, {G8} = 4 rows; width 1880 gives 2 rows; width 720 gives 4 rows. Simple view uses G1, G5, G6 only (1 row at >= 840 px, else 2).
- `featureRows` changes (resize or view toggle) trigger a relayout; panels are positioned only by `computeLayout`.
- Resize persistence: `resized()` restarts a 500 ms timer; on fire `setExtras(ui.width/height)`. Restore: constructor reads `extras().ui` once (clamped to limits); later `Change::Extras` from a project load resizes via `setSize` only when the editor is visible and the stored size differs.
- Scale: `setScaleFactor` after construction and on change; combined with host DPI by JUCE; the resize limits apply to logical size.
- Simple view toggling hides components with `setVisible(false)` and removes them from focus (`setWantsKeyboardFocus(false)` while hidden); painted state and values persist; `Change::Extras` (project load) applies the stored mode.
- Selection by keyboard repaints Pattern/Orbit only and scrolls nothing (no scrolling views yet); selection by keyboard announces once.
- `Delete` with nothing selected: no-op, status `Select a hit first.`. Locked targets: engine/F02 messages unchanged.
- VST3 hosts may swallow keys (Live, FL Studio) or deliver them only when the plugin window is focused; behavior is best effort and the mouse path always exists.
- Determinism and real-time: UI only; no session, renderer or audio-thread change.

## 7. Test plan
**Unit**
- `plugin_host_adapter`: `describe_param_formats_float_choice_and_bool` (density 0.5 -> `Density: 50% (range 0% to 100%, default 50%)`; `output_gain_db` -6 -> `Output Gain: -6.00 dB (range -60 to 12 dB, default 0 dB)`; `pattern_bars` index 2 -> `Pattern Length: 4 bars (choices: 1, 2, 4, 8; default 4)`; `effect_enable` 1 -> `Effect Enable: on (default on)`) for every manifest entry: output non-empty and contains name; manifest tests unchanged.
- `plugin_ui_adapter`: `neighbor_hit_walks_time_order_and_lanes` (5 rects, 3 lanes: Next x4 visits all in order, Next at end returns self, Previous with none selects last, LaneDown skips an empty lane, ties choose earlier, nested rects ignored, unknown id = no selection).
- `plugin/tests/test_editor_keys.cpp` (no DISPLAY): `map_key_table_matches_the_spec` (all rows incl. `Cmd+Shift+Z` = Redo, `G` with Ctrl held = nullopt, `Z` vs `Shift+Z`, arrows only with `canvasFocus`, Space = SpaceHint, `Alt+G` = nullopt).
- `plugin/tests/test_editor_layout.cpp`: `compute_layout_has_no_overlaps_and_respects_minimums` (200 deterministic sizes from `Rng(7)` in [720,1920]x[700,1400], both modes, featureRows 1..4: rects inside bounds, pairwise disjoint, wave>=80, pattern>=140, bottom>=120, orbit square, history+orbit+6 gap == bottom width); `feature_flow_wraps_groups_by_width` (920 -> 4, 1880 -> 2, 720 -> 4; Simple at 920 -> 1); `layout_at_940_by_860_matches_the_numbers_in_this_spec` (114/218/182).
**GUI (xvfb; extends F00 helpers)**
- `keyboard_shortcuts_drive_the_session`: Detect via button, then `ed->keyPressed(KeyPress('g'))` returns true and a pattern exists; `'m'` changes the bytes; `KeyPress('z', ModifierKeys::commandModifier, 0)` restores the previous bytes; `Cmd+Y` redoes; with a focused `juce::TextEditor` (seed field `grabKeyboardFocus`) `'g'` returns false and the pattern is unchanged; `KeyPress::spaceKey` returns false and sets the hint text once (second press leaves the status unchanged).
- `arrow_keys_walk_the_hits_and_enter_auditions`: after Generate, `KeyPress::rightKey` x2 -> selection is the 2nd rect in time order; `downKey` changes lane; `returnKey` increments a `proc.audition` test counter; `deleteKey` removes the hit (event count -1) and `Cmd+Z` restores it.
- `focus_order_follows_the_layout`: `ed->createFocusTraverser()->getAllComponents(ed)` yields panel components in the order Controls, Feature, Wave, Pattern, Orbit, History, Status; hidden Simple-view controls are absent.
- `tooltips_exist_for_every_control_and_slider_tooltips_show_values`: walk all descendants; each Button/ComboBox/Slider/TextEditor has a non-empty tooltip (via `SettableTooltipClient`/`TooltipClient::getTooltip`) containing its title or documented text; `density` slider set to 0.8 -> tooltip contains `80%`.
- `editor_resizes_within_limits_and_persists`: `setSize(1200,900)` then pump 700 ms -> `extras().ui.width==1200 && height==900`; `getConstrainer()->getMinimumWidth()==720`, `getMinimumHeight()==700`; all panel bounds pass the layout invariants; `setSize(940,860)` restores; editor created from a state with `ui.width=1000` opens at 1000 wide.
- `simple_view_hides_and_restores_controls`: click `View mode`; `Fractal`, `Evolve`, `Store A` not visible, `Generate`/`Export WAV` visible; values unchanged; project save/reload restores Simple.
- `interface_scale_applies`: set combo `Interface scale` to `150%` -> `ed->getTransform()` scale 1.5 (or `getApproximateScaleFactor`), `extras().ui.scalePercent==150`.
- `accessibility_names_exist`: each of the 7 panels has the title in section 3; each interactive child has a non-empty `getTitle()` or accessibility-handler title; handlers non-null (`getAccessibilityHandler()`); `slider_double_click_resets_to_default` (set density 0.9, simulate double-click -> 0.5).
- Existing `editor_buttons_drive_the_session` and F00's layout test updated only for the four-row Feature panel numbers (F00 test is parameterized by the layout constants).
**Sanitizers:** ASan on all new GUI tests. **Manual QA:** (1) Tab through the whole window; every stop shows a visible focus ring or native focus; Shift+Tab reverses. (2) Keyboard-only: load via Load Loop (Enter), Detect (Space on the button), then G, arrows, Enter, Cmd+Z. (3) Drag the window corner from 720x700 to 1920x1400 and back: no clipping, no overlap, controls reachable. (4) 150 % scale on a 1080p screen: window fits. (5) VoiceOver / NVDA: panel names announced; arrow selection reads the hit description. (6) In REAPER, FL Studio and Live: shortcuts work when the plugin window is focused and typing in the seed field does not trigger them. (7) Simple view: only the documented controls are present; switch back, nothing lost.

## 8. Acceptance criteria
- [ ] Every shortcut in the table works and nothing fires while a text field is focused; unmapped or modified keys return false.
- [ ] `computeLayout` yields non-overlapping, in-bounds rectangles satisfying the minimums for all 200 sampled sizes and both modes.
- [ ] At every legal size and both modes no control is clipped (FeaturePanel group widths sum within the panel per row; test asserts `rowsFor` rows each fit `width`).
- [ ] Every Button, ComboBox, Slider and TextEditor has a tooltip and an accessible title; every host-parameter tooltip equals `describeParam` for the current value.
- [ ] Window size, scale and view mode round-trip through project save/reload.
- [ ] Focus order is Controls, Feature, Wave, Pattern, Orbit, History, Status; a visible focus indicator exists on each canvas.
- [ ] Double-click resets each parameter slider to the manifest default.
- [ ] Idle CPU unchanged: no new repaint while stopped and idle (repaint counter 0/s).
- [ ] pattern_engine golden hashes unchanged; no state schema beyond F01; no audio-thread diff.
- [ ] `docs/BUILD_STATUS.md` gaps "resizable layout, full keyboard navigation" updated.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| plugin/src/PluginEditor.h/.cpp | Mod, **HOT** | `keyPressed`, tooltip window, resizable + limits, layout call, scale, size timer |
| plugin/src/EditorKeys.h/.cpp | New | key map |
| plugin/src/EditorLayout.h/.cpp | New | `computeLayout` |
| plugin/src/ControlsPanel.*, FeaturePanel.*, StatusBar.* | Mod | tooltips, groups/flow, Simple hiding, view/scale controls, slider reset |
| plugin/src/WavePanel.*, PatternPanel.*, OrbitPanel.*, HistoryPanel.* | Mod | focus ring, accessibility, tooltips, key hooks |
| plugin/src/EditorModel.*, EditorCommands.* | Mod, **HOT** | focus state, `setUiPrefs` |
| plugin/CMakeLists.txt | Mod, **HOT** | `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` |
| modules/plugin_host_adapter/include/.../parameters.hpp, src/parameters.cpp, tests | Mod | `describeParam` |
| modules/plugin_ui_adapter/include/.../views.hpp, src/views.cpp, tests/test_views.cpp | Mod | `neighborHit` |
| plugin/tests/test_editor_keys.cpp, test_editor_layout.cpp, test_keyboard_gui.cpp | New / Mod | tests |
| docs/BUILD_STATUS.md | Mod | known gaps |

## 10. Risks and mitigations
- `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` makes some hosts forward fewer keys to the DAW: only listed keys are consumed; fallback is `FALSE` (keys then work after clicking a canvas) and a QA note per host.
- Plugin-window resize behavior differs per host (REAPER fine, Live constrains): the editor honors host `setSize` calls; layout is pure so any size is valid; tested across the whole range.
- Four Feature rows at 940 shrink the canvases by ~26 px vs. F00: accepted and documented; Simple view recovers space.
- Screen-reader support for painted panels is limited to group-level descriptions in JUCE 8.0.15 (no virtual children); announcements carry the detail. A later feature may promote hits to child components.
- Shortcut collisions with host (Cmd+Z): only when the plugin window has focus; documented.

## 11. Implementation steps
1. `describeParam` + tests; `neighborHit` + tests.
2. `EditorKeys` + `keyPressed` + commands wiring + tests (needs F00; F02 for Delete/Duplicate, else those rows are skipped by a feature flag until F02 lands).
3. Tooltips (table + `describeParam` sliders) and slider reset.
4. `EditorLayout` + FeaturePanel groups/flow + resizable editor + persistence + tests.
5. Simple/Advanced view and interface scale in StatusBar.
6. Focus order, focus ring, accessibility titles/descriptions/announcements + tests.
7. QA matrix on three hosts, docs update.
