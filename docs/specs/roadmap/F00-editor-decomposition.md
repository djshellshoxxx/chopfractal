# F00 Editor decomposition

**Status:** Implemented (as-built notes in section 12)  **Size:** M (5 engineer-days)  **Depends on:** none  **Blocks:** F01 (UI half), F02, F03, F04, F05, F06 and every later feature that adds editor controls

## 0. Implementation status (audited 2026-10-20)
**Implemented** (see section 12). Audit-time notes follow. `plugin/src/PluginEditor.cpp` is now 1404 lines (header about 150 lines), not 815, because pass 3 (chaos, presets, drag-out, direct hit editing) landed in the monolith before this split. The window is 940 x 912, not 940 x 860. Decisions recorded by the audit:
- Section 3 geometry below is the **as-built** layout (the old 815-line numbers were replaced). Pixel values must still be re-read from `ChopFractalEditor::resized()` on the day F00 starts; the snapshot comparison in section 7 remains the arbiter.
- The transport row (Play, Stop, Follow DAW, hint label, "Move / resize hits" toggle), the Chaos slider, Reroll, the preset combo with Save Preset / Delete, and the Drag MIDI / Drag WAV handles are existing widgets and move verbatim: transport row, sliders, Chaos and the edit-mode toggle into ControlsPanel; preset combo, Save Preset, Delete, Reroll and both drag handles into FeaturePanel row D.
- Direct hit editing (`DragMode`, `dragId_`, `dragStart_`, `nudgeSelected`, `editSelected`, Shift+click add, Alt+drag duplicate, Delete/M/D/arrow keys) moves into PatternPanel (mouse) and stays in the editor shell for `keyPressed` until F04 takes over. Section selection (`selectingSections_`) is split between WavePanel and PatternPanel. `DragOut.h/.cpp` and `PresetManager.h/.cpp` are already separate files and are kept as they are.
- The status area is a read-only scrolling `juce::TextEditor` ("Status messages", 920 x 70 visible) at the bottom, not a 24 px label. StatusBar owns it. F05 and F04 widget placement that assumed a 920 x 24 bar must use this area plus a new 24 px strip carved from its top (decision: StatusBar becomes 920 x 74 = 24 px widget strip on top of the 50 px scrolling log; F05 section 3 is adjusted accordingly).
- Any "old lines N-M" reference in this spec points at the 815-line baseline and is historical: locate code by symbol name (`refresh`, `mouseDown`, `resized`, ...), not by line.
- The Feature-row overflow described below is already fixed in the as-built editor (four rows A to D, the widest is 870 px), so the "known and preserved defect" no longer exists and F04 does not need to re-flow for it.

## 1. Summary and user value
`plugin/src/PluginEditor.cpp` was 1,404 lines and one class (`ChopFractalEditor`) that owned about 80 widgets, the view caches, the session commands, painting and hit-testing for four canvases, direct hit editing, keyboard editing, presets and drag-out. F00 splits it into seven `juce::Component` panels, an `EditorModel` (cached view data, selection, transient UI state) and an `EditorCommands` layer (every action that calls `ProjectSession`), so features land in separate files. Pure refactor: same widgets, same pixels, same strings, same session calls. The user sees no change; developers gain parallel lanes.

Note: this spec was first written against an 815-line editor. The editor had grown (transport row, presets, edit mode, drag-out, keyboard editing, scrolling status text, 940x912 window) before F00 started; the sections below describe the editor as it was when F00 was implemented, and section 12 lists every place the implementation departs from the first draft. `docs/specs/roadmap/README.md` defers to this file for panel names and responsibilities.

## 2. User stories and scope
- As a developer I can add a control to one panel without touching the other six, so feature branches stop conflicting.
- As a developer I can unit test view building and refusal messages without a window (EditorModel, EditorCommands).
- As a producer I notice nothing: every button, label, tooltip title, status string and layout position is unchanged.

**In scope:** new files listed in section 9; moving code verbatim; recursive widget lookup in the GUI test helpers; layout identical at 940x912 (as built).
**Non-goals:** new features, behavior changes, resizing, keyboard handling, accessibility changes, changing any `ProjectSession` or module API, state format changes.

## 3. UX
Unchanged. Window 940x912, `setResizable(false,false)`. The panels are laid out by `ChopFractalEditor::resized()` at exactly these bounds (editor-local coordinates; margin 10, derived from the old `resized()` arithmetic):

| Panel | x | y | w | h | Contents (moved verbatim) |
|---|---|---|---|---|---|
| ControlsPanel | 10 | 10 | 920 | 232 | transport row (Play, Stop, Follow DAW, hint label, "Move / resize hits" toggle), row 1 buttons (Load Loop, Detect Chops, Generate, Mutate, Undo, Redo, Zoom In, Collapse, Lock Bar; 96 px pitch, `reduced(2)`), row 2 (Seed label 50, seed editor 110, New Seed 90, Embed toggle 210, bars combo right 90), the 5+2 sliders and 7 toggles block (128 px) |
| FeaturePanel | 10 | 248 | 920 | 104 | row A: Smart Setup, Accept Roles, Use Tempo, Motif, Depth, Fractal; row B: Evolve, Every, amount slider, Keep, Format, rate, Loops; row C: Export WAV, Export Kit, Locate Source, detection-mode combo, Store/Recall A/B; row D: preset combo, Save Preset, Delete, Reroll, role combo, Set Role, Drag MIDI, Drag WAV |
| WavePanel | 10 | 358 | 920 | 120 | waveform, markers, empty-state text |
| PatternPanel | 10 | 484 | 920 | 230 | lane grid, hits, bar locks |
| HistoryPanel | 10 | 720 | 808 | 106 | family tree |
| OrbitPanel | 824 | 720 | 106 | 106 | Orbit View (`min(bottom.h, bottom.w/2)` square, right aligned) |
| StatusBar | 10 | 830 | 920 | 70 | the scrollable read-only status text |

Inside a panel the same `removeFromTop/Left` sequences are used, relative to the panel's own bounds (no extra `reduced(10)`). All painting uses panel-local coordinates (the Orbit View is the one exception, section 12); the colour constants, fonts (13 px; 10 px ring numbers), alpha values and strings are copied unchanged, including the three empty-state strings, the `"locked"` bar label and the `"Orbit view"` placeholder.

The first draft preserved a Feature-row overflow (row C needing 1,054 px). It no longer exists: Role and Set Role live in row D, and every row fits in 920 px. F04 still re-flows the rows, but there is nothing to fix first.

## 4. Data model and state
No persisted state, no host parameters, no schema change. `EditorModel` holds only transient UI state that today lives in `ChopFractalEditor` members: seed value, selection, Smart Setup result, status text, `pushedEvolve_`.

```cpp
// plugin/src/EditorModel.h
struct EditorView {                       // replaced wholesale on every rebuild; panels read it by const&
  std::uint64_t revision = 0;             // ++ on each rebuild; panels may cache by it
  bool hasSource = false, hasPattern = false;
  std::int64_t sourceFrames = 0;
  cf::ui::PeakBins peaks;                 // computed for the wave panel's width; kept while source and width are unchanged
  std::vector<cf::ui::MarkerView> markers;
  std::vector<cf::ChopInfo> chops;
  cf::ui::PatternView pattern;
  cf::ui::OrbitView orbit;
  std::vector<cf::ui::TreeNodeView> tree;
  int treeCols = 0, treeRows = 0;
  std::int64_t barTicks = 0, gridTicks = 0; int barCount = 0;   // pattern geometry, kept from the previous view while there is no pattern
};
```
`evolveRunning`, the session's Evolve settings and `sourceMissing` change between rebuilds, so they are model accessors refreshed every tick (`evolveRunning()`, `evolve()`, `sourceMissing()`), not view fields.

## 5. Public API
All classes live in `plugin/` (JUCE shell, message thread only). The audio thread never touches any of them. Only `EditorModel` and `EditorCommands` call `ChopFractalProcessor::withSession`; panels never do (checked by grep, section 8).

```cpp
// plugin/src/EditorModel.h
class EditorModel {
 public:
  enum class Change { View, Evolve, Selection, Seed, Status, Setup, Flags, Extras /* emitted from F01 on */ };
  struct Listener { virtual ~Listener() = default; virtual void modelChanged(Change) = 0; };
  explicit EditorModel(ChopFractalProcessor&);
  void addListener(Listener*); void removeListener(Listener*);

  // Same signature rule as today's refresh(): rebuild when the session signature changed or `force`.
  // Also runs the per-tick Evolve/locate/embed sync. Returns true when the view was rebuilt.
  bool refresh(bool force, int waveWidthPx);
  const EditorView& view() const { return view_; }

  cf::EventId selected() const;  void select(cf::EventId);     // cleared by refresh if the hit vanished
  int selectedBar() const;                                      // 0 if none (as today)
  int selectedChopIndex() const;                                // -1 if none
  bool hasSelection() const;

  std::uint64_t seed() const;    void setSeed(std::uint64_t);   // default 1, 12 decimal digits max
  const cf::composition::SetupSuggestion& setup() const;  bool haveSetup() const;  void setSetup(cf::composition::SetupSuggestion);
  juce::String statusLine() const;     // proc.statusMessage() + the two host-fallback hints, same text as today
  void setStatus(const juce::String&);                          // proc.setStatusMessage + Change::Status

  // pass-throughs so panels never touch the processor
  double playheadQuarters() const;  bool hostPlaying() const;  bool usedFallbackTempo() const;  bool usedFallbackMeter() const;
  double manualBpm() const;  double activeBpm() const;
  bool embedSource() const;  void setEmbedSource(bool);
};

// plugin/src/EditorCommands.h
struct FractalUi { std::string motif = "x.xx"; int depth = 2; };                 // 1..3
struct EvolveUi  { bool enabled = false; int everyIndex = 2; double amount = 0.25; };  // everyIndex 0..4 -> {1,2,4,8,16}
struct ExportUi  { int formatIndex = 1; int rateIndex = 1; int loopsIndex = 1; };      // 24-bit, 48 kHz, 2 loops
enum class DetectChoice { TransientsReplace = 1, TransientsKeepMine = 2, Grid8 = 3, Grid16 = 4 };  // ComboBox ids

class EditorCommands {
 public:
  EditorCommands(ChopFractalProcessor&, EditorModel&);
  // The old ChopFractalEditor::run(): withSession, takeNotices (last notice wins, as today), set status
  // (error message or notice), model.refresh(true). Returns the action's Status.
  cf::Status run(const std::function<cf::Status(cf::composition::ProjectSession&)>& action);

  void chooseFile();  void locateSource();                       // FileChooser, then proc.loadFileAsync(f[, true])
  void detectChops(DetectChoice);                                // exact lambda from the old detect_.onClick
  void generate();  void mutate();  void undo();  void redo();
  void zoomInSelected();  void collapseSelected();  void toggleLockSelectedBar();
  void newSeed();                                                // random & 0x7fffffff
  void storeSnapshot(std::size_t slot);  void recallSnapshot(std::size_t slot);
  void setRoleOfSelected(const juce::String& role);
  void smartSetup();  void acceptRoles();  void useTempo();
  void applyFractal(const FractalUi&);  void setEvolve(const EvolveUi&);  void keepEvolved();
  void exportWav(const ExportUi&);  void exportKit();
  void activateNode(cf::history::NodeId);                         // Immediate unless proc.hostPlaying()
  void historyMenu(cf::history::NodeId, juce::Component* anchor);// the existing 4-item PopupMenu
  void auditionFrame(std::int64_t frame);                          // old mouseDoubleClick body
  void addMarkerAt(std::int64_t frame);  void removeMarkerNear(double xPx, double tolerancePx = 8.0);
  void say(const juce::String&);                                  // model.setStatus
};

// plugin/src/EditorPanels.h  (one header per panel: WavePanel.h, PatternPanel.h, ... listed in section 9)
class EditorPanelBase : public juce::Component, protected EditorModel::Listener {  // registers/unregisters itself
 protected:
  EditorPanelBase(EditorModel&, EditorCommands&, const char* componentId);        // setComponentID(componentId)
  EditorModel& model_;  EditorCommands& cmd_;
};
class WavePanel    : public EditorPanelBase { /* paint, mouseDown (drag-select chops and audition; Shift+click adds a marker; Alt+click removes the nearest marker), mouseDoubleClick (audition) */ };
class PatternPanel : public EditorPanelBase { /* paint, mouseDown/Drag/Up (select, drag-select sections, move, resize, Alt+drag duplicate, Shift+click on empty space adds a hit; one gesture = one undo step on mouse-up) */ };
class HistoryPanel : public EditorPanelBase { /* paint, mouseDown (activate / popup menu) */ };
class OrbitPanel   : public EditorPanelBase { /* paint (reads model.playheadQuarters()), mouseDown (hitTest select) */ };
class ControlsPanel: public EditorPanelBase { ControlsPanel(EditorModel&, EditorCommands&, juce::AudioProcessorValueTreeState&); /* owns attachments */ };
class FeaturePanel : public EditorPanelBase { /* modelChanged(Evolve/Setup/Flags) syncs widgets; owns pushedEvolve_ */ };
class StatusBar    : public EditorPanelBase { /* paint: model.statusLine(); repaints when the composed line changes */ };
```
Component IDs (used by tests): `"ControlsPanel"`, `"FeaturePanel"`, `"WavePanel"`, `"PatternPanel"`, `"HistoryPanel"`, `"OrbitPanel"`, `"StatusBar"`.

`ChopFractalEditor` keeps: constructor (creates model, commands, seven panels in the order above, `setSize(940, 912)`, `setResizable(false,false)`), `resized()` (table in section 3), `paint()` = `g.fillAll(0xff15181d)` only, the 10 Hz `Timer` and `~ChopFractalEditor`. Timer rule: `model.refresh(false, wavePanel.getWidth())`; on rebuild the model emits `Change::View` and WavePanel/PatternPanel/HistoryPanel/OrbitPanel repaint; if `playheadQuarters() >= 0 || lastPlaying_` only OrbitPanel repaints (the old code repainted the whole editor but only Orbit shows the playhead); StatusBar repaints when its composed line differs from the last painted line.

## 6. Behavior details and edge cases
- Verbatim move rule: every lambda body, string literal, number and call order is copied; only `this`-member access becomes `model_`/`cmd_`/panel-local access. Exceptions are exactly: coordinate origin (`waveArea_.getX()+x` becomes `x`), `status_` writes become `model_.setStatus(...)`, `selected_` becomes `model_.selected()`.
- `refresh()` split: signature + rebuild stays in `EditorModel::refresh`; the Evolve toggle/combos/slider sync (old lines 652-661) becomes `FeaturePanel::modelChanged(Change::Evolve)`; the model emits `Evolve` whenever `evolveRunning` or `everyLoops/amount/startSeed` differ from what it last published, on every tick (not only on rebuild), preserving today's behavior. `locate_.setEnabled(sourceMissing)` and the embed toggle sync are `Change::Flags`, emitted per tick when either value changed.
- Selection survives a rebuild if the id is still in `view().pattern.rects`, else clears (old lines 689-691). Orbit and Pattern clicks both write the same selection.
- Peaks are recomputed only when the source pointer or `waveWidthPx` changed (old condition); the width is `WavePanel::getWidth()` (920) so output is identical.
- Mouse routing: the editor no longer has `mouseDown`; each panel handles clicks in its own coordinates. Double-click audition lives in WavePanel.
- Destruction order: panels (and their `SliderAttachment`s) are destroyed before `EditorModel`; attachments are declared after the widgets they bind, as today. `SafePointer` is kept in `historyMenu` callbacks.
- Threading: model/commands/panels are message-thread only. `withSession` lock scope per call is unchanged (no new nested locks; `EditorCommands::run` is the only caller that holds the lock while running a user lambda).
- Determinism: no session call changes, so pattern_engine golden hashes (2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128) cannot move; the tests below do not touch them.

## 7. Test plan
Step 0 changes only test infrastructure so the safety net runs before and after the refactor.
- **Test support (step 0):** move `drumLoop`, `FakePlayHead`, `prepare`, `makeSession`, `patternBytes`, `findButton`, `findCombo` from `test_plugin_shell.cpp` into `plugin/tests/shell_test_support.hpp` (inline). Make `findButton`/`findCombo` recursive (`root.findChildWithID` is not enough: iterate `getChildren()` depth-first). Replace the final accessibility loop with a recursive walk over all `juce::Button` descendants. Assertions are untouched. Run against the old editor to prove the helpers are equivalent.
- **Existing GUI test (the net):** `editor_buttons_drive_the_session` (under xvfb) must pass unchanged after every step. Its clicks: Detect Chops, Mutate (refused, `!hasPattern()`), Generate, Mutate (`m != g`), Undo (`== g`), Redo (`== m`), Store A, Fractal (`f != m`), Recall A (`== m`), Lock Bar with no selection (no bar locked, status contains "Select a hit"), Keep, Smart Setup, Evolve toggle on/off (`evolveRunning()` true/false), Zoom In, Collapse, Set Role, every button has a title.
- **New `plugin/tests/test_editor_layout.cpp`:**
  - `editor_panels_have_the_pre_refactor_bounds`: create editor, `setVisible(true)`, pump 60 ms; for each ID in section 3, `findChildWithID(id)->getBounds()` equals the table row exactly; `editor->getWidth()==940 && getHeight()==912`.
  - `editor_snapshot_is_unchanged_by_the_split`: with `CHOPFRACTAL_SNAPSHOT_REF` set, compare `createComponentSnapshot` to the PNG at that path pixel by pixel (0 differing pixels; skipped if the env var is absent).
- **New `plugin/tests/test_editor_model.cpp` (no DISPLAY needed):**
  - `model_refresh_builds_views_only_when_the_session_changes`: empty session: `view().hasSource==false`, `refresh(true,920)` returns true; second `refresh(false,920)` returns false; after `loadSource`+`applyChops`+`generate`: `refresh(false,920)` true, `view().markers.size()==chopMap()->markers().size()`, `view().pattern.rects` non-empty and equals `ui::buildPatternView` output size, `view().orbit.arcs.size()==pattern rects with depth 0 + nested`, `view().tree.size()==history().size()`.
  - `model_selection_survives_rebuild_and_clears_when_the_hit_is_gone`: select `rects[0].id`; `mutate` with amount 0 keeps it; `generate` with a new seed clears it (`!hasSelection()`).
  - `model_emits_changes_to_listeners_once_per_cause`: counting listener sees `View` once per rebuild, `Evolve` once after `setEvolve` start, `Selection` once per distinct `select`.
  - `commands_report_refusals_with_the_existing_strings`: `mutate()` without pattern -> `statusMessage()=="Generate a pattern first."`; `zoomInSelected()` with no selection -> `"Select a hit in the pattern first."` and the seed field unchanged; `toggleLockSelectedBar()` -> `"Select a hit first; Lock Bar locks the bar that hit is in."`; `undo()` with nothing -> `"Nothing to undo."`.
  - `commands_mutate_advances_the_seed_only_when_a_pattern_exists`: seed 1 -> after refused `mutate()` still 1; after `generate()` then `mutate()` it is 2.
- **Sanitizer:** existing ASan job runs `ChopFractalShellTests` under xvfb; no new job. Real-time: unaffected (no audio-thread code touched).
- **Manual QA:** (1) Build before and after; run the editor with `CHOPFRACTAL_SNAPSHOT=/tmp/before.png` then `/tmp/after.png`; `cmp` reports identical (if not, diff must be limited to anti-aliasing of text, <= 1/255 per channel, and noted in the PR). (2) Load a WAV, Detect, Generate, click a hit, Zoom In, Collapse, Lock Bar, right-click a family-tree node and use each of the 4 menu items, drag nothing. (3) Click the waveform: marker added; Alt-click near it: removed; double-click in a chop: audition plays. (4) Press Evolve, change Every and the amount, close and reopen the editor: controls show the same values. (5) Save the project, reload: identical.

## 8. Acceptance criteria
- [x] `editor_buttons_drive_the_session` passes unmodified except for the step 0 helper relocation/recursion.
- [x] `editor_panels_have_the_pre_refactor_bounds`, all `test_editor_model.cpp` tests and the full CI (Release, Werror matrix, ASan/UBSan, `plugin-linux` job both with `env -u DISPLAY` and under `xvfb-run`, and `tools/validate_vst3.sh`) are green.
- [x] `plugin/src/PluginEditor.cpp` is <= 250 lines and contains no `paint` body other than `fillAll`; `PluginEditor.h` declares no `juce::TextButton`.
- [x] `grep -n "withSession" plugin/src/*Panel.cpp plugin/src/StatusBar.cpp` returns nothing.
- [x] No file under `modules/` or `composition/` is modified; `git diff --stat` shows only `plugin/` and `docs/BUILD_STATUS.md`.
- [x] Every user-visible string in the old `PluginEditor.cpp` still exists exactly once in the new sources (`git grep -c` before/after for each literal in a checklist the PR attaches).
- [x] Pattern_engine golden hashes unchanged (existing `ctest` passes).
- [x] Idle CPU: with no playback and no edits the timer repaints nothing (verified by a repaint counter in a temporary debug build; PR notes the number of repaints per second is 0).

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| plugin/src/PluginEditor.h / .cpp | Mod, **HOT** | reduced to the editor shell (section 5); every later feature rebases on it |
| plugin/src/EditorModel.h / .cpp | New | view cache, selection, seed, setup, status |
| plugin/src/EditorCommands.h / .cpp | New | all `run(...)`-based actions, file choosers, history menu |
| plugin/src/EditorTheme.h | New | `laneColour`, the colour constants used by more than one panel (0xff15181d, 0xff20252d, 0xff6fb1ff, 0xffffc857, 0xffff6b6b, 0xff7cf29a, 0xff9aa4b2) |
| plugin/src/WavePanel.h/.cpp, PatternPanel.h/.cpp, HistoryPanel.h/.cpp, OrbitPanel.h/.cpp, ControlsPanel.h/.cpp, FeaturePanel.h/.cpp, StatusBar.h/.cpp | New | one pair per panel |
| plugin/CMakeLists.txt | Mod, **HOT** | introduce `set(CHOPFRACTAL_PLUGIN_SOURCES src/PluginProcessor.cpp src/PluginEditor.cpp src/Editor*.cpp src/*Panel.cpp src/StatusBar.cpp)` via `file(GLOB ... CONFIGURE_DEPENDS)` used by both targets; test target uses `file(GLOB CHOPFRACTAL_SHELL_TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)` so later features add test files without editing CMake |
| plugin/src/PluginProcessor.h/.cpp | Unmodified | **HOT**, deliberately untouched by F00 |
| plugin/tests/test_plugin_shell.cpp | Mod, **HOT** | helpers moved out; recursive lookup; no assertion changes |
| plugin/tests/shell_test_support.hpp | New | shared fixtures |
| plugin/tests/test_editor_layout.cpp, test_editor_model.cpp | New | section 7 |
| docs/BUILD_STATUS.md | Mod | note the new file layout under "Code audit" |

## 10. Risks and mitigations
- Pixel drift (fonts, clipping, opaque panels): panels are non-opaque except the four canvases, which fill their own rectangle exactly as before; the snapshot comparison catches drift. Fallback: set `setPaintingIsUnclipped(false)` defaults and re-check before changing any draw code.
- Label `attachToComponent` positions: labels become children of ControlsPanel, not the editor; verify slider labels at the same x/y via the bounds test (add `label` bounds to the layout test if drift is seen).
- Recursive test helpers could match a different button with the same text: texts are unique today; the test asserts `findButton` finds exactly one match (add a count check in the helper).
- Lost repaints because repaint scope shrank: the Orbit-only playing repaint and the StatusBar comparison are the only reductions; if QA sees a stale region, fall back to repainting all four canvases on every `View` change (never on idle ticks).
- Merge conflicts on the PR itself: land F00 first, alone; freeze edits to `PluginEditor.cpp` during the 5 days.

## 11. Implementation steps
1. Test support: create `shell_test_support.hpp`, recursive helpers, glob test sources; CI green on the old editor.
2. `EditorTheme.h` (move `laneColour` and colour constants); no behavior change.
3. `EditorModel`: move `refresh`, selection, seed, setup, status into it; editor delegates; add `test_editor_model.cpp` model tests.
4. `EditorCommands`: move `run`, all `do*` methods and button lambdas; editor buttons call it; add the command tests.
5. Extract `StatusBar`, then `OrbitPanel`, `HistoryPanel`, `PatternPanel`, `WavePanel` (one commit each, GUI test after each). Add `test_editor_layout.cpp` bounds test with the first panel and extend per commit.
6. Extract `ControlsPanel` (with attachments) and `FeaturePanel` (with the Evolve sync).
7. Final cleanup: delete dead members, enforce the acceptance greps, update `docs/BUILD_STATUS.md`, record the reference snapshot comparison in the PR.

## 12. As built (deviations from the first draft)
- Window is 940x912 (the editor had grown since the draft); the real panel bounds are in section 3 and are asserted by `test_editor_layout.cpp`.
- `EditorModel` also holds `Selection` change notifications (`selectSectionsInWave/InPattern` call `announceSelection`), `detectMode`, and the setup/seed/status state; `refresh` emits `Evolve` every tick, and `Setup`, `Selection` (if changed) and `View` on a rebuild.
- `EditorCommands::setEvolve(const EvolveUi&, evolve::Settings& pushed)` returns a bool and reports the settings pushed to the session; `FractalUi` carries `mutation` and `accent` as well as motif and depth.
- `OrbitPanel` paints with `g.setOrigin(-getPosition())` (editor coordinates) so anti-aliased float geometry stays pixel-identical; the other panels paint panel-locally.
- `FeaturePanel` owns presets, drag-out handles and the Evolve sync; `ControlsPanel` owns the slider and Length-combo attachments.
- Extra tests: `test_editor_panels.cpp` (MouseSim-driven history, orbit, pattern and wave gestures). `MouseSim` lives in `shell_test_support.hpp`.
- CMake uses `file(GLOB ... CONFIGURE_DEPENDS)` for plugin and shell-test sources; `CHOPFRACTAL_WARNING_SOURCES` carries the warning flags.
- Verified: snapshot byte-identical to the pre-split render; 0 idle repaints in 3 s.
