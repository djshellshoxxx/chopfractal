# ChopFractal roadmap: implementation plan

Companion to the per-feature specs in this folder (`F00` to `F19`, plus the per-event effects `E01` and `E02`). Read this first: it fixes the **order**, the **branch and merge rules**, the **file ownership** that keeps parallel work from colliding, the **quality gates**, and what only the maintainer can supply.

Baseline: `main` at ddbb8bd (portable modules, composition root, JUCE shell, pass 3 shipped: Chaos, drag-out, presets, direct hit editing basics; VST3 validator 47/47). See `docs/BUILD_STATUS.md` for the live numbers and each spec's section 0 for what is already implemented.

## 1. Why the order matters (the three merge hazards)

1. **`plugin/src/PluginEditor.cpp` (1404 lines as built; 815 when this plan was first written) is one file that every UI feature edits.** Parallel feature branches would conflict on every merge. Fix: **F00** splits it into panel classes first. After F00, each feature mostly adds files.
2. **State format.** Several features add saved state (scenes, presets, A/B, capture, settings). Done one at a time they would bump the schema repeatedly and fight over `saveState`/`loadState`. Fix: **F01** defines one coordinated bump (state v2), wires the migration registry, and provides a small `StateSection` API so later features append their own section without touching shared code.
3. **`ProjectSession` (826 + 293 lines) and `parameters.cpp` are shared.** Fix: new feature logic goes into **new portable modules** or new `composition/src/<feature>.cpp` files, with only a thin declaration in `project_session.hpp`. Host parameters are appended in **one manifest bump (version 3)** reserved per wave (section 5).

## 2. Dependency graph and waves

```
Wave 0 (foundation, strictly sequential, one engineer each, merge before anything else)
  F00 editor-decomposition  ->  F01 state-v2-and-persistence

Wave 1 (parallel, low coupling; each touches its own panel/module)
  F02 direct-editing        (WavePanel, PatternPanel, OrbitPanel)
  F05 meters-and-feedback   (StatusBar, new MeterPanel)
  F04 keyboard-and-layout   (editor shell only; start after F02/F05 land their panels' hit areas)
  F15 cross-platform-and-packaging (CI/CMake/installers only; no source overlap) -- can start in Wave 0

Wave 2 (parallel; depend on F01 state sections)
  F03 presets-and-favorites
  F06 rule-editor
  F13 midi-drag-out
  F10 ab-morph

Wave 3 (audio-engine changes; one at a time through renderer.cpp / PluginProcessor.cpp)
  F09 evolve-scenes        (moved here: needs a sample-accurate armed loop-boundary switch in the renderer; start of Wave 3)
  F08 chaos-and-gestures   (parameter manifest v3, small renderer hook)
  F07 performance-pads     (MIDI input, audio-thread queue)
  F12 input-capture        (audio-thread ring buffer)
  F11 multi-output-stems   (bus layout; riskiest for host compatibility)
  F14 tempo-match-stretch  (new portable module; offline only, so it can run in parallel with any Wave 3 item)

Wave 4 (new specs; each is independent of the others except where noted)
  F16 level-match-and-limiter   (renderer output stage + new module level_match; before F18)
  F17 groove-lab                (new module groove + Playback::build hook; before F18)
  E01 echo                      (event bit 64, effect pass, renderer expansion)  ->  E02 tremolo-pan (bit 128, shares E01's pass and state tag 17)
  F18 arrangement-export        (needs F09, F13; uses F16/F17/E01/E02 when present)
  F19 command-palette           (needs F04; last, so it registers every earlier action; its parameter commands read the manifest automatically)
```

Wave 4 dependencies in one line: F16, F17, E01 need F01 (state sections); E02 needs E01; F18 needs F09, F13 (soft F16, F17); F19 needs F04 (soft everything).

Hard dependencies: F01 needs F00. F02, F03, F05, F06, F09, F10, F13 need F00. F03, F09, F10, F12 also need F01. F04 needs F02 and F05 (it binds keys to their actions). F07 needs F08's gesture plumbing only if both ship together (otherwise independent). F11 should land **after** F07 and F12, because all three change `processBlock` and bus/MIDI declarations.

Critical path (about 7 sequential items): F00, F01, F02, F04, F08, F07, F11. Everything else runs beside it.

## 2b. Cross-spec decisions (resolved here so implementers do not have to ask)

- **Panel names** are defined in F00 section 5 (authoritative). State sections and `ProjectExtras` are defined in F01; F03/F04/F09/F10/F11 append to it.
- **Lazy state versioning (F01):** the session schema becomes 2 only when something new is persisted; feature-free projects keep their exact v1 bytes. Pattern and other module schemas stay 1, so golden hashes are unaffected. Evolve `enabled` is never persisted (a project never starts changing by itself).
- **Space key (F04):** a plugin cannot control the host transport, so Space is left to the host and the editor shows a hint. It is not an audition key.
- **Waveform clicks (F02):** single click auditions a chop; double click adds a marker (replaces today's click-adds-marker).
- **Load Loop (F05):** asks for confirmation before replacing a loop that has a pattern.
- **Shared helpers:** F12 and F14 both need a one-level "undo last source replace" store and `ProjectSession::sourceNeedsEmbed()`. Whichever lands first creates them under the name `SourceReplaceUndo` in `composition/src/source_replace.cpp`; the other reuses them (state this in the PR description).
- **Pre-existing UI debt fixed by the specs:** Feature row C overflows 920 px (F00 preserves, F04 re-flows into four rows); notices from Evolve/loop-boundary steps pile up unseen (F05 drains them); `Renderer::activeVoices()` is read cross-thread (F05 publishes it via an atomic).
- **Values to capture at implementation time** (cannot be known without building): the v1 fixture hash (F01), factory preset hash (F03), the F11 CPU ratio bound (1.5x target) and the F12 Keep copy time (250 ms target).
- **Performance pads and hosts (F07):** Ableton Live cannot route MIDI to an Fx-category plugin and the plugin category stays `Fx`; on-screen pads are the guaranteed path. Adding an Instrument category is a product decision. Whether JUCE adds MIDI-CC emulation parameters once `acceptsMidi` is true, and the validator test count, must be checked when F07 lands.
- **State sections API:** F01 defines the exact signature; F07/F08/F09 sections (`pads`, `chaos`, `scenes`) are written against "id + schema + bytes, optional, absent when default" and must be adjusted to F01's final form.
- **Maintainer decisions still open:** minimum macOS version (specs assume 11.0), bundle id domain (assumed `com.chopfractal`), Windows signing route, whether Linux ships, Apple/Windows certificates, JUCE license route, factory preset content, Chaos mapping taste.

## 2c. FeaturePanel row budget (shared UI space)

Rows A to D are used as built (about 848 to 870 px of the 920 px content height). New buttons go to a new **row E** of 28 px height; controls that do not fit open a call-out from one button. Allocation: F06 and F07 buttons on row A or E (their specs say which), E01 140 px, E02 140 px, F16 260 px, F17 150 px, F18 90 px. If row E overflows 920 px, F04's reflow moves the least used button into a `More...` menu; do not widen the editor beyond 940 x 912 without a maintainer decision.

## 2d. Manifest policy (host parameters)

Version stays 3. Parameter ids are permanent and never reordered or reused; each feature appends its rows after the last row present when it merges (the golden list test is updated in the same PR). Rows reserved by the specs, in expected merge order after `chaos`: F07 `perf_mode`, `perf_velocity_curve`; F11 `stems_enabled`; F16 `level_match`, `match_trim_db`, `limiter_enable`, `limiter_ceiling_db`; E01 `allow_echo`, `echo_amount`, `echo_rate`, `echo_decay`, `echo_tone`; E02 `allow_tremolo`, `trem_amount`, `trem_mode`, `trem_rate`, `trem_depth`; RND (merged with the F16/E01/E02 rows, same merge): Stutter `allow_stutter`, `stutter_amount`, `stutter_rate`, `stutter_gate`, `stutter_decay_db`, `stutter_pitch`; Roll `allow_roll`, `roll_amount`, `roll_rate`, `roll_rate_end`, `roll_freeze`; renderer `slice_polish`; F17 `groove_enable`, `groove_template`, `groove_swing`, `groove_hum_time`, `groove_hum_vel`. F18 and F19 add none. Positions follow merge order, not this list; ids are what is fixed. Whether to freeze version 3 at first public release and use version 4 for later rows is a maintainer decision (open item).

## 2e. State section tag registry (F01 `StateSection`)

Authoritative table is F01 section 4.4. Tags claimed by later specs: 14 `groove` (F17), 15 `levelmatch` cache (F16), 16 `arrangement` (F18), 17 `fx` (E01, extended by E02). F19 keeps its data in a per-user file, not in project state. Pattern event flag bits: 1 to 32 used, **64 E01, 128 E02**; the byte is then full. The extension byte (formerly the has-child byte) carries bit 0 child, **bit 1 Stutter, bit 2 Roll**; five bits remain, readers reject unknown ones. Neither change alters the encoding of an event that has none of these blocks (the golden pattern hashes are unchanged).

## 3. Branch, PR and merge rules

- **One feature = one branch = one PR**, named `feat/<ID>-<slug>`, cut from the latest `main` **at the moment work starts**, and rebased/merged from `main` at least daily. PRs stay under about 800 changed lines (excluding tests and generated docs); split larger work using the spec's "Implementation steps", each step independently green.
- **Squash merge** only after all 8 CI jobs are green. Never merge a red PR; never skip a test; never edit a golden hash to make a test pass (a hash change is an engine-version event and needs its own decision).
- **Hot files** (touch only in the order given, announce in the PR description, keep edits to declarations and one-line calls): `plugin/src/PluginProcessor.cpp/.h`, `composition/include/chopfractal/composition/project_session.hpp`, `modules/plugin_host_adapter/src/parameters.cpp`, root and plugin `CMakeLists.txt`, `.github/workflows/ci.yml`. After F00 the editor is not hot.
- **New code goes in new files.** A feature adds `composition/src/<feature>.cpp`, `plugin/src/panels/<Feature>Panel.{h,cpp}`, or a new module. Edit an existing file only to register the new one.
- **Conflict protocol:** if a rebase touches a hot file, re-run the full local gate (section 6) before pushing; resolve in favor of keeping both sides' additions; never resolve by dropping a test.
- **Schema/ID registry** (below) is edited only in the PR that claims the entry, and the claim happens in the first commit so others see it.

### Registries (claim before use)

| Registry | Rule | Where |
|---|---|---|
| Host parameter IDs | append-only, never reuse; manifest version bumps once per wave | `parameters.cpp` + golden list in `test_host_adapter.cpp` |
| Module payload schemas | `composition` payload v2 introduced by F01; features add **sections**, not new versions | F01 spec |
| Event flag bits (`writeEvent`) | bits 1-32 used; **64 Echo (E01) and 128 Tremolo-Pan (E02) claimed by RND**; the byte is full | `chop_contracts/src/event.cpp` |
| Event extension byte (the old has-child byte) | bit 0 child, **bit 1 Stutter (E03) and bit 2 Roll (E04) claimed by RND**; bits 3-7 reserved and rejected on read; events without them encode exactly as before | `chop_contracts/src/event.cpp` |
| Module IDs | lower_snake, one folder each, manifest + docs generated | `tools/gen_module_docs.py` |
| Notice/status strings | defined in the spec's UX section, reused verbatim in tests | specs |

## 4. File ownership map (who edits what)

| Area | Primary owner feature | Others may touch only via |
|---|---|---|
| `plugin/src/panels/*` (after F00) | F00 creates; each panel's feature owns it | a small, announced change |
| `plugin/src/PluginEditor.cpp` (shell) | F00, then F04 | none |
| `plugin/src/PluginProcessor.cpp` | F01 (state), F07/F12/F11 (audio I/O) | sequential in Wave 3 |
| `composition/src/project_session.cpp` | F01 | new logic in `composition/src/<feature>.cpp` |
| `modules/audio_renderer` | F08 (hook), F11 (buses), F09 (armed switch, isolated in `src/armed_switch.hpp` plus a small hook in `renderer.cpp`) | sequential |
| new modules | the feature that creates them | n/a |
| `.github/workflows/ci.yml`, installers | F15 | feature PRs may add a test step only |
| `modules/audio_renderer/src/renderer.cpp` output stage and `Playback::build` | F16 (limiter, match gain), F17 (groove), E01/E02 (expansion, LFO) | sequential: F16, then F17, then E01, then E02 |
| `modules/chop_contracts/src/event.cpp` (flags byte) | E01 (bit 64), E02 (bit 128) | none; no other feature may claim a flag bit |
| `modules/pattern_engine/src/fx_pass.cpp` | RND (new file: `applyFxPass` for Echo, Tremolo-Pan, Stutter, Roll; ENG owns generation and does not touch it) | none |
| `composition/src/{fx_level,groove,arrangement}.cpp` (`fx_level.cpp`: RND, setFx and level match) | F16, F17, E01/E02, F18 | thin declarations in `project_session.hpp` |
| `plugin/src/CommandRegistry.*` | F19 | each panel registers its commands in its own `registerCommands()` |

## 5. Parameter and state budget

- **Manifest v3 (one bump, Wave 3):** `chaos` (F08), `perf_mode`, `perf_velocity_curve` (F07), optional `stems_enabled` (F11); Wave 4 appends 4 (F16) + 5 (E01) + 5 (E02) + 5 (F17) rows, see section 2d. Declared up front so the golden list is edited in one coordinated PR sequence; each PR appends its own rows below the previous one (append conflicts resolve trivially).
- **State v2 sections** (F01 API): `ui` (seed, motif, depth, export settings, detect mode), `ab` (A/B snapshots), `evolve`, `presets` (selected only; preset files live in the user folder), `scenes` (F09), `capture` (F12 metadata only; audio rides the existing embedded-audio block), `stems` (F11 routing); Wave 4 tags 14 to 17 (section 2e). Unknown sections are preserved and written back (forward compatibility).

## 6. Quality gates (every PR, no exceptions)

Local, before pushing (the repo's own commands; scripts exist):
1. `cmake -S . -B build/all -DCHOPFRACTAL_WARNINGS_AS_ERRORS=ON` build + `ctest` (GCC), same with Clang, and a Release build.
2. ASan+UBSan build + ctest, **with `UBSAN_OPTIONS=halt_on_error=1`** (a past UBSan finding only printed locally and failed in CI).
3. TSan on the renderer when audio-thread code changed.
4. `python3 tools/check_modules.py && python3 tools/check_includes.py`, and `tools/gen_module_docs.py` committed when manifests change.
5. `tools/smoke_transfer.sh` when a portable module was added or its dependencies changed.
6. Plugin: build `ChopFractal_VST3` + `ChopFractalShellTests`; run headless **and** `xvfb-run`; run `tools/validate_vst3.sh` (must stay 47/47 or better); render and look at the editor snapshot (`CHOPFRACTAL_SNAPSHOT=...`) for any UI change.
7. Golden hashes unchanged; allocation-free `process()` test passes; new audio-thread code has a test that fails if it allocates.

Per-feature "definition of done": every acceptance criterion in the spec checked off in the PR description; regression test for every bug found; docs updated (`docs/BUILD_STATUS.md` table, README feature list, module manifest/docs regenerated); spec statuses updated.

## 7. Test strategy to keep bugs out

- **Pure logic first:** put algorithms in portable modules with unit tests and deterministic fixtures; the editor and processor stay glue.
- **GUI tests:** extend the button-driving harness (`findButton`, `triggerClick`, pump dispatch loop) with a **mouse-simulation helper** added in F00 (synthesized `MouseEvent` at component coordinates) so drag features (F02, F07 pads, F10 slider) are testable headless under xvfb.
- **Property/fuzz tests** for every new decoder or parser (state sections, presets, MIDI input mapping): truncation sweep and byte-flip sweep, as the repo already does.
- **Audio-thread rules:** no allocation, no locks, no `std::function` construction on the audio thread; queues are fixed-size SPSC; tests assert allocation counts with the existing replaced `operator new` harness.
- **Determinism:** same seed and settings give identical output; any new generation draw is **appended** to the existing per-event random stream.
- **Cross-platform:** anything with `<filesystem>`, `std::hash`, or float formatting is checked on the macOS and MSVC CI jobs before merge.
- **Soak test (add in F05):** a scripted 30-minute headless run (random button clicks + looping playback) that must not crash or leak; run nightly in CI once F15 lands.

## 8. Who/what implements each item (cheapest capable model, no quality loss)

| Item | Suggested implementer | Why |
|---|---|---|
| F00, F01 | strongest model, one session each, reviewed by a second model | they define interfaces everything else depends on; mistakes multiply |
| F02, F04, F05, F06, F03, F13 | mid-tier model (Sonnet class), spec-driven | UI and glue with clear specs and tests |
| F08, F09, F10 | mid-tier model | small logic plus tests, parameter plumbing |
| F07, F12, F11 | strongest model for the audio-thread parts; mid-tier for UI parts | real-time safety and host compatibility |
| F14 | strongest model for the DSP, then mid-tier for UI | algorithm quality and measured error bounds |
| F16 | strongest model for the limiter and loudness math; mid-tier for UI | true-peak guarantee, audio-thread safety |
| F17, E01, E02 | mid-tier model; strongest reviews the renderer hooks | determinism and golden-hash neutrality |
| F18 | mid-tier model | job, formats and tests are well specified |
| F19 | mid-tier model | UI and glue plus a small portable matcher |
| F15 | mid-tier model plus the maintainer for certificates and accounts | mostly YAML/scripts; secrets need a human |
| Every PR | an independent **read-only review pass** by a second model with the diff only, then the gates in section 6 | catches what the author is blind to; cheap relative to a bad merge |

Developer prompt template (paste with the spec): "Implement `docs/specs/roadmap/<ID>.md` on branch `feat/<ID>-<slug>` from latest main. Follow section 11 steps in order, one commit per step, each passing the section-6 gates. Do not edit files outside the spec's 'Files touched' table; if you must, say why in the PR. Add every test named in section 7. Do not change golden hashes. Report open questions instead of guessing."

## 9. Other things required to finish quickly with maximum quality

**Only the maintainer can supply:**
- **Apple Developer ID certificate + notarization credentials** and a **Windows code-signing certificate** (OV or EV) stored as CI secrets (F15). Without them installers trigger OS warnings.
- **JUCE license decision** (AGPLv3 vs commercial) before any binary leaves the team (`docs/LICENSING.md`), and the VST3 SDK terms check.
- **Real hardware/software for host testing:** at least REAPER (free to evaluate), Ableton Live, FL Studio, Cubase or Bitwig on Windows and macOS; a mono-bus host; a host without MIDI input to the plugin; a host with multi-out support. A tester script is in F15.
- **Product decisions** the specs mark "decision needed": factory preset content and names, Chaos mapping taste, stems default routing, installer branding/icon/name, versioning scheme.
- **Audio test material:** a small royalty-free loop library (drums, break, melodic, vocal, mono and stereo, 44.1/48/96 kHz, 60 s max) committed under `tests/assets/` (or generated procedurally; the specs say which tests need real files).

**Engineering hygiene to add early (cheap, high payoff):**
- `CODEOWNERS` and a PR template with the section-6 checklist.
- A `tools/ci_local.sh` that runs section 6 steps 1-5 in one command (the repo already has the pieces in `/tmp/verify.sh` style; commit a real one in F00).
- `clang-format` + `.clang-format` committed and enforced in CI (format-only PR first, so it never mixes with logic).
- Crash/diagnostic log file in the user folder with a "Copy diagnostics" button (F05) so field bugs are reproducible.
- A `tests/assets` golden-audio regression set (rendered WAV hashes for fixed seeds) to catch accidental DSP changes.
- Dependabot-style pin review for JUCE and the VST3 SDK tag.

## 10. Effort and schedule (ideal engineer-days, one engineer per item; parallelize across waves)

| ID | Size | Days | ID | Size | Days |
|---|---|---|---|---|---|
| F00 | M | 3 | F08 | M | 3 |
| F01 | L | 5 | F09 | L | 6 |
| F02 | L | 8 | F10 | M | 3 |
| F03 | M | 4 | F11 | L | 6 |
| F04 | M | 3 | F12 | L | 6 |
| F05 | M | 3 | F13 | S | 2 |
| F06 | M | 5 | F14 | L | 8 |
| F07 | L | 6 | F15 | L | 7 |
| F16 | M | 6 | F18 | L | 9 |
| F17 | M-L | 8 | F19 | M | 6 |
| E01 | M | 5 | E02 | S-M | 4 |

Total about 116 engineer-days (78 for F00 to F15, 38 for Wave 4). With three parallel engineers following the waves: about 5 to 6 calendar weeks, bounded by the critical path (about 28 days) plus review and host-testing time. Add one week of dedicated real-host testing and fixes before any public beta.

## 11. Release readiness checklist (after Wave 3)

- All specs' acceptance criteria met; zero known HIGH bugs; soak test green.
- VST3 validator clean on Linux, macOS and Windows; real-host matrix signed off.
- Installers signed and notarized; license and third-party notices included; JUCE license resolved.
- `docs/BUILD_STATUS.md` updated; user manual (quick start, every control) written from the specs' UX sections; version stamped; tagged release workflow run end to end on a pre-release tag.
