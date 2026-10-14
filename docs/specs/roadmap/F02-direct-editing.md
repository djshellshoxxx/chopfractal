# F02 Direct editing

**Status:** Ready for development  **Size:** L (9 engineer-days)  **Depends on:** F00 (panels), F01 (persisted snap prefs in `ProjectExtras::ui`)  **Blocks:** F04 (Delete/Ctrl+D/arrow keys call these commands), F07, F10

## 1. Summary and user value
Everything the engine can already edit (`pattern::moveEvent`, `addEvent`, `deleteEvent`, `setEventChop`, `setEventTransform`, `setEventFx`, `setLock`, `ChopMap::moveMarker/setEnabled/setLabel/setTrim`, `ProjectSession::editMarkers/undoMarkers/redoMarkers`) has no mouse access: the only editor gestures today are click-adds-marker, Alt-click-removes and select-a-hit. F02 adds drag, resize, move-between-lanes, duplicate, delete, mute, reset and effect editing of hits in PatternPanel and OrbitPanel, full marker editing and audition in WavePanel, and waveform zoom/scroll. Producers can shape a generated groove by hand instead of re-rolling the seed.

## 2. User stories and scope
- As a producer I can drag a hit to a new time and lane, stretch its end, and have it snap to the grid, so I fix one hit without regenerating.
- As a producer I can right-click a hit to mute, duplicate, delete, lock, reset or edit its level/pan/pitch/effects.
- As a producer I can drag a chop marker, have it snap to the nearest transient or zero crossing, disable it, trim a chop and undo marker edits.
- As a producer I can click the waveform to audition a chop and zoom into a region to place markers precisely.

**In scope:** top-level hits (`depth == 0`) in PatternPanel and OrbitPanel; WavePanel markers, zoom, scroll; three new pure `pattern_engine` edits; geometry/snap helpers in `plugin_ui_adapter`; the event editor popup; debounced history commit for manual edits.
**Non-goals:** editing zoomed (nested) hits (selectable, but all edit gestures refuse with a message); left-edge resize (move + right-edge resize cover it); multi-select and lasso; drag-copy between bars of different patterns; marker fades/groups UI (`setFades`, `setGroup`, `setColor` stay API-only); keyboard shortcuts (F04); rule editing (F06); changing any serialization format.

## 3. UX
All sizes in panel-local pixels. Modifier names: `Cmd` on macOS means Ctrl elsewhere (`juce::ModifierKeys::commandModifier`).

**PatternPanel (920 x 230)**
- Hit body: press selects (as today). Moving the pointer > 4 px starts a move drag. A ghost outline (white, 1 px, alpha 0.9) follows the snapped target; the original stays dimmed to alpha 0.35 until release. Horizontal = time, vertical = lane (`laneH = h / chops.size()`); crossing half a lane changes the chop. Release applies one edit. `Esc`-equivalent: dragging back to the start position within 2 px cancels.
- Right edge: the last 6 px of a hit (if the hit is wider than 18 px, else the last third) is a resize handle; cursor `LeftRightResizeCursor`. Dragging changes duration only. Minimum duration 60 ticks (1/64 note); maximum `barTicks - startInBar`.
- Alt held at press: move becomes duplicate (copy appears at the drop position, original stays).
- Double-click on a hit: opens the event editor popup (below). Double-click on empty lane space: adds a hit of one grid cell (`gridTicks(settings.grid)`) at the snapped position for that lane's chop. Empty pattern: unchanged message `Detect chops, then press Generate.`
- Right-click on a hit: popup menu, exact items in this order: `Edit hit...`, `Mute hit` / `Unmute hit`, `Duplicate hit`, `Lock hit` / `Unlock hit`, separator, `Reset effects and transform`, separator, `Delete hit`. Right-click on empty space: `Add hit here`.
- Overlay (top-right of the panel, 2 px from edges): `ToggleButton` "Snap" (title `Snap to grid`, 56 x 20, default on) and `ComboBox` (title `Snap division`, 100 x 20; items `Pattern grid`, `1/4`, `1/8`, `1/16`, `1/32`; default `Pattern grid`). Holding `Cmd` during a drag disables snap for that drag. Both persist through `ProjectExtras::ui.gridSnap/snapDivision`.
- Muted hits draw at alpha 0.25 with a dashed 1 px outline and the label `m` replacing the chop number (not colour alone).
- Messages (status line, exact): locked `That hit is locked. Unlock it or its bar first.`; nested `Zoomed hits cannot be edited here. Collapse the zoom first.`; no room `No room after this hit in its bar.`; cap `The pattern already has the maximum of 512 hits.`; resize dropped zoom `Zoom content of this hit was removed because its length changed.`

**Event editor popup** (`plugin/src/EventEditor.h/.cpp`, a `juce::CallOutBox` 260 x 250 anchored to the hit): sliders `Level` 0..4 step 0.01 (default 1.0; text `%`-free, two decimals), `Pan` -1..1 step 0.01, `Pitch (semitones)` -24..24 step 1, `Retrigger` 1..8 step 1; toggle `Reverse`; combo `Filter` (`Off`, `Low-pass`, `High-pass`); sliders `Cutoff` 0..1, `Resonance` 0..1, `Glide (semitones)` -24..24 step 1, `Crush` 0..1 step 0.01; button `Reset`. Filter-dependent sliders are disabled when Filter is Off. A change is applied when a drag ends, on text-box commit or on combo/toggle change (never per drag tick).

**OrbitPanel (190 x 190)**: dragging an arc moves it along time (angle to tick, snapped like PatternPanel) and radially to another ring (chop). Same ghost, same menu, same messages; no resize here.

**WavePanel (920 x 120)**
- Zoom: mouse wheel zooms around the pointer (`factor = 1.25^(wheel.deltaY * 4)` per event, clamped so the window is never narrower than 256 frames nor wider than the clip); `Shift`+wheel or horizontal wheel scrolls (`scrolledBy`). Overlay buttons top-right: `Fit` (44 x 20), `Undo Marker` (92), `Redo Marker` (92), 4 px gaps; the last two are enabled by `ProjectSession::canUndoMarkers()/canRedoMarkers()`.
- Click on empty waveform: selects nothing and auditions the chop under the pointer (`proc.audition`). This replaces click-adds-marker. Double-click on empty waveform: adds a marker at the snapped frame. `Alt`+click near a marker (8 px) still deletes it.
- Marker handle: 12 px wide hit zone centred on the line. Press selects it (line drawn 2 px, white label box), movement > 3 px starts a drag, release applies one `editMarkers(moveMarker)`. The drag is clamped to `[prev.position + minRegionFrames, next.position - minRegionFrames]` (`ChopMap::minRegionFrames()` = 64 by default) and cannot cross a neighbour. Snap (see `ProjectExtras::ui.markerSnap`): `Off`, `Transient` (default), `Zero crossing`; `Cmd` disables snap during the drag. A combo in the overlay is not added; the mode is in the right-click menu.
- Right-click on a marker: `Enable chop` / `Disable chop`, `Rename marker...` (AlertWindow `Marker name`), `Trim start here`, `Trim end here`, `Clear trim`, separator, `Snap: Off` / `Snap: Transient` / `Snap: Zero crossing` (radio ticks), separator, `Delete marker`. Right-click on empty waveform: `Add marker here`, `Zoom to fit`.
- Drawing: disabled markers grey as today; manual markers get a small filled square at the top, detected ones a triangle; labels (non-empty) replace the number; trimmed regions draw a darker overlay over the trimmed frames; the selected marker's chop region gets a 10 % white wash.
- Errors show the engine message in the status line; nothing is partially applied.

## 4. Data model and state
No new persisted types beyond F01's `ProjectExtras::ui` (`markerSnap`, `gridSnap`, `snapDivision`). Zoom window, selection, ghost and drag state are transient. Pattern format unchanged: mute uses the existing serialized `Event::enabled` flag (bit 0 of the event flags), duplicates and resizes use existing fields. New view fields (additive, `modules/plugin_ui_adapter/include/.../views.hpp`):
```cpp
struct EventRect { /* existing fields ... */
  std::int64_t startTick = 0;      // absolute, within the pattern
  std::int64_t durationTick = 0;
  std::int64_t startInBar = 0;
  bool enabled = true;             // false = muted; muted hits remain in the view
  bool topLevel = true;            // depth == 0
};
```
`buildPatternView` must include disabled events (today the view is built from the pattern tree, so it already lists them; the test pins this). `OrbitArc` gains `bool enabled = true`; `describe()` in `views.cpp` appends `, muted` to the accessible text when `!e.enabled`. `ChopMap`/`Marker` unchanged. Limits: markers <= `kMaxChops` 256; events <= `settings.maxEvents` (512).

## 5. Public API
**Portable `modules/pattern_engine` (`pattern.hpp`, `src/edit.cpp`)** — pure, each returns a new `Pattern`, refuses locked scopes with `ErrorCode::Blocked` exactly like `moveEvent`, sets `userOwned` on the event and its beat:
```cpp
Result<Pattern> setEventEnabled(const Pattern& p, EventId id, bool enabled);
Result<Pattern> resizeEvent(const Pattern& p, EventId id, Ticks newDuration);          // 60 <= d <= barTicks - start; drops child when d changes
Result<Pattern> duplicateEvent(const Pattern& p, EventId id, int bar, Ticks startInBar, EventId* newId = nullptr);
// copy keeps chop, region, tx, fx, probability, enabled; no child; locked=false; id = nextId++; duration clipped to barTicks - startInBar;
// LimitExceeded at maxEvents; Blocked if the destination beat is locked.
```
**Portable `modules/plugin_ui_adapter` (new `edit_geometry.hpp`, UI thread, pure)**:
```cpp
enum class SnapMode : std::uint8_t { Off, Transient, ZeroCrossing };
std::int64_t snapMarkerFrame(const float* const* channels, int numChannels, std::int64_t frames, std::int64_t frame, SnapMode mode, std::int64_t radiusFrames);
Ticks snapTickToGrid(Ticks tick, Grid grid, bool enabled);                     // nearest multiple of gridTicks(grid); ties round up; disabled = identity
struct BarPos { int bar = 0; Ticks inBar = 0; };
BarPos splitTick(Ticks absoluteTick, Ticks barTicks, int bars);                // clamps to [0, bars*barTicks-1]
struct HitMove { int bar = 0; Ticks startInBar = 0; int chopIndex = 0; bool changed = false; };
HitMove planHitMove(const PatternView&, const pattern::Settings&, const EventRect&, Ticks grabOffsetTicks, double pointerFraction, int pointerLane, int laneCount, bool snap, Grid grid);
Ticks planHitResize(const pattern::Settings&, const EventRect&, double pointerFraction, bool snap, Grid grid);   // returns new duration
// orbit.hpp additions
double angleOf(double x, double y);                                            // clockwise from 12 o'clock in [0, 2*pi); (0,0) -> 0
int ringAt(const OrbitView&, double x, double y, double radius);               // -1 outside the rings
Ticks angleToTick(double angle, Ticks lengthTicks);
```
`snapMarkerFrame` algorithm: `Transient`: over `[frame-r, frame+r]` (clamped to the clip) compute `d(i) = E(i, i+64) - E(i-64, i)` where `E` is the sum of squares of the channel average; return the `i` with the maximum `d` if `d > 0`, ties broken by smaller `|i-frame|` then smaller `i`, else `frame`. `ZeroCrossing`: nearest index to `frame` within `r` where the channel average changes sign (`a[i-1] <= 0 < a[i]` or the reverse), ties to the lower index, else `frame`. Deterministic, no allocation beyond locals; `r = clamp(12 px * framesPerPixel, 1, 2048)`.
**Composition (`project_session.hpp`, **HOT**)**: `bool canUndoMarkers() const; bool canRedoMarkers() const;` (delegate to `chopMap_->canUndo()/canRedo()`). No other session change: all pattern edits use `ProjectSession::edit(fn)` and marker edits `ProjectSession::editMarkers(fn)`.
**Plugin (`EditorCommands`, F00; message thread)**:
```cpp
void moveHit(cf::EventId id, int bar, cf::Ticks startInBar, int chopIndex);      // edit(): moveEvent then setEventChop if the lane changed
void resizeHit(cf::EventId id, cf::Ticks newDuration);                            // edit(): resizeEvent
void duplicateHit(cf::EventId id, int bar, cf::Ticks startInBar);                 // edit(): duplicateEvent; selects the copy
void addHit(int bar, cf::Ticks startInBar, int chopIndex);                        // edit(): addEvent(duration = gridTicks(grid))
void deleteHit(cf::EventId id);  void setHitEnabled(cf::EventId, bool);  void setHitLocked(cf::EventId, bool);   // setLock({ScopeLevel::Event, bar, beat, id}, v)
void resetHit(cf::EventId id);                                                    // setEventTransform(default) + setEventFx(default) in one edit
void setHitTransform(cf::EventId, const cf::EventTransform&);  void setHitFx(cf::EventId, const cf::EventFx&);
void moveMarker(cf::ChopId, std::int64_t frame);  void deleteMarker(cf::ChopId);  void setMarkerEnabled(cf::ChopId, bool);  void renameMarker(cf::ChopId, const std::string&);
void trimMarker(cf::ChopId, std::uint32_t trimStart, std::uint32_t trimEnd);      // ChopMap::setTrim; values in frames relative to the region
void undoMarkers();  void redoMarkers();
void noteManualEdit();  void flushManualEdit();                                    // debounced history commit (section 6)
```
Each pattern command is one `ProjectSession::edit` call: all pure edits for that gesture are composed inside one lambda so a gesture is exactly one undo step. `EditorModel` gains `selectedMarker()/selectMarker(ChopId)`.

## 6. Behavior details and edge cases
- **Undo/history:** each completed gesture = one `PatternSession` undo step (existing `Undo`/`Redo` buttons). `ProjectSession::edit` does not write the family tree, so `noteManualEdit()` arms a 1500 ms idle timer (15 ticks of the 10 Hz editor timer); when it fires, or before `generate/mutate/zoomIn/collapse/activate/applyFractal/keepEvolved`, `flushManualEdit()` calls `ProjectSession::commitEdit("Edited hits")` once. Undo cancels a pending commit. Result: a burst of drags leaves one tree node, not dozens (history cap 64).
- **Locks:** `EventRect.locked` is only the hit's own flag; the panel pre-checks `rect.locked || pattern.barLocked[rect.bar]` (`barLocked` already includes the phrase lock) and refuses before the drag starts (cursor `NotAllowedCursor`, message above). Beat locks and locked destination beats are not in the view: the engine returns `Blocked` and its text (`blocked: this beat` style from `edit.cpp`) is shown unchanged in the status line.
- **Ownership and generation:** edits set `userOwned`, so `Mutate` and Evolve skip them (existing rule); `Generate` replaces everything (existing). Evolve running: edits are allowed; a step that conflicts is the engine's problem (steps are atomic). `hasPendingActivation()`: edits apply to the current pattern; the pending switch still replaces it at the boundary (status unchanged).
- **Roles/rules:** hand edits bypass the grammar by design (same as `addEvent` today). Moving to another lane changes the chop via `setEventChop` (clears region and child, per the engine).
- **Zoomed parents:** `moveEvent` clips the duration at the bar end and drops the child (existing); `resizeEvent` does the same and the UI emits the `Zoom content ... removed` notice only when `e.child` existed.
- **Marker transactions:** `editMarkers` is transactional (map, chops, roles restored on failure) and reconciles the pattern (`sanitize`); a marker move that changes a chop range keeps every pattern event valid (existing test `marker_edits_reconcile_the_pattern_and_stay_valid`). Disabling a marker keeps its role (existing). Trim validation for disabled markers is a known gap in BUILD_STATUS; `Trim` items are hidden for disabled markers.
- **Audition:** `proc.audition(chopId)` (existing) on single click; no pattern playhead interaction. Clicking repeatedly is bounded by the renderer's preview queue (returns false when full; ignored).
- **Zoom state:** `ViewWindow{start,end,widthPx}`; every zoom/scroll goes through `ui::zoomAround` / `ui::scrolledBy` (never accumulated manually). Peaks recompute for the window and `widthPx` and are cached by `(sourcePtr, start, end, width)`; markers use `buildMarkerViews(map, window)` with the same window. Window resets to the full clip when the source changes.
- **Determinism:** no edit touches `Settings::seed`, the RNG or any generation path. New functions are pure and covered by round trip through `serialize`/`deserialize`. Pattern_engine golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 unchanged (no serialization change).
- **Real-time:** all of this is message thread; the only audio-thread-visible effect is the validated `Playback` published through the Mailbox by `installPlayback`.

## 7. Test plan
**Unit, pattern_engine (`test_pattern_engine.cpp`)**
- `set_event_enabled_removes_the_hit_from_flatten_and_survives_serialize`: mute -> `flatten` has one fewer event; `deserialize(serialize(p))` keeps `enabled=false`; unmute restores the original flatten list.
- `resize_event_enforces_bounds_and_drops_the_child`: d=59 -> OutOfRange; d=barT-start ok; d=barT-start+1 -> OutOfRange; event with child resized -> `child==nullptr`, `childActive==true`.
- `duplicate_event_copies_transform_and_fx_with_a_fresh_id`: ids unique, `nextId` advanced, `userOwned`, no child, locked false; at `maxEvents` -> `LimitExceeded`.
- `new_edits_refuse_locked_scopes`: locked event/beat/bar/phrase -> `ErrorCode::Blocked` for all three functions and the destination-lock case of `duplicateEvent`.
**Unit, plugin_ui_adapter (`test_views.cpp`, new `test_edit_geometry.cpp`)**
- `pattern_view_exposes_ticks_enabled_and_top_level`: startTick/durationTick match `flatten`; muted hit present with `enabled=false`; nested rect `topLevel=false`.
- `snap_tick_to_grid_rounds_to_nearest_and_ties_up` (grid 1/16: 479->480, 240 -> 240 tie rule, disabled identity); `split_tick_clamps`; `plan_hit_move_changes_lane_after_half_a_lane`; `plan_hit_resize_respects_min_and_bar_end`.
- `snap_marker_frame_finds_the_transient_and_zero_crossing`: synthetic click at frame 12000, query at 11950 radius 100 -> 12000 (Transient); sine, query 1003 -> nearest sign change (ZeroCrossing); radius 0..1 and empty clip return the input; repeated calls identical.
- `orbit_angle_helpers_invert_each_other`: `angleOf(x,y)` for the four axes = 0, pi/2, pi, 3pi/2; `ringAt` equals the ring `hitTest` reports for arc midpoints; `angleToTick(playheadAngle(q, len), len)` within 1 tick of `q*960`.
**Integration, composition (`test_features.cpp`/new `test_editing.cpp`)**
- `each_edit_gesture_is_one_undo_step`: for move/resize/duplicate/delete/mute/reset: one `undo()` restores `serialize` bytes of the previous pattern; `redo()` reapplies.
- `manual_edits_survive_mutate_and_are_replaced_by_generate`; `flush_commits_one_history_node_for_a_burst_of_edits` (5 edits + flush -> `history().size()` +1).
- `marker_drag_clamps_and_undo_redo_round_trip`: move marker 1 past marker 2 -> engine error, map unchanged; valid move -> pattern still valid; `undoMarkers()` restores positions; `canRedoMarkers()` true.
**GUI (xvfb; `plugin/tests/test_editing_gui.cpp`; helper `makeMouse(Component&, Point<float>, ModifierKeys, bool dragged, int clicks)` builds a `juce::MouseEvent` with `Desktop::getInstance().getMainMouseSource()`; helpers `mouseDown/Drag/Up(panel, p, mods)` call the panel's virtual handlers)**
- `pattern_drag_moves_a_hit_one_grid_cell`: session with drum loop, Detect, Generate seed 42 (2 bars), then `edit(addEvent(bar0, chop0, start 0, dur 480))`; read the new rect from `view().pattern`; PatternPanel local: press at its centre, drag +`gridTicks/lengthTicks*920` px, release; assert event `start == 240`, `userOwned`, one `undo()` returns the previous bytes.
- `pattern_drag_to_another_lane_changes_the_chop`; `pattern_right_edge_resizes`; `alt_drag_duplicates` (event count +1); `locked_hit_refuses_drag` (lock via `setHitLocked`, drag, bytes unchanged, status `That hit is locked. Unlock it or its bar first.`; repeat with `Lock Bar` on its bar); `double_click_empty_lane_adds_a_hit`.
- `context_menu_items_exist_in_order`: invoke `PatternPanel::buildContextMenu(rect)` (public for tests) and compare item texts to section 3.
- `wave_double_click_adds_marker_and_single_click_auditions`: marker count +1 on double-click at x=300; single click does not change markers (`chopMap()->markers().size()` same) and requests a preview (renderer preview queue accepted; assert via `proc.audition` call counter test hook).
- `wave_marker_drag_moves_and_clamps`, `wave_wheel_zoom_keeps_anchor_frame_under_pointer` (anchor frame before/after within 1 px worth), `wave_alt_click_deletes_marker`, `undo_marker_button_restores`.
- Existing `editor_buttons_drive_the_session` unchanged and green.
**Sanitizers:** ASan/UBSan on all new tests (menus use `SafePointer`). **Manual QA:** (1) Load a loop, Detect, Generate; drag three hits, resize one, duplicate with Alt, mute one, delete one; press Undo five times: pattern returns to generated state. (2) Lock bar 1 via Lock Bar; try dragging a bar-1 hit: refused with the message. (3) Zoom to ~1 s of the loop, drag a marker onto a transient: it snaps; hold Cmd: no snap. (4) Right-click a marker: Disable chop; the lane for that chop disappears from PatternPanel after reconcile; `Undo Marker` brings it back. (5) Start Evolve, drag a hit, wait two loops: the dragged hit is preserved. (6) Open the family tree 3 s after a burst of edits: exactly one `Edited hits` node.

## 8. Acceptance criteria
- [ ] Every gesture in section 3 works and produces exactly one undo step (`each_edit_gesture_is_one_undo_step`).
- [ ] A burst of N edits within 1.5 s produces one history node labelled `Edited hits`.
- [ ] Locked hits/bars cannot be changed by any gesture; the status text matches exactly.
- [ ] Snap: with grid snap on, 100 % of moved/resized/added hits land on multiples of `gridTicks(grid)` (property test over 200 random pointer positions).
- [ ] `snapMarkerFrame` never moves a marker more than `radiusFrames` and is deterministic across 1000 calls.
- [ ] A dragged marker can never cross a neighbour or leave a region shorter than `minRegionFrames()` (property test).
- [ ] Wheel zoom keeps the frame under the pointer within 1 pixel of its original x for 50 zoom steps (uses `ui::zoomAround`; no drift).
- [ ] Muted hits are inaudible (`flatten` excludes them) and remain visible with the `m` label.
- [ ] Pattern_engine golden hashes unchanged; `pattern::kSchemaVersion` unchanged; old projects load identically.
- [ ] No allocation or lock added on the audio thread (diff of `processBlock`/`Renderer::process` is empty).
- [ ] `docs/BUILD_STATUS.md` known gap "no marker dragging, snap controls" removed.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| modules/pattern_engine/include/.../pattern.hpp, src/edit.cpp | Mod | three new edits (**not** the serializer) |
| modules/pattern_engine/tests/test_pattern_engine.cpp | Mod | new tests; golden test untouched |
| modules/plugin_ui_adapter/include/.../views.hpp, orbit.hpp, src/views.cpp, src/orbit.cpp | Mod | new fields and helpers |
| modules/plugin_ui_adapter/include/.../edit_geometry.hpp, src/edit_geometry.cpp, tests/test_edit_geometry.cpp | New | snap and geometry |
| composition/include/.../project_session.hpp, composition/src/project_session.cpp | Mod, **HOT** | `canUndoMarkers/canRedoMarkers` only |
| composition/tests/test_editing.cpp | New | integration tests |
| plugin/src/WavePanel.*, PatternPanel.*, OrbitPanel.* | Mod | gestures, menus, overlays, zoom |
| plugin/src/EventEditor.h/.cpp | New | popup |
| plugin/src/EditorCommands.*, EditorModel.* | Mod, **HOT** (shared with F03/F04/F05) | commands, marker selection, manual-edit timer |
| plugin/src/PluginEditor.cpp | Mod, **HOT** | timer calls `commands.tickManualEdit()` only |
| plugin/tests/test_editing_gui.cpp, shell_test_support.hpp | New / Mod | mouse helpers |
| docs/BUILD_STATUS.md | Mod | known gaps |

## 10. Risks and mitigations
- Hit-testing precision on 920 px with 8 bars (115 px/bar): 1/32 hits are ~7 px wide; the 6 px resize handle would swallow the body, hence the "last third" rule; unit-test `planHitResize` at 1/32 and enforce a minimum 4 px body.
- Synthetic mouse events in tests differ from OS events: keep logic in panel methods taking `(Point, ModifierKeys)` and have `mouseDown/Drag/Up` forward to them; the unit tests hit those methods directly, the GUI tests prove the wiring.
- History flooding: the debounce timer; fallback if the tree still floods is to disable `commitEdit` for gestures and rely on `Keep`.
- `setTrim` validation gap for disabled markers: hide the items; add the missing validation to `ChopMap` only if QA finds a repro (separate fix).
- Click semantics change (click no longer adds a marker): called out in release notes; double-click adds, right-click menu `Add marker here` is the discoverable path.

## 11. Implementation steps
1. pattern_engine: `setEventEnabled`, `resizeEvent`, `duplicateEvent` + tests (hot-file free, mergeable immediately).
2. plugin_ui_adapter: view fields, `edit_geometry`, orbit helpers + tests; session `canUndoMarkers/canRedoMarkers`.
3. EditorCommands pattern/marker commands + manual-edit debounce + composition tests.
4. PatternPanel gestures, overlay, menu, EventEditor + GUI tests.
5. OrbitPanel drag + shared menu.
6. WavePanel zoom/scroll, marker drag/snap/menu, audition, overlay buttons + GUI tests.
7. Docs and manual QA pass.
