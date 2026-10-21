# Feature index (input for spec authors)

IDs are permanent. Each feature gets docs/specs/roadmap/<ID>-<slug>.md written from _TEMPLATE.md.

Foundation (must land first; other features build on them)
- F00 editor-decomposition: split plugin/src/PluginEditor.cpp (815 lines) into component classes (WavePanel, PatternPanel, HistoryPanel, OrbitPanel, ControlsPanel, FeaturePanel, StatusBar) with an EditorModel (cached view data) and a command layer, so later features touch separate files. No behavior change.
- F01 state-v2-and-persistence: one coordinated state version bump. Persist what is missing today: seed, motif/depth, role-combo, export settings, A/B snapshots, evolve settings, manual BPM, detection mode, UI prefs; plus the migration registry wired for real (identity v1->v2), worker-thread restore of reference-only audio, export on a worker thread with progress and cancel.

Usable
- F02 direct-editing: drag/resize/move/delete/duplicate hits in PatternPanel and OrbitPanel (snap to grid, locked scopes, undo), event context menu (mute, reset, duplicate, effects), marker editing on WavePanel (drag, snap to transient/zero crossing, enable/disable, trim, label, marker undo/redo, plus single-click audition replacing click-adds-marker), waveform zoom/scroll using existing ui::zoomAround/scrolledBy.
- F03 presets-and-favorites: factory presets (settings + fractal motifs + rule templates), user presets (save/load/rename/delete in a user folder), preset browser menu, favorites filter on the variation tree, search.
- F04 keyboard-and-layout: keyboard shortcuts (Space play hint, G/M/Z/L, Ctrl+Z/Y, arrows to move selection, Enter to audition), focus order, tooltips, resizable layout with min size and scale, accessibility handlers for painted areas, simple/advanced view split.
- F05 meters-and-feedback: output peak meter and clip indicator, active-voice display, tempo/meter/sync status box, pending-activation indicator, notice log with severity, drag-and-drop WAV onto the editor.
- F06 rule-editor: GUI for roles/rules (chop_roles_grammar already has the engine): list/add/edit/enable rules, templates, validation messages, explain-why for blocked events (uses GrammarPolicy::explain).

Fun
- F07 performance-pads: MIDI note input and on-screen pads trigger chops live (monophonic/poly modes, velocity), optional recording of takes into the pattern at grid, host MIDI input enabled in the plugin (acceptsMidi).
- F08 chaos-and-gestures: one Chaos macro (blends density, variation, effect probabilities, retrigger) as an automatable parameter; stutter/glitch gestures (hold-to-retrigger, reverse, tape-stop) on buttons and MIDI mod wheel / CC; deterministic.
- F09 evolve-scenes: record an Evolve wander as numbered scenes (pattern snapshots + seeds), play scenes in sequence per N loops, scene chaining, export scene list.
- F10 ab-morph: morph between snapshot A and B with a slider (per-event interpolation of level/pan/pitch/fx, crossfade of presence by probability), commit morph result as a new variation.

Valuable
- F11 multi-output-stems: per-chop / per-role output buses (up to 8 stereo pairs + main) so a DAW can process each chop separately; bus layout negotiation; routing UI.
- F12 input-capture: Arm / Capture / Stop / Keep from the track input into a source buffer (ring buffer on the audio thread, copy off-thread), length from bars at host tempo, then chop; replaces the file-only workflow.
- F13 midi-drag-out: drag a MIDI file of the current variation (and the Producer Kit) from the editor into a DAW; per-variation export; MIDI export settings (base note, velocity curve, loops).
- F14 tempo-match-stretch: match the loop to the host tempo with a time-stretch (offline, quality preset, preserves chop boundaries by scaling markers), tempo-detect assist from Smart Setup.

Platform
- F15 cross-platform-and-packaging: macOS (arm64 + x86_64 universal) and Windows plugin builds in CI, validator on all three, code signing and notarization steps (documented, secrets via CI), installers (pkg / MSI or Inno), version stamping, release workflow, crash-safe logging, third-party notices bundle, JUCE license gate.

New specs (Wave 4)
- F16 level-match-and-limiter: usability. Loudness-match the generated output to the source (BS.1770 style measurement on a worker thread), look-ahead true-peak-safe soft limiter, gain-reduction readout, deterministic and bypassable, four host parameters appended to manifest v3, state tag 15 cache. Off by default, bit-exact when off.
- F17 groove-lab: musicality. Playback-time groove: MPC-style 16th swing 50 to 75 percent, shuffle, per-role push/pull, seeded humanize, accent curve; module groove, state tag 14, five host parameters, golden hashes untouched.
- F18 arrangement-export: value. Build an 8 to 32 bar arrangement from F09 scenes; one WAV with cue points, one MIDI file with markers, a cue list; deterministic worker job with progress and cancel; state tag 16.
- F19 command-palette-and-quick-start: ease of use. Ctrl/Cmd+K fuzzy palette over every action and parameter plus a first-run checklist (load, detect, generate, play, export); per-user file, no project state, no parameters.
- E01 echo: per-event tempo-synced repeats, event flag bit 64, seeded effect pass, five host parameters, state tag 17.
- E02 tremolo-pan: per-event tremolo and auto-pan, event flag bit 128, shares the effect pass and tag 17, five host parameters.
