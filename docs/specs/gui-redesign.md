# GUI redesign (from scratch)

**Status:** Draft for review. Replaces `gui-and-interaction.md` as the GUI source of truth once step S0 lands; F04, F05, F06, F19 keep their behavior specs but their layout sections defer to this file.
**Scope:** editor only (`plugin/src`). No change to modules, parameter IDs, state format or golden pattern hashes, except the optional step S11 (three new macro weights) which is a separate engine spec.
**Date:** 2026-10-21

## 1. Research summary

Sources are listed in section 20. Where a page could not be fetched, the point comes from product knowledge and is marked (known).

| Product | What it does well | What we take | What we avoid |
|---|---|---|---|
| Serato Sample | Big DJ-style waveform, 16 pads under it, auto slicer lands slices on pads, drag a file in and it analyses tempo and key | Waveform as the hero, chops as visible pads/lanes, drop-to-load with instant analysis | Auto find placing cues on look-alike sounds with no way to see why; keyboard audition that cannot be recorded |
| Ableton Simpler Slice | Auto transient markers, drag markers, zoom by drag, each slice becomes a playable note | Marker drag with local zoom, immediate audition, one slice = one lane | No generated rhythm at all; the user programs timing by hand |
| Ableton Beat Repeat (known) | One screen, few big controls (Interval, Gate, Chance, Variation) with a probability feel | Few macros with plain names; Chance and Variation as first-class | Hidden grid/offset semantics |
| NI Maschine / Battery / Kontakt (known) | Pads as the mental model, per-pad strip with the same small set of controls, color per group | Per-lane header with the same controls on every lane; selection drives one inspector | Deep tabbed pages for per-pad settings |
| iZotope Stutter Edit 2 | Gestures behave like tempo-synced presets mapped to keys, Auto mode removes MIDI routing, gesture panel has few named controls (Length, Release, Grid, Freeze) | Performance-first controls, a "play it now" path with no routing, short named controls | Many hidden modifier curves exposed by default |
| Plasmonic BreakTweaker (known) | Per-hit sequencer lane with per-step modulation editing | Lane rows plus a per-hit lower editor | Dense, small-text multi-page layout |
| Output Portal / Movement | One large macro surface (few knobs), live visual feedback of what the effect does, presets as the entry point | A big, labelled macro strip; visible motion that explains audio | Pure black-box macros with no way to see the result |
| Sugar Bytes Effectrix | Colour-coded effect lanes down the side of a step grid, per-lane dry/wet, modulation lane under the grid, 12 sequences per preset, randomise at several levels | Lane colour + grid layout is the right shape for our pattern view; layered randomise | Several preset types that confuse; global randomiser that cannot be scoped or locked |
| Cableguys ShaperBox / TimeShaper | Pen/curve tools, shift and double curves, host-synced grid, three quick presets per shaper, cloud browser | Direct manipulation on the graph itself; first-run presets inline | Presets that cannot be searched by intent |
| Xfer Serum / OTT / LFOTool (known) | One display that shows what the processing does; very few controls on OTT | Always show the result graphically; OTT-style "few knobs plus a Mix" macro philosophy | Serum-depth menus in the main view |
| Arturia Pigments, Vital (known) | Drag-source-to-target modulation, ring around the knob shows modulation amount, progressive disclosure through tabs, resizable with scale | Modulation/macro rings on knobs; drag-to-assign; scalable UI | Too many parallel tabs |
| Slate Digital, Soundtoys (known) | Few controls, strong hardware metaphor, big readable knobs, shared style across plugins | A single consistent knob and button family; value readout on hover and drag | Skeuomorphic detail that costs paint time |
| NI Replika / Supercharger (known) | One big mode/selector, a small number of large knobs, clear on/off | Big power buttons, clear mode chips | None |
| ReaSamplOmatic5000, Tracktion/Bitwig Sampler (known) | Everything exposed for power users; slice view with note mapping | Advanced mode can expose depth | Default view showing all of it |

Synthesis:
1. Every best-in-class tool has one hero display (waveform, pad grid, gesture graph) and 6 to 10 large controls. Depth lives behind a selection, a drawer or an Advanced switch.
2. The product that explains audio with moving visuals (Portal, ShaperBox, Serum) is the one users call "intuitive". Playhead, hit flashes and a macro ring are not decoration.
3. Randomise tools get criticised when they cannot be scoped or locked (Effectrix). We already have locks, seed and a family tree; the UI must make them obvious.
4. Presets are the entry point for most users. The preset browser must be one click from the top bar.

## 2. Diagnosis of the current UI

Measured from `plugin/src/*Panel.cpp`, `EditorTheme.h`, `PluginEditor.cpp` (HEAD e12625c).

1. **Fixed 940 x 912 window, `setResizable(false,false)`.** Taller than most laptop screens (a DAW on 1080p leaves about 900 px). No scale option. This is the single biggest usability defect.
2. **No hierarchy.** About 45 controls are visible at once: transport, 10 buttons, seed row, 5 sliders, 7 toggles, 4 rows of Feature buttons (Smart Setup, Accept Roles, Use Tempo, Fractal, Keep, Export WAV, Export Kit, Locate Source, Store/Recall A/B, Set Role, Reroll, Save Preset, Delete, Evolve, Motif, Depth, Every, Format, Rate, Loops, drag handles). Every control has the same weight. Generate, the one primary action, looks like Mutate and Undo.
3. **Controls are 5 horizontal sliders with 64 px text boxes.** Low data-ink, small hit area, no visual link to what they change. Parameters that matter most (Variation, Density, Swing) are not more prominent than Output gain.
4. **Waveform is 120 px high, one flat blue, 1 px columns, no zoom, no playhead.** Chops are shown only as amber marker lines with a number. A chop is a region, not a line; no region fill, no role badge, no per-chop colour. Playhead is not drawn here at all.
5. **Playhead exists only in the Orbit view**, which is a secondary visualisation at the bottom. The pattern grid has no playhead. `timerCallback` runs at 10 Hz and only repaints Orbit; a smooth playhead needs 60 Hz while playing.
6. **Pattern grid has no headers.** Lanes are anonymous rows (number painted inside hits). No lane mute/solo/lock, no chop name, no waveform thumbnail, no bar ruler labels, no beat labels. Nested hits are drawn by shrinking by 3 px per depth, unreadable beyond depth 2.
7. **Colour semantics are overloaded.** `laneColour` = HSV hue steps at fixed S/V: neighbouring hues (1 and 8, 2 and 3) are hard to tell apart and fail for red-green colour blindness. Red means both "locked" and "Stop" and "accentRed" error. Blue is waveform, selection and rubber band at once. Amber is marker and nothing else. Green is Play only.
8. **Theme is five hardcoded colours** with `juce::Colours::grey/white` mixed in. No tokens, no `LookAndFeel`: buttons, sliders, combo boxes, tooltips are stock JUCE Default (LookAndFeel_V4) with a few `setColour` calls. Looks like a prototype, no brand identity (no Circuit Drift Labs presence).
9. **Text.** Fonts 10 to 13 px fixed, 10 px 50% white version label (contrast about 3:1 on `0xff15181d`, fails 4.5:1). Labels use `attachToComponent(..., true)` to the left, so they collide at other widths.
10. **Mode confusion.** "Move / resize hits" toggle changes what a drag means on the grid; the state is a small checkbox. Drag-select vs drag-edit should be modeless (move by body drag, resize by edge, rubber band on empty space).
11. **Discoverability by tooltip only.** Tooltips are static text, long; one area tooltip hides per-control ones. No command palette yet (F19), no context menus on the grid (F02 partial), keyboard map partial (F04).
12. **History tree and Orbit share the bottom strip at equal weight** though they are secondary; the tree is 30 px rows with 13 px text.
13. **Empty and error states are one grey sentence.** No first-run guidance, no drop-zone affordance.
14. **Accessibility.** Painted panels expose no `AccessibilityHandler`; focus order is implicit; focus ring is JUCE default. Contrast of grey-on-dark text is under WCAG for small sizes.
15. **Performance risks.** Waveform repainted column by column with `drawVerticalLine` (n calls); pattern grid does a linear `std::find` per rect for selection (O(n*m)); whole panel repaints on any `Change::View`. Fine now, poor at 2x or with deep nesting.

## 3. Principles

1. **One main view.** Source on top, pattern below, macros at the bottom. Nothing else is needed to make sound.
2. **One primary action.** Generate is the biggest control on screen. Reroll and Evolve sit beside it. Everything else is smaller.
3. **Show, then ask.** Visuals explain behavior (playhead, hit flash, macro rings); numbers appear on hover or drag.
4. **Progressive disclosure.** Simple mode shows the main view only. Advanced adds lane headers' extra controls, the rack, nested hit zoom, rules and fractal settings. Drawers open on demand and never cover the main view's controls.
5. **Direct manipulation.** Drag, drop, double-click, right-click do what the user expects, modelessly.
6. **Everything undoable.** Every UI edit goes through `EditorCommands` and the existing undo stack; macro drags coalesce into one entry.
7. **Colour carries role, never meaning alone.** Every state also has a shape, label or glyph.
8. **Quiet when idle.** No repaint while stopped and nothing hovered; no looping animation.
9. **Keyboard complete.** Every action has a shortcut or a palette entry; focus order follows the layout.
10. **Fits every window.** 880 x 560 up to 2x without clipping.
11. **Brand without cost.** Circuit Drift Labs look through tokens and a LookAndFeel, painted with flat fills and strokes only (no per-frame gradients, blurs or images).

## 4. Information architecture

```
Main view (always visible)
  Top bar        brand, preset name + browse, transport, undo/redo, Simple/Advanced, palette, settings
  Source panel   waveform + chop lanes + toolbar (Detect, mode, sensitivity, zoom)
  Pattern panel  ruler + per-lane step grid (Grid view; Orbit view in Advanced)
  Macro strip    Generate / Reroll / Evolve cluster + 8 knobs
  Status bar     message line, meters, host sync, seed
Drawers (right, docked, one at a time)
  Inspector      Hit tab, FX rack tab (selection driven)
  History        family tree (variation_history), compare, favorites
Sheets (modal, centered)
  Preset browser, Rule editor (F06), Export, Settings, Quick start (first run), Command palette (overlay)
```

Mapping of today's controls:

| Today | New home |
|---|---|
| Load Loop, Detect Chops, detect mode, Locate Source, Smart Setup, Accept Roles, Use Tempo, Set Role | Source panel toolbar (Advanced: roles, tempo) |
| Play, Stop, Follow host | Top bar transport |
| Generate, Mutate, Reroll, Dice/Seed | Macro strip cluster; Seed in status bar and Advanced cluster |
| Density, Swing, Variation, Chaos, FX intensity | Macro strip knobs (Tape/Filter takes FX intensity) |
| Dry mix | Macro strip "Mix"; Output gain moves to status-bar meter fader |
| Allow reverse/pitch/retrigger/filter/glide/crunch | Advanced: "Allowed" chip row in the Pattern header; Inspector FX rack shows per-hit values |
| Pattern length | Pattern header dropdown (1/2/4/8 bars) |
| Undo, Redo | Top bar |
| Zoom, Collapse, Lock | Pattern header (Advanced) and context menu; shortcuts Z, Shift+Z, L |
| Edit mode toggle | Removed (modeless editing) |
| Store/Recall A/B | Top bar A/B pair (Advanced), F10 |
| Fractal, Keep, Evolve, Motif, Depth, Every | Rule editor sheet (F06) plus Evolve button/popover in the macro cluster |
| Export WAV/Kit, Format, Rate, Loops, drag handles | Export sheet; drag handles stay on the top bar as MIDI and WAV chips |
| Preset combo, Save, Delete | Preset browser sheet; top bar shows name, prev/next |
| History tree | History drawer (key H) |
| Orbit | Pattern panel view switch Grid / Orbit (Advanced) |
| Status log | Status bar line, click to open log popover |

## 5. Simple vs Advanced mode

Persisted per user in `ProjectExtras::ui` (F01) and in the per-user file (F19) as default for new projects. Toggle in the top bar (segmented control `Simple | Advanced`, key Ctrl/Cmd+Shift+A).

| Element | Simple | Advanced |
|---|---|---|
| Source toolbar | Load, Detect, Zoom | + mode, sensitivity, roles, Smart Setup, Use Tempo |
| Chop lanes | color region, number | + role badge, per-chop gain handle, enable toggle |
| Pattern lane header | number, name, mute | + solo, lock, lane gain, source swap |
| Pattern header | length, Grid | + Orbit, Zoom, Collapse, Allowed chips, Move snap |
| Macro knobs | all 8 (or 6 until S11 lands) | same, plus fine ring for per-level variation on right-click |
| Drawers | Inspector (Hit tab) | Inspector (+FX rack), History |
| Rules, Fractal, Evolve scenes | hidden (Evolve button visible) | Rule editor, Evolve popover |
| Status bar | message, meter, sync | + seed field, voices |

Rule: Simple never hides a control needed for the Quick Start path (load, detect, generate, play, export). Switching mode never changes audio state.

## 6. Layout grid

Units are logical pixels at scale 1.0. Default 1100 x 720, minimum 880 x 560, maximum 2200 x 1440 (2x). Outer margin 12, gap between panels 8, panel corner radius 6.

### 6.1 Default 1100 x 720 (Drawer closed)

| Region | x | y | w | h | Notes |
|---|---|---|---|---|---|
| Top bar | 0 | 0 | 1100 | 44 | full width, bottom 1 px line |
| Source panel | 12 | 52 | 1076 | 208 | toolbar 28, ruler 16, waveform 120, chop strip 36, gaps |
| Pattern panel | 12 | 268 | 1076 | 240 | header 28, bar ruler 18, grid 194 (7 lanes of 26 visible, scroll beyond) |
| Macro strip | 12 | 516 | 1076 | 168 | cluster 232 wide, gap 12, 8 cells of 104 |
| Status bar | 0 | 692 | 1100 | 28 | |

Source panel interior (inner width 1076 minus 2 x 8 padding = 1060): toolbar y+0 to 28; ruler y+30 to 46; waveform y+46 to 166 (120 px; peak area 112 plus 4 px top and bottom); chop strip y+168 to 204 (36 px of coloured regions with number and role badge).

Pattern panel interior: lane header column 148 wide (swatch 6, number 20, name flexible, M/S/L icons 3 x 20 in Advanced; Simple shows swatch, number, name, M); grid starts at x = 12 + 148 = 160.

Macro strip: cluster x 12 to 244: Generate button 232 x 64 (y+12), below it Reroll 112 x 40 and Evolve 112 x 40 (y+84) with 8 gap. Knob cells x = 256 + i * 104 (i = 0..7), each 104 x 144: knob diameter 72 centred at y+12, name label 14 px at y+92 (13 px text), value readout at y+110 (12 px mono), mod/ring legend none. Cell hit area is the whole 104 x 144 rectangle.

### 6.2 Drawer open (Inspector or History)

Drawer 320 wide docked on the right between top bar and status bar. Source and pattern panels shrink to 748 inner width; macro strip stays full width (knobs never move). Drawer x = 768 to 1088, y = 52 to 684 (632 tall). At the minimum width the drawer is 280.

### 6.3 Minimum 880 x 560

| Region | y | h |
|---|---|---|
| Top bar | 0 | 40 |
| Source | 48 | 150 (toolbar 24, ruler 14, wave 80, chop strip 28) |
| Pattern | 206 | 170 (header 24, ruler 16, grid 126: 5 lanes of 24, scroll) |
| Macro strip | 384 | 128 (knob 52, label, value; cluster 176 wide, cell 84) |
| Status | 536 | 24 |
| Slack | 520 to 536 | 16 px distributed to the two big panels |

At 880 wide: 8 cells of 84 = 672, cluster 176, gaps 12, margins 12 gives 12 + 176 + 12 + 672 = 872 (8 px slack). Lane header column 120. Drawer 280 (grid inner width 880 - 24 - 280 - 8 = 568). Below 960 wide the Simple/Advanced control collapses to an icon, preset name truncates, transport keeps Play, Stop.

### 6.4 Flexible rules (any size between min and max)

- Fixed heights (scaled by `s`): top bar 44s, status 28s, macro strip 168s with floor 128 (knob diameter 72s, floor 52).
- Remaining height H is split Source : Pattern = 44 : 56 with floors (Source 150, Pattern 170 at min). When a lane count exceeds visible rows the grid scrolls vertically (wheel, drag scrollbar 10 px, auto-scroll to keep selected lane visible).
- Widths: panels take the full width minus margins and the drawer. Knob cells share width equally: `cell = (W - 24 - cluster - 12) / 8`, clamped to [84s, 130s]; surplus width is added to the panel margins left and right (centred strip).
- Scale `s = clamp(min(W / 1100, H / 720), 0.8, 2.0)` applies to fixed metrics (row heights, fonts, knob diameters, icon sizes, stroke widths). Panels do not use an `AffineTransform`; text is rendered at true size so it stays crisp, hit testing stays exact.
- The window uses `setResizable(true, false)` with `ComponentBoundsConstrainer::setMinimumSize(880, 560)`, `setMaximumSize(2200, 1440)`, no fixed aspect ratio (forum reports of aspect-lock jumping on macOS); corner resizer visible. Size saved in `ProjectExtras::ui` and clamped on restore.
- Interface scale in Settings: 100, 125, 150, 200 percent (multiplies `s`; also sets window size to keep layout proportions) in addition to the host/OS DPI scale `Desktop::getGlobalScaleFactor` / `getScaleFactor` which JUCE applies to painting automatically. The two compose; stored per user, not per project.
- Layout code lives in one pure function `computeShellLayout(Size, scale, drawerState, mode) -> ShellRects` (no JUCE components) so it is unit-testable.

## 7. Screens and wireframes

### 7.1 Main view, Simple, Inspector closed (1100 x 720)

```
+--------------------------------------------------------------------------------------------------+
| (CDL) ChopFractal  [<] Dusty 90s Break  [>] [Browse]  [Play][Stop] 92.0 BPM  [Undo][Redo]  Simple|Adv  [?][gear] | 44
+--------------------------------------------------------------------------------------------------+
| SOURCE  break_92.wav  4 bars   [Load] [Detect: Transients v] [Zoom -][+]                          | 28
| |1        |2        |3        |4                                                                  | ruler 16
| ,-.  _ .||.__,.-._|,,.|__.--.||._,_  |.-.__ ,|,._  ... waveform (cyan, mirrored peaks) ......   | 120
| |[1 K ]|[2 S ]|[3 H ]|[4 H ]|[5 K ]|[6 S    ]|[7 H ]|[8 K ]|    chop strip, one colour each      | 36
+--------------------------------------------------------------------------------------------------+
| PATTERN   Length [2 bars v]   [Grid]                                                              | 28
|            |1.1   1.2   1.3   1.4 |2.1   1.2 ...                                                  | ruler 18
| 1 K [M]    |###        ###       |###     ###                                                     | 26
| 2 S [M]    |      ###        ###  |      ###     ###                                             |
| 3 H [M]    |# # # # # # # # # # # |# # #  # # #                                                    |
| ...        |                  ^playhead (full height line + triangle)                              |
+--------------------------------------------------------------------------------------------------+
| [ GENERATE ]  (Var)  (Dens) (Swing) (Stut) (Glitch) (Tape) (Chaos) (Mix)                          | 168
| [Reroll][Evolve]  o      o      o      o      o       o      o       o                             |
|                  Variation Density Swing Stutter Glitch Tape/Filt Chaos Mix                         |
+--------------------------------------------------------------------------------------------------+
| Generated pattern, seed 481516.            IN ||||  OUT |||| -6 dB   Host sync 92.0   Seed 481516 | 28
+--------------------------------------------------------------------------------------------------+
```

### 7.2 Main view, Advanced, Inspector open

```
+------------------------------------------------------------+-----------------------------------+
| top bar (Advanced: A/B, Allowed chips menu)                | INSPECTOR  [Hit][FX]   [x]        |
+------------------------------------------------------------+-----------------------------------+
| SOURCE toolbar + Roles [Smart Setup][Use Tempo][Sens ---o-]| Hit 3.2  (chop 2 Snare)           |
| waveform with chop regions, role badges, gain handles      |  Source   [2 Snare v]  [Audition] |
| chop strip                                                 |  Start    1.3.1   Len 1/8         |
+------------------------------------------------------------+  Prob     (o) 80 %  Locked [ ]    |
| PATTERN  [Grid|Orbit] Len[2v] [Zoom][Collapse][Lock bar]   |  Level -2 dB  Pan  C              |
|  Allowed: [Rev][Pitch][Retrig][Filt][Glide][Crunch]        |  Pitch +0  [Reverse]  Retrig 1    |
| 1 K  M S L |###     ###   |                                |  [Mute][Duplicate][Delete]        |
| ...                                                        |                                   |
+------------------------------------------------------------+-----------------------------------+
| macro strip (full width, unchanged)                                                                |
+--------------------------------------------------------------------------------------------------+
```

### 7.3 Inspector, FX rack tab (per-hit effects)

```
+-------------------------------------+
| INSPECTOR  [Hit][FX]            [x] |
| Editing 3 hits (mixed values shown   |
| as "mixed")                          |
| +-- Filter ---------------[ON]----+ |
| | Type [LP|HP]  Cutoff (o)  Res (o) | |
| +-- Tape glide -----------[ON]----+ |
| | Glide  -12 ... +12 st  (o)        | |
| +-- Crunch ---------------[ON]----+ |
| | Crush (o)                         | |
| +-- Retrigger ------------[ON]----+ |
| | Count 1..N  (o)                   | |
| +-- Level / Pan / Pitch / Reverse -+ |
| [Copy FX] [Paste FX] [Reset FX]      |
+-------------------------------------+
```

Modules map one to one to `EventFx` (filter, cutoff, resonance, glideSemitones, crush) and `EventTransform` (level, pan, pitchSemitones, reverse, retrigger). A module's power button sets its values to default (off) and remembers the last value in the UI for one toggle-back (UI-only, not state). Multi-selection edits apply the delta to all selected hits; mixed values show a dash and the knob is a "ghost" until touched.

### 7.4 History drawer (family tree)

```
+-----------------------------------+
| HISTORY   [Compare][Fav only] [x] |
|  o--o--o--o--*   (current ring)   |
|     \--o--o      favourite star   |
|         \--o                      |
| Node 12  seed 8812  Generate      |
| 12 hits, density 0.62             |
| [Recall] [Favorite] [Rename] [Del] |
+-----------------------------------+
```
Nodes 14 px circles on a 36 px grid, edges 1.5 px; node label shows kind glyph (G generate, M mutate, Z zoom, E edit). Current node has a 2 px ring in the accent. Compare shows two pattern thumbnails side by side (reuse `compare`). Clicking recalls at next loop boundary during playback, as today.

### 7.5 Rule editor sheet (F06)

```
+------------------------------------------------------------------------------+
| RULES   [Preset: Breakbeat v] [Save]                                  [Close] |
| +-- Rules (list, drag to reorder) --+  +-- Selected rule -------------------+ |
| | [x] Kick on beat 1               |  |  When  [Bar v] [every 2 v]          | |
| | [x] Snare backbeat               |  |  Place [Chop role: Snare v]         | |
| | [ ] Hat ghost notes              |  |  Chance 70%  Swing follow [x]       | |
| | [+ Add rule]                     |  |  Lock after generate [ ]            | |
| +----------------------------------+  +-------------------------------------+ |
| Preview strip: 2-bar pattern thumbnail updates on every edit    [Apply] [Cancel]|
+------------------------------------------------------------------------------+
```
Layout, fields and persistence come from F06; this file fixes chrome only: sheet 760 x 520 (min: width - 40), two columns 280 / rest, live preview strip 64 px high, Apply and Cancel at bottom right.

### 7.6 Preset browser sheet

```
+--------------------------------------------------------------------+
| PRESETS  [Search...........]   Tags: [All][Factory][User][Fav]  [x] |
| +-- List ---------------------------+  +-- Preview ---------------+ |
| | * Dusty 90s Break        Factory  |  |  mini grid of the preset | |
| |   Halftime Chop          Factory  |  |  Macros: ring values      | |
| |   My Jungle Edit         User     |  |  [Load] [Save as] [Delete]| |
| +-----------------------------------+  +--------------------------+ |
+--------------------------------------------------------------------+
```
Sheet 640 x 440. Type to search (focus lands in search). Up/Down browses and previews macro values without applying; Enter loads. Search matches name and tags; the same entries appear in the command palette (F19). Factory entries are read-only; Save as is the one persistence action (F03).

### 7.7 Export sheet

```
+----------------------------------------------+
| EXPORT                                    [x] |
| Format [WAV 24-bit v]  Rate [44.1k v]  Loops [4 v] |
| Content (o) Mixed render  ( ) Producer kit (stems+MIDI) |
| [Drag MIDI]  [Drag WAV]                       |
|                    [Cancel] [Export...]       |
+----------------------------------------------+
```
Sheet 480 x 260. Drag chips stay available on the top bar as well (drag onto the DAW).

### 7.8 Settings sheet

```
+--------------------------------------+
| SETTINGS                          [x] |
| Interface scale  [100|125|150|200 %]  |
| Default mode     [Simple|Advanced]    |
| Reduce motion    [ ]                  |
| High contrast    [ ]                  |
| Tooltips         [x]  delay [600 ms]  |
| Show quick start [Reopen]             |
| Version, licenses, open logs folder   |
+--------------------------------------+
```
Sheet 420 x 320. Per-user file (shared with F19). No audio parameters live here.

### 7.9 Command palette overlay (F19)

520 x 360 centered; chrome per F19 section 3.1, drawn with theme tokens (surface `raised`, 1 px `line`, selected row `accent` 20% fill with 2 px left bar). Opens with Ctrl/Cmd+K.

### 7.10 First run, empty and error screens

Empty (no source), Simple and Advanced identical:

```
+-----------------------------------------------------------+
|                                                           |
|            [ drop-zone, dashed 2 px, 120 px high ]        |
|     Drop a loop here, or  [Load a loop]  [Use input*]     |
|            wav, aiff, flac, mp3, ogg                      |
+-----------------------------------------------------------+
| PATTERN  (dimmed)  "Load a loop to start."                |
| macro strip dimmed, only Mix and Chaos readable           |
```
- Quick Start card (F19 section 3.2) sits over the pattern area, not over the drop-zone; 300 x 150.
- Source loaded, no chops: the waveform fills the zone, a single pill reads `No chops yet` with button `Detect chops` (primary). Pattern area text `Detect chops, then press Generate.`
- Chops but no pattern: Generate is pulsing once (single 600 ms glow at load, then static) and the pattern grid reads `Press Generate.`
- Source missing: banner across Source, amber, glyph and text `Audio file not found.` + `Locate...`; markers, pattern and history stay drawn.
- Unsupported or corrupt file: toast (status bar, 6 s) `Could not read that file: <reason>. Supported: wav, aiff, flac, mp3, ogg.` and the previous source is kept.
- Analysis in progress: waveform shows an indeterminate 2 px bar under the toolbar (no spinner) and Detect is disabled with tooltip `Analysing...`.
- Audio device/standalone error: banner in the top bar, red glyph + text, with `Open audio settings`.
- Errors never use colour alone: glyph (triangle with !) plus text plus role colour.
- (*) `Use input` appears only when F12 lands.

## 8. Component inventory

Reuse = keep logic, restyle through theme. Rewrite = new paint/layout, same model and command calls. New = no equivalent today.

| New component | Replaces / reuses | Decision |
|---|---|---|
| `ShellLayout` (pure fn) + `PluginEditor::resized` | `PluginEditor::resized` fixed rects | Rewrite |
| `TopBar` | transport row of `ControlsPanel`, preset combo, undo/redo, A/B, drag chips | New, reuses `ControlsPanel` button callbacks (`cmd_.play/stop/undo/redo`) |
| `SourcePanel` (waveform, chop strip, toolbar) | `WavePanel` | Rewrite paint; reuse mouse selection (`selectSectionsInWave`, `addMarkerAt`, `removeMarkerNear`, `audition*`), file-drop |
| `PeakCache` | `v.peaks` column drawing | New (pre-rendered `juce::Image` per size/scroll, invalidated on view change) |
| `PatternPanel` -> `StepGrid` + `LaneHeader` + `BarRuler` | `PatternPanel` | Rewrite paint; reuse hit test (`cf::ui::laneAtY`, `hit_edit` adapter), drag move/resize and rubber band logic, ghost outline |
| `PlayheadOverlay` | none (Orbit only) | New, one lightweight non-opaque component above Source and Pattern |
| `MacroStrip` | slider part of `ControlsPanel` | New; reuse `ParamSlider` attachments (`SliderAttachment`) with `Knob` instead of linear sliders |
| `GenerateCluster` | Generate/Mutate/Dice buttons, `FeaturePanel::reroll_`, Evolve toggle | New; reuse `cmd_.generate/mutate/reroll/newSeed` |
| `Knob` (custom `juce::Slider` subclass using ThemeLookAndFeel) | stock rotary/linear | New |
| `InspectorDrawer` (Hit tab) | selection state in `EditorModel`, `cmd_` hit edits | New; reuse F02 commands |
| `FxRack` (FX tab) | none (effects set via Allow toggles) | New; commands from F02 and effects spec |
| `HistoryDrawer` | `HistoryPanel` | Reuse paint/hit-test; restyle, move into drawer |
| `OrbitView` | `OrbitPanel` | Reuse; hosted as the Orbit tab of the pattern panel |
| `RuleEditorSheet` | none (F06) | New, F06 owns content |
| `PresetBrowser` | `FeaturePanel` preset combo/save/delete, `PresetManager` | New UI; reuse `PresetManager` |
| `ExportSheet` | `FeaturePanel` export row, `DragHandle` | New UI; reuse `exportUi()` and `makeDragFile` |
| `SettingsSheet` | standalone Options, `ControlHelp` toggle | New |
| `StatusBar` | `StatusBar` | Reuse; add meters (F05), seed field, sync chip, restyle |
| `CommandPalette`, `QuickStartCard` | F19 | New (F19 owns behavior) |
| `ThemeTokens.h`, `ThemeLookAndFeel` | `EditorTheme.h` | Rewrite; `editor_theme::*` kept as thin forwards until step S10 |
| `Tooltip` (custom `TooltipWindow` subclass painting through the theme) | stock tooltip | New |
| `ControlHelp.h` | help catalog | Reuse; shown through the new tooltip; composed with value line per F04 decision 2 |
| `EditorModel`, `EditorCommands`, `PluginProcessor` | | Untouched except publishing a playhead snapshot (S2) |
| `FeaturePanel`, `ControlsPanel`, `WavePanel`, `PatternPanel` (old) | | Deleted in S10 after the new ones are wired |

## 9. Color tokens

No palette is documented in this repo (only `EditorTheme.h`). Circuit Drift Labs is described as a dark, neon-circuit brand; the tokens below are a provisional CDL palette. When the CDL design system artifact is available, replace the hex values in `ThemeTokens.h` only; names and roles are stable. Dark is the only theme; a High contrast variant (setting) raises text and line colours.

All contrast ratios below are against the token they sit on, computed with the WCAG formula; the unit test `test_theme_contrast` enforces them.

| Token | Hex | Role | Min contrast rule |
|---|---|---|---|
| `bg` | `#0B0E13` | window background | n/a |
| `panel` | `#12161D` | panel fill | n/a |
| `raised` | `#1A2029` | cards, drawers, popups | n/a |
| `line` | `#2A3340` | separators, idle strokes | 1.5:1 vs panel (decorative) |
| `lineStrong` | `#46536A` | control outlines, knob track | 3:1 vs panel |
| `text` | `#E6EBF2` | primary text | 4.5:1 on all surfaces (about 15:1) |
| `textDim` | `#9AA7BA` | secondary text, units | 4.5:1 on panel and raised |
| `textFaint` | `#6F7C90` | disabled | decorative only, never information |
| `source` | `#3CE6F0` (cyan) | waveform, source-related, audition | 3:1 vs panel |
| `pattern` | `#FF4FA8` (magenta) | Generate, generated content, primary action | 3:1 |
| `select` | `#FFC247` (amber) | selection, markers, focus ring | 3:1 |
| `play` | `#7CF29A` (lime) | playhead, playing state, success | 3:1 |
| `fx` | `#9B7BFF` (violet) | effects, modulation rings, macro ring fill | 3:1 |
| `warn` | `#FF8A3D` (orange) | warnings, missing source | 3:1 |
| `danger` | `#FF5C5C` (red) | lock, destructive, errors | 3:1 |
| `onAccent` | `#0B0E13` | text on any accent fill | 4.5:1 |

Role semantics, fixed: cyan = source audio and anything that comes from the file; magenta = generated pattern and the main action; amber = what is selected or focused; lime = time (playhead, playing); violet = effect amount and macro modulation; red = locked, destructive, error; orange = needs attention. A role colour is never reused for another role. Today's red Stop button becomes neutral `raised` with a square glyph; red is reserved for lock/error.

Lane palette (8, colour-blind safe, derived from Okabe-Ito and lifted for dark backgrounds; the 9th and later lanes repeat with a different marker shape):

| Lane | Hex | Marker shape in the lane header |
|---|---|---|
| 1 | `#F5A623` orange | circle |
| 2 | `#4FB3F5` sky | square |
| 3 | `#3DD6A0` green | triangle up |
| 4 | `#F2E24A` yellow | diamond |
| 5 | `#6F8CFF` blue | triangle down |
| 6 | `#FF7A45` vermillion | pentagon |
| 7 | `#E48BC0` pink | cross |
| 8 | `#C9D2E0` light grey | ring |

Lane colour sits on the hit fill; the lane number and shape are always drawn. Hit fill alpha: 0.85 normal, 0.35 muted (plus a hatched stroke), 1.0 selected (2 px `select` outline). Locked hits get a 2 px `danger` bottom edge and a padlock glyph at 12 px (never colour alone). Nested hits: depth 1 draws as a 2-row subdivision inside the parent with a darker border; depth 2+ draws as ticks, with Zoom (Z) to open the nested hit at full lane height.

State encoding table:

| State | Colour | Shape / text |
|---|---|---|
| Selected hit | `select` outline 2 px | 4 corner handles |
| Muted hit | lane colour 35% | diagonal hatch |
| Locked bar/hit | `danger` | padlock glyph + "locked" tooltip |
| Probability < 100% | lane colour | dashed outline, % in tooltip |
| Reverse | lane colour | left-pointing chevron in hit |
| Retrigger N | lane colour | N tick marks in hit |
| Effect active | `fx` | small violet dot at hit top right |
| Playing hit | `play` | 1 frame flash then 120 ms fade, plus a 2 px top bar |
| Focus | `select` | 2 px ring offset 2 px |

## 10. Typography

Token indirection (`ThemeTokens::font(Role, scale)`) so the CDL fonts can be swapped in. Provisional: UI "Inter" regular/semibold (SIL OFL), numerics "JetBrains Mono" (OFL), both embedded as BinaryData subset (Latin), fall back to JUCE default sans / monospace. Digit-heavy readouts use tabular figures.

| Role | Size at s=1 | Weight | Use |
|---|---|---|---|
| `title` | 15 | semibold | brand, sheet titles |
| `section` | 11, caps, +6% tracking | semibold | panel names (SOURCE, PATTERN) |
| `label` | 13 | regular | knob names, buttons |
| `value` | 12 mono | regular | readouts |
| `small` | 11 | regular | ruler, units, status |
| `button` | 13 | semibold | buttons |
| `primary` | 18 | semibold | Generate |

Minimum text size 11 px at s=1 (never below 11 * s_min 0.8 = 8.8; at min window the s floor is 0.8 so labels use the max of 11 * s and 10). No text on a hit fill smaller than 11 px; hits too small hide their number and rely on lane header.

Brand note (from owner's CDL notes): copy is plain and human, no stock marketing phrases; the logo mark may carry one or two deliberate hand-made imperfections (decorative only, never on controls or text that carries data).

## 11. Custom LookAndFeel (`ThemeLookAndFeel : juce::LookAndFeel_V4`)

All drawing with flat fills and strokes, no gradients per frame, no images, no blur. Corner radius 4 for controls, 6 for panels. Stroke 1 px at scale 1 (at least 1 physical px).

**Knob (rotary).** Diameter `72 s` (min 52). 270 degree arc, start 135 degrees. Track: `lineStrong` 4 px stroke, round caps. Value arc: `fx` for macros, `pattern` for the Generate-related (Variation, Density), `play` never. Indicator: 2 px line from 55% to 85% radius in `text`. Centre disc `raised`. Modulation/Chaos ring: second thin arc (2 px) at radius + 5 in `fx` 50%. Value text below (mono) shows exact value while idle, replaced by a large 16 px readout in the cell during drag. Behavior: vertical drag (full travel = 200 px, Shift = fine x10, Ctrl/Cmd+drag fine x10 on macOS too), mouse wheel steps 2% (Shift 0.2%), double-click resets to default, Alt-click resets (compat), right-click menu: Reset, Enter value..., Copy value, Paste value. Text entry commits on Enter, Esc cancels. Velocity mode off (`setVelocityBasedMode(false)`), `setMouseDragSensitivity(200)`, `setScrollWheelEnabled(true)`. Hover: outline `select` 1 px, value tooltip after 600 ms. Focus: ring. Disabled: `textFaint`, no interaction.

**Buttons (`TextButton`, `ToggleButton`).** Height `28 s` (min 24), radius 4, padding 12. Idle: fill `raised`, 1 px `lineStrong`, text `text`. Hover: fill +6% lightness. Pressed: fill -4%, text same. Toggled on: fill role colour at 20%, 1 px role colour outline, text role colour, plus a 6 px filled dot left of the text (shape cue). Primary (Generate): fill `pattern`, text `onAccent`, 18 px semibold, radius 6, 2 px lighter top edge. Icon buttons 28 x 28 with 16 px vector glyph from `juce::Path` (play triangle, stop square, undo/redo arrows, gear, magnifier). Power button on rack modules: 20 px circle with line glyph, on = `fx` fill.

**Sliders (non-knob).** Linear horizontal: 4 px track `lineStrong`, filled portion role colour, 14 px circle thumb, hit height 24 s. Used for sensitivity, lane gain, drawer values. Bipolar sliders (pan, pitch) draw a 1 px centre tick and fill from centre.

**ComboBox / popup menu.** Closed box like a button with a chevron; popup `raised`, 1 px `line`, items 28 s high, highlighted item `select` 20% fill and `text`; shortcut text right aligned in `textDim`. Section separators 1 px `line`.

**Tooltip.** Custom `TooltipWindow` subclass, delay 600 ms, `raised` fill, 1 px `line`, text 12 px `text`, max width 280, padding 8. Content: first line bold control name and current value with unit and range (`Swing 35 % (0 to 100)`), then the `ControlHelp` sentence, then shortcut chip if any. Never covers the control being edited while dragging (suppressed during drag).

**Scrollbars.** 10 px, auto-hide, thumb `lineStrong`, hover `textDim`.

**Text editor** (seed, search): fill `bg`, 1 px `lineStrong`, focused 1 px `select`.

**Meters.** 8 px wide bars, segmented 2 px with 1 px gaps (drawn as one `fillRect` per lit run); green `play`, last 3 dB `warn`, clip latch `danger` with the text `CLIP` and click to clear.

**Focus ring.** `LookAndFeel::drawFocusRing`-equivalent drawn by each component: 2 px `select`, offset 2, radius control + 2.

## 12. Source panel details

- Waveform: mirrored peaks, `source` at 85% for body and 100% for the outline of peaks; clipped regions (|peak| >= 0.99) tint `danger`. Drawn from a `PeakCache` image (one `drawImage` per paint). Height 120 s at default.
- Chop regions: fill lane colour at 18% across the full waveform height, 1 px lane colour top bar with number at left; boundaries are 2 px lines in `select` when selected, `textDim` otherwise; boundary handles 8 px wide hit zones (cursor `LeftRightResizeCursor`). Disabled markers dashed.
- Chop strip under the waveform: one cell per chop with number, role badge (K, S, H, P, O letters from roles, text plus colour), enable toggle in Advanced. Clicking a cell selects and auditions it. Dragging a cell onto a pattern lane or hit changes source (drag image 120 x 28 semi-transparent, drop target highlight `select`).
- Playhead (source view): when auditioning, a thin `play` line moves through the chop.
- Zoom: wheel zooms around the cursor (Ctrl/Cmd+wheel vertical amplitude), drag with Space (or middle button) pans, double-click on ruler fits all. Minimap appears only when zoomed (8 px strip). Zoom changes only the view, not the model.
- Transient strength: faint ticks under the ruler (height by strength) in Advanced; sensitivity slider moves a threshold line.
- Interaction: click = select + audition; drag = rubber band over chops; Shift-click = add marker; Alt-click = remove nearest marker; drag marker = move (snaps to transient within 6 px when Snap on, to grid when Shift); double-click = audition at point (unchanged from F02); right-click = menu (Add marker here, Remove marker, Enable/Disable chop, Set role >, Split in 2, Merge with next, Reset detection).
- File drop: highlight the panel with a 2 px dashed `source` outline and text `Drop to load`; non-audio files show `Not an audio file` in `danger` and are rejected.

## 13. Pattern panel details

- Header (28): `PATTERN`, Length dropdown, view switch Grid | Orbit (Advanced), Zoom, Collapse, Lock bar buttons (Advanced), Allowed chips (Advanced, six small toggle chips with text: Rev, Pitch, Retrig, Filter, Glide, Crunch).
- Bar ruler (18): bar numbers `1 2 3 4`, beat ticks; at 8 bars it labels every bar; click on the ruler seeks the local transport (standalone) or sets the loop-relative selection (plugin); drag on the ruler selects a time range.
- Lane header (148 wide at default): lane swatch (6 px), shape glyph, number, chop name, mute, (Advanced: solo, lock, gain mini-slider). Header click selects all hits of the lane; drag a header to reorder lanes (changes display order only, `restore source order` command resets).
- Grid cells: vertical lines `line` at beats, `lineStrong` at bars; background alternates 4% on 2-beat groups for readability. Snap grid shown as faint ticks; snap choices 1/4, 1/8, 1/16, 1/32, Triplet, Off (shown in the header; key `S` cycles them, see section 16).
- Hits: rounded 3 px rectangles, height `laneH - 4`, with lane number or shape glyph if width >= 14 and hit label; drawing order: normal, selected on top. Selection check uses a `std::unordered_set` built per view change, not a linear find per rect.
- Playhead: 2 px `play` line over the grid with a 10 px triangle in the ruler; also dims hits already played in the current loop to 70% alpha only when `Reduce motion` is off (otherwise no dimming). Hits flash on trigger (see animation rules).
- Ghost preview: when dragging a hit, draw a translucent copy at the snapped position and a vertical snap guide; drop commits.
- Empty lane cells: single click on empty space adds a hit (when "Click adds hits" is on, default on in Advanced, off in Simple), drag on empty space = rubber band, Alt-drag = paint hits across the lane.
- Zoom (Z): opens the selected hit's nested pattern in place, the grid rescales time to that hit with a breadcrumb `Bar 1 > Beat 2 > Hit 3` in the header and `Esc`/Shift+Z to go up.

## 14. Macro strip and generation controls

| # | Knob | Binds to | Default | Status |
|---|---|---|---|---|
| 1 | Variation | `variation_amount` | 0.5 | exists |
| 2 | Density | `density` | 0.5 | exists |
| 3 | Swing | `swing` | 0.0 | exists |
| 4 | Stutter | retrigger weight (new `macro_stutter`) | 0 = legacy behavior | S11 |
| 5 | Glitch | reverse + pitch + crunch weight (new `macro_glitch`) | 0 = legacy | S11 |
| 6 | Tape / Filter | `fx_intensity` now; glide + filter weights in S11 (new `macro_tape`) | 0.5 | exists (partial) |
| 7 | Chaos | `chaos` | 0.0 | exists |
| 8 | Mix | `dry_mix` | 0.0 | exists |

Until S11 lands Stutter and Glitch are not shown (six knobs, cells widen); no inert controls. The allow_* toggles stay as masks (Advanced "Allowed" chips); when a macro goes above 0 and its mask is off, the knob shows a small `fx`-coloured padlock-less warning dot and the tooltip `Masked off: enable Retrig in Allowed.` (no silent no-ops). In Simple mode, raising Stutter/Glitch/Tape also sets the matching masks on automatically (one undo step).

- Generate (primary, `G`): new pattern from the seed policy; Mutate becomes Shift+G and a secondary menu item on Generate's chevron (`Generate | v` split button: Generate, Mutate unlocked, Mutate bar). Reroll (`R`): new seed + generate (existing `cmd_.reroll`). Evolve (`E`): toggle, lit `fx` when on, popover (Advanced) with Amount, Every, Depth, Motif (existing `evolve` settings).
- Seed: status bar field, dice button, copy. Locking is unchanged: locked hits survive Generate/Reroll/Evolve.
- Macro rings: while Evolve or Chaos moves a parameter, the knob shows a second thin ring with the live effective value.
- Every macro drag is one undo entry (coalesce on mouseUp).

## 15. Animation rules

- Principle: motion shows time or causality; nothing else moves. With `Reduce motion` (Settings) or when the OS reports reduced motion, all non-playhead animation is off.
- Playhead: positions interpolated from the last host position and elapsed wall time; repainted at display refresh via `juce::VBlankAttachment` only while the transport is running; no timer repaint at 10 Hz for it. Stopped: no repaint source at all.
- Hit trigger flash: 1 frame full `play` top bar then 120 ms linear fade; drawn as part of the playhead overlay (does not repaint the grid).
- Hover/press: instant state change, 80 ms ease on fill (JUCE `Timer` at 30 Hz only during the transition, stops after).
- Drawer open/close: 140 ms ease-out slide of width (layout recomputed each step at 60 Hz, max 9 frames). Disabled with Reduce motion (instant).
- Generate: new hits appear instantly; a single 200 ms 25% brighten on newly created hits (skipped with Reduce motion).
- Never: flashing faster than 3 Hz, looping pulses, parallax, shake, blur.
- Meters: refresh at 30 Hz only while audio is non-silent; peak hold 1 s; release 300 ms.

## 16. Interaction details and shortcuts

Dispatch: shortcuts go through the F04 action registry and F19 `CommandRegistry` so each one is also a palette entry. Modifier chords that DAWs use (Ctrl/Cmd + letters other than listed) are left to the host. Space belongs to the host in plugin mode and toggles local play in Standalone (F04 decision 1).

| Action | Shortcut |
|---|---|
| Generate / Mutate / Reroll | G / Shift+G / R |
| Evolve toggle | E |
| Undo / Redo | Ctrl/Cmd+Z / Ctrl/Cmd+Shift+Z (and Ctrl+Y on Windows) |
| Play/Stop (Standalone) | Space |
| Select all hits / none | Ctrl/Cmd+A / Esc |
| Move selected hit | Left/Right (one snap step), Shift = resize, Alt = fine 1/4 step |
| Move between lanes | Up/Down |
| Mute / Duplicate / Delete | M / D / Delete or Backspace |
| Lock bar | L |
| Zoom in / out nested | Z / Shift+Z |
| Snap cycle | S (selects next snap value; header shows it) |
| Inspector / History drawer | I / H |
| Simple/Advanced | Ctrl/Cmd+Shift+A |
| Command palette | Ctrl/Cmd+K |
| Preset prev / next | Ctrl/Cmd+Left / Right (when focus is in the top bar) |
| Cycle focus regions | F6 / Shift+F6 (top bar, source, pattern, macros, status) |
| Tab / Shift+Tab | next / previous control inside a region |
| Audition chop | Enter on a focused chop or hit; 1 to 8 plays chops 1 to 8 when the Source panel has focus |
| Fit waveform | F |
| Help overlay (shortcut list) | ? |

Mouse:
- Drag hit body = move; drag right edge (6 px zone, 1/3 of width for narrow hits) = resize; Shift-drag = constrain to lane; Alt-drag = duplicate; drag onto another lane = change source (confirms with a snap highlight); modeless (the Edit mode toggle is removed).
- Snap: default 1/16 grid (Pattern header). Snapping applies to move/resize/paint; holding Ctrl/Cmd temporarily disables snap.
- Drag a chop from Source onto the Pattern: onto a lane = replace lane source; onto empty grid = add hit; onto a hit = replace that hit's source. Drop highlight `select`.
- Drag a preset row is not supported (click to load).
- Right-click on a hit: Mute, Duplicate, Delete, Lock, Probability >, Reverse, Retrigger >, Effects >, Set source >, Zoom in, Reset hit. On empty grid: Add hit here, Paste, Select lane, Clear lane. On a lane header: Mute, Solo, Lock, Source, Rename, Clear lane. On a knob: Reset, Enter value, Copy/Paste. On a tree node: existing menu.
- Double-click hit = open Inspector on the Hit tab. Double-click knob = reset. Double-click empty grid = add hit.
- Scroll: wheel scrolls lanes vertically, Shift+wheel scrolls time when zoomed, Ctrl/Cmd+wheel zooms.
- Confirmations only for destructive source replace with unsaved edits (existing) and Delete preset.
- Undo: the editor shows `Undo: <action name>` in the status line (e.g. `Undo: Move hit`), names come from the command layer.

## 17. Accessibility

- Contrast: text 4.5:1, UI components and focus indicators 3:1 (WCAG 2.2 1.4.3 and 1.4.11, known values); tokens enforced in a unit test; High contrast setting uses `text` for lines and 7:1 text.
- Colour independence: lane numbers + shapes, lock glyphs, hatch for mute, role badge letters; states in section 9.
- Keyboard: every action reachable by keys (section 16); F6 region cycling; focus ring 2 px; visible focus never hidden by drawer.
- Screen readers: every component sets `setTitle`/`setDescription` and `setFocusContainerType`; painted panels implement `createAccessibilityHandler()` exposing hits and chops as a virtual list: `AccessibilityRole::list` for lanes, `listItem` for hits with value text `Lane 2 Snare, bar 1 beat 3, length 1/8, probability 80 percent, muted no`. Knobs expose slider role with value in units. `AccessibilityHandler::postAnnouncement` for generate (`New pattern, 24 hits`), undo, drawer open.
- Target sizes: interactive controls at least 24 x 24 at s = 1 (knob cells are 104 x 144), grid hit minimum 24 px tall (laneH floor 24).
- Motion: Reduce motion; no flashes above 3 Hz.
- Text scaling: interface scale 100 to 200 percent; layout reflows without clipping (tested).
- Tooltips can be turned off; delay adjustable; all tooltip info is also in the palette/help.

## 18. Performance of custom painting

- Panels are `setOpaque(true)` where they fill their rect; overlay components (playhead, hit flashes, rubber band, drag ghost) are separate non-opaque children so a playhead tick repaints a few pixels.
- Cache: `PeakCache` image for the waveform (render at physical pixel size, `Component::setBufferedToImage(true)` for the grid background layer), rebuilt only on source/zoom/size change. Hit layer cached as an image keyed by (view revision, size, selection revision); selection changes redraw only the changed hits via `repaint(rect)` using the dirty hit bounds.
- Selection membership via `unordered_set<HitId>` per revision (replaces linear `std::find`).
- Fonts: cache `juce::Font` objects in theme; avoid `Font` construction inside paint loops; use `GlyphArrangement` caching for ruler labels.
- Avoid `drawVerticalLine` loops of n calls: build one `juce::Path` per channel or draw the cached image.
- No gradients, shadows or `DropShadow` per frame; static drop-shadow images pre-rendered at theme load if needed (drawers).
- Repaint budget: grid full repaint under 2 ms at 8 lanes x 256 hits on a 2x display; playhead repaint under 0.2 ms. Measured by `test_paint_budget` (informational on CI, asserts a 5x ceiling).
- Stopped and idle: zero repaints per second (counted in a test through `paint` counters).
- Timers: editor timer drops to 4 Hz idle (model refresh only); the 60 Hz source is VBlank, running only while playing.
- HiDPI: `juce::Graphics` coordinates are logical; the image caches are created at `getTopLevelComponent()->getDesktopScaleFactor()` times logical size; strokes of 1 px use `PathStrokeType` with logical width `1.0f / scale` floor where a crisp hairline is needed. Verify at 100, 125, 150, 200 percent.
- Resizing: during drag-resize the caches are scaled (draw the old image stretched) and re-rendered on a 100 ms trailing debounce to avoid hitching.

## 19. Empty, first-run and error behavior (summary)

See 7.10. First run opens Quick Start (F19) once; Settings can reopen it. Missing-source, unreadable file, analysis busy, audio-device error are the four error states and each has glyph + text + action. The editor never shows a modal dialog from the audio or timer thread; all sheets are in-window components so hosts that dislike modal loops stay happy (existing alert helpers are replaced by in-window sheets in S9).

## 20. Implementation plan

Every step ends with a green build and a working plugin. Steps S0 to S2 change look only; layout changes start at S3. Per step: files, tests. Quality gates from the roadmap README apply (warnings-as-errors for touched code, `ctest`, `check_modules.py`, `check_includes.py`, golden hashes unchanged; no audio-thread change).

| Step | Change | Files | Tests |
|---|---|---|---|
| S0 | Theme tokens + `ThemeLookAndFeel` installed on the editor (`setLookAndFeel`, reset in dtor). Existing layout untouched (940 x 912). `editor_theme::*` forwards to tokens. Lane palette replaced. | new `plugin/src/ThemeTokens.h`, `ThemeLookAndFeel.h/.cpp`; edit `EditorTheme.h`, `PluginEditor.cpp`, `plugin/CMakeLists.txt` | `test_theme_contrast` (every pair in section 9), snapshot of existing editor (no layout assertions break), `test_editor_layout` unchanged |
| S1 | `computeShellLayout` pure function + resizable window (880 x 560 to 2200 x 1440), old panels placed into the new rects with simple reflow (Controls fixed height, rest flexible); size saved in `ProjectExtras::ui` (F01 dependency; until F01 lands persist in per-user file) | new `plugin/src/ShellLayout.h/.cpp`; edit `PluginEditor.cpp/.h` | `test_shell_layout`: at 880x560, 1100x720, 1920x1080, 2200x1440 no rect overlap, every rect inside bounds, min heights respected, knob cell widths in range; resize round-trip keeps audio state hash |
| S2 | Playhead snapshot (processor publishes atomic ppq + running flag) and `PlayheadOverlay` with `VBlankAttachment`; `PeakCache`; timer to 4 Hz idle | new `PlayheadOverlay.h/.cpp`, `PeakCache.h/.cpp`; edit `PluginProcessor.h/.cpp` (one atomic), `WavePanel.cpp`, `PatternPanel.cpp` | `test_playhead` (position mapping vs ppq, stopped = no callbacks), `test_paint_budget`, idle repaint counter |
| S3 | New `TopBar` (transport, undo/redo, preset name + prev/next, mode switch, drag chips, gear/palette stubs removed: only existing features appear); old transport row removed from `ControlsPanel` | new `TopBar.h/.cpp`; edit `ControlsPanel.*`, `FeaturePanel.*`, `PluginEditor.*` | mouse-sim tests: play/stop/undo/redo via TopBar buttons; preset prev/next; tab order |
| S4 | `Knob` and `MacroStrip` (Variation, Density, Swing, Chaos, Mix, Tape/Filter=fx_intensity) + `GenerateCluster`; sliders removed from `ControlsPanel`; Dry mix and Output gain moved (gain to status bar fader) | new `Knob.h/.cpp`, `MacroStrip.h/.cpp`, `GenerateCluster.h/.cpp`; edit `ControlsPanel.*` (shrinks to nothing), `StatusBar.*` | `test_knob` (drag, fine, wheel, double-click reset, text entry, host automation round-trip via APVTS), macro drag = one undo entry |
| S5 | `SourcePanel` rewrite (toolbar, chop regions, chop strip, zoom/pan, drag chip) replacing `WavePanel` | new `SourcePanel.h/.cpp`; delete `WavePanel.*` at end of step; keep `EditorModel` calls | mouse-sim: click audition, rubber band, Shift/Alt markers, marker drag, wheel zoom; snapshot 3 sizes |
| S6 | `StepGrid` + `LaneHeader` + `BarRuler` replacing `PatternPanel` paint; modeless edit (Edit mode toggle removed, its test updated); mute/solo/lock per lane (lane mute uses F02 mute; solo and lane lock only if the model has them, else hidden) | new `StepGrid.h/.cpp`, `LaneHeader.h/.cpp`, `BarRuler.h/.cpp`; delete `PatternPanel.*` | existing `test_plugin_shell` direct-edit tests ported; new tests: move/resize/duplicate by Alt-drag, snap, drag chop onto lane, rubber band, auto-scroll |
| S7 | Simple/Advanced split; Allowed chips; Orbit tab; drawers framework + `InspectorDrawer` (Hit tab) | new `DrawerHost.h/.cpp`, `InspectorDrawer.h/.cpp`; edit `OrbitPanel` (hosted), `FeaturePanel` shrinks | layout test with drawer open at min size; mode switch keeps audio hash; focus order test |
| S8 | `FxRack` (FX tab); `HistoryDrawer` (restyled `HistoryPanel`); status bar meters (F05) | new `FxRack.h/.cpp`, edit `HistoryPanel.*`, `StatusBar.*` | rack edits apply to multi-selection, undo; history click recall; meter clip latch |
| S9 | Sheets: `PresetBrowser`, `ExportSheet`, `SettingsSheet`, `RuleEditorSheet` (F06), `FeaturePanel` removed; in-window alerts | new `PresetBrowser.*`, `ExportSheet.*`, `SettingsSheet.*`, `RuleEditorSheet.*`; delete `FeaturePanel.*` | preset save/load/search; export drag file creation (existing tests ported); rule edit live preview |
| S10 | Command palette + Quick Start (F19), context menus, full shortcut table (F04), tooltip window, accessibility handlers, empty/error states; remove `editor_theme` forwards | new `CommandRegistry.*`, `CommandPalette.*`, `QuickStartCard.*`, `AccessibleGrid.*`; edit all panels | `test_accessibility` (every focusable has title; grid exposes list and items; announcements), keyboard-only walkthrough (load, detect, generate, play, export), palette fuzzy tests (F19) |
| S11 | Separate engine spec F20: `macro_stutter`, `macro_glitch`, `macro_tape` weights (parameter manifest `sinceVersion` bump, policy weights default 0 meaning legacy, golden hashes unchanged at defaults). Then add the two or three knobs. | per F20 | per F20; UI test knob binds to param |

### Test infrastructure

- Headless mouse simulation: extend the existing `plugin/tests/shell_test_support.hpp` mouse sim (already delivers synthesized events to panel canvases) with `dragTo`, `wheel`, `rightClick`, key sim `pressKey(KeyPress)`; all new interaction tests use it under xvfb (`CHOPFRACTAL_REQUIRE_DISPLAY=1` in CI).
- Screenshot checks: `Component::createComponentSnapshot` at 880x560, 1100x720, 1920x1080 (scale 2.0 via `setScaleFactor`), in states (empty, loaded, playing at fixed ppq, drawer open, Advanced). Compare to golden PNGs in `plugin/tests/golden/ui/` with a per-pixel tolerance of 6/255 and at most 0.1% differing pixels; goldens regenerated by `tools/update_ui_goldens.sh` and reviewed in the diff. Platform fonts differ: goldens are Linux-only and use the embedded fonts (so S0 embeds them).
- Layout invariants tested without display via `computeShellLayout`.
- Contrast and palette tests need no display.
- Paint budget and idle repaint counters use a test hook counting `paint` calls on panels.

## 21. Risks and decisions to confirm

1. **Palette and fonts are provisional.** The CDL design system should supply final values; swapping touches `ThemeTokens.h` only. Confirm hex values and fonts before S0 merges.
2. **S11 needs engine work.** Without it the strip has six knobs. Decision: do not ship inert knobs.
3. **Removal of the Edit-mode toggle changes drag semantics.** Rubber band on empty space and drag-on-hit are unambiguous; tests ported in S6.
4. **Resizing in hosts.** Some hosts misbehave with free-aspect resize (known JUCE forum threads); keep the constrainer min/max only, test in Reaper, Ableton Live, FL Studio, Bitwig, Logic (AU is out of scope today).
5. **Drawer docking shrinks the main view.** Chosen over overlay to avoid hiding the grid; at minimum width the grid is 568 px, still usable (2 bars x 16 steps at 17 px per step; Advanced hides the lane header to 120).
6. **Accessibility handlers for painted views** add work in S10; keep the list flat (lanes and hits) to limit cost.
7. **Brand authenticity.** Hand-made imperfections only in brand marks; no effect on data-bearing UI.

## 22. Sources

Fetched: [iZotope Learn Stutter Edit in 10 minutes](https://www.izotope.com/en/learn/learn-stutter-edit-in-10-minutes); [MusicRadar Effectrix2 review](https://www.musicradar.com/reviews/sugar-bytes-effectrix2-review); [MusicRadar Simpler Slice mode](https://www.musicradar.com/tuition/tech/how-to-chop-beats-with-ableton-simplers-new-slice-mode-632939); [MusicRadar ShaperBox 2 review](https://www.musicradar.com/reviews/cableguys-shaperbox-2); [Digital DJ Tips Serato Sample review](https://www.digitaldjtips.com/reviews/serato-sample-sampling-plugin/); [JUCE forum: resizing plugin by scaling](https://forum.juce.com/t/resizing-plugin-by-scaling/48381).
Searched only (page not read): Output Portal reviews (MusicRadar, Bonedo, Gearnews), Pigments reviews (MusicRadar, SOS), JUCE accessibility docs and forum, JUCE resizable-editor threads, MathWorks plugin GUI design page.
Blocked or unreadable: WCAG 2.2 non-text contrast page (permission not answered; ratios 4.5:1 text and 3:1 non-text are from the standard, known), MusicRadar Output Portal review (permission not answered), Plugin Boutique BreakTweaker page (wrong page).
Products marked (known) in section 1 were not fetched.
