# F14 Tempo-match stretch

**Status:** Ready for development  **Size:** L (10 engineer-days)  **Depends on:** F00 (FeaturePanel, WavePanel, StatusBar); F01 (background job runner, optional); shares `sourceNeedsEmbed()` with F12 (whichever lands first adds it)  **Blocks:** none

## 1. Summary and user value
Match a loop that was recorded or downloaded at one tempo to the project's tempo by time-stretching it offline into a new source, while keeping every chop boundary exactly on its scaled position, so roles, pattern events and Smart Setup still apply. Today the renderer only resamples (pitch follows duration, see `renderer.hpp` header comment) and `suggestLoop` only suggests a BPM; the producer must stretch in a DAW and re-import. This turns "any loop, any tempo" into one click.

## 2. User stories and scope
- As a producer I can pick "4 bars at 96.0 BPM" from the Smart Setup suggestions (or type the source BPM), press "Match to host", and get the loop at the host tempo with its chops intact.
- As a producer I can choose Draft / Standard / High quality and Undo Stretch if I do not like it.
- As a producer I am told when the change is too large for good quality and cannot exceed 0.5x..2x.
- **In scope:** new portable module `time_stretch` (WSOLA with chop-anchored regions), marker/pattern/ID remapping rules, `ProjectSession` plan/apply API, worker-thread render with progress and cancel, FeaturePanel "Tempo match" section, Undo Stretch, tests with synthetic signals, licensing guard.
- **Non-goals:** real-time/live stretching, pitch shifting independent of tempo, formant control, varispeed, automatic stretch on load, following host tempo changes after the stretch, polyphonic-quality guarantees beyond "good for loops 0.75x..1.5x", detecting the source tempo from audio (the repo rule "Tempo is never inferred from the imported audio" in `host_time.hpp` stands: the source BPM comes from the user or from `suggestLoop`, which uses clip duration only).

## 3. UX
**FeaturePanel, section "Tempo match"** (below Smart Setup; enabled only when `hasSource()` and the source is not missing):
- ComboBox **"Loop length"**: entries from `insight::suggestLoop(seconds, hostBpm)` (first 3, text from `LoopSuggestion::note`, e.g. `"4 bars at 96.0 BPM"`), then `"1 bar"`, `"2 bars"`, `"4 bars"`, `"8 bars"`, `"16 bars"`, `"Custom BPM"`. Choosing a bar count sets `Source BPM = bars * 4 * 60 / seconds` (4/4, as `suggestLoop` assumes). Default: the top suggestion if present, else `"Custom BPM"`.
- Slider/TextEditor **"Source BPM"** (20..999, 1 decimal; editable only for Custom BPM; otherwise shows the computed value).
- TextEditor **"Target BPM"** (20..999, default = host tempo `activeBpm` when `!usedFallbackTempo`, else `manualBpm`) with a small button **"Use host"**.
- ComboBox **"Quality"**: `"Draft"`, `"Standard"` (default), `"High"` (tooltips: `"Fast, for trying ideas."`, `"Good for most loops."`, `"Slowest, best for tonal material and small tempo changes."`).
- Read-only line: `"96.0 -> 120.0 BPM: 125% speed, length x0.800 (8.0 s -> 6.4 s)"`. States: amber with `"Large change: High quality is recommended."` when length ratio is outside 0.75..1.5; red and **"Match to host"** disabled with `"Out of range: the tempo change must stay between 0.5x and 2x."` outside 0.5..2.0; red with `"The stretched audio would be 80.0 s; the maximum is 60 s."` when `outFrames` exceeds the 60 s / 100 MB limits; hidden line (and disabled button) when Source BPM == Target BPM within 0.05: `"Already at the target tempo."`
- Buttons: **"Match to host"** (starts the job), **"Cancel"** (visible while running), **"Undo Stretch"** (visible after a stretch until the next Generate/Mutate/Load/Capture/Stretch). Running state: StatusBar shows `"Stretching... 42%"`; the section is disabled; Generate/Mutate stay usable but are queued behind the apply (the session lock is held only in the short apply step).
- Confirm (`juce::AlertWindow`) when a pattern exists: title `"Stretch the source?"`, message `"Stretching replaces the source audio. Your pattern keeps its rhythm, but the variation tree restarts from the current pattern and A/B snapshots are cleared. You can undo this with Undo Stretch."`, buttons `"Stretch"` / `"Cancel"`.
- Messages (StatusBar + Notice): `"Stretched to 120.0 BPM (Standard)."` (Info); `"The source changed while stretching; nothing was applied."` (Warning); `"Stretching cancelled; nothing was changed."`; `"Stretching to 70.0 BPM would make 3 chops shorter than 64 frames. Remove or merge tiny chops first, or use a smaller change."` (Error); `"This source was already stretched. Stretching again lowers quality; use Undo Stretch first to start from the original."` (Warning, shown before the confirm when the flag is set); `"The stretched audio is saved inside the project."` after apply (it is embedded, see section 4).
- Keyboard/accessibility: every control gets `setTitle`; Enter in a BPM field applies the value; no global shortcuts.

## 4. Data model and state
New portable module `modules/time_stretch` (`chopfractal::stretch`), depends on `chop_contracts` only (Result/Status). No JUCE.
```cpp
enum class Quality : std::uint8_t { Draft = 0, Standard = 1, High = 2 };
struct QualityParams { double windowMs; double toleranceMs; int searchDecimation; };
// Draft {20, 5, 8}, Standard {40, 10, 4}, High {60, 15, 1}
constexpr double kMinLengthRatio = 0.5, kMaxLengthRatio = 2.0;   // outFrames / inFrames, inclusive (1e-9 slack)
struct Plan {
  std::int64_t inFrames = 0, outFrames = 0;
  std::vector<std::int64_t> inBounds, outBounds;   // region boundaries, strictly increasing, first 0, last inFrames/outFrames
};
std::int64_t mapPosition(std::int64_t pos, std::int64_t inFrames, std::int64_t outFrames);   // see rule below
```
**Marker scaling rule (no accumulated drift):** `mapPosition(p, in, out) = floor((2*p*out + in) / (2*in))` in `int64` (p <= 11,520,000, out <= 23,040,000, so `2*p*out` < 5.3e14, no overflow). It is computed from the ORIGINAL position every time (never from a neighbour or a previous result), maps 0 -> 0 and in -> out, is monotonic non-decreasing, and satisfies `|mapPosition(p) - p*out/in| <= 0.5`. `outFrames = llround(inFrames * sourceBpm / targetBpm)` (double), then fixed; the exact ratio used everywhere is the integer pair (in, out). Marker `trimStart/trimEnd/fadeIn/fadeOut` (frames) use `mapLength(x) = floor((2*x*out + in) / (2*in))` with the same pair (a value of 0 stays 0).
Composition types:
```cpp
struct StretchRequest { double sourceBpm = 120.0; double targetBpm = 120.0; stretch::Quality quality = stretch::Quality::Standard; };
struct StretchPlan {
  stretch::Plan plan;                                   // regions from 0, enabled markers, in
  std::shared_ptr<const render::SourceData> input;      // immutable snapshot taken at plan time
  SourceId inputId;                                      // identity check at apply time
  StretchRequest request;
};
```
`source_chop` gains `static Result<ChopMap> ChopMap::rescaled(const ChopMap& from, SourceInfo newInfo, std::int64_t inFrames, std::int64_t outFrames)`: copies every marker (enabled or not) with the same `ChopId`, `manual`, `label`, `color`, `group`, `enabled`, positions via `mapPosition`, trims/fades via `mapLength`, `nextId` preserved, `minRegionFrames` preserved, fresh undo history; validated with the existing `validate` (so markers closer than `minRegionFrames` (64) after scaling return `Conflict` with the section 3 message and the count in `hint`). Because IDs are preserved, `RoleMap`, rules and all pattern references to `ChopId` stay valid.
`pattern_engine` gains `Result<Pattern> mapRegions(const Pattern& p, const std::function<SampleRange(SampleRange)>& fn)`: applies `fn` to every non-empty `Event::region`, recursively through `child` trees; empty regions (whole-chop) stay empty; `Pattern` structure, ids, seeds, locks untouched (pure, deterministic). Used with `[&](SampleRange r){ return SampleRange{mapPosition(r.start,in,out), mapPosition(r.end,in,out)}; }` then `pattern::sanitize(mapped, *newChops)` (clamps/drops invalid) and `validate`. `pattern::serialize` format and the golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 are unchanged (new function only).
Persistence: **no new state and no schema bump.** The result is an ordinary embedded source: `SourceInfo::name = <old name> + " @" + <target BPM, 1 decimal> + " BPM"`, `path = ""`, `sampleRate` and `channels` unchanged. Pathless sources are saved embedded: `getStateInformation` uses `embed = embedSource || session_.sourceNeedsEmbed()` (`bool ProjectSession::sourceNeedsEmbed() const { return hasSource() && chopMap_->info().path.empty(); }`, shared with F12; the first feature to land adds it). Size is bounded by 60 s / 100 MB (< `kMaxEmbeddedSourceBytes` 128 MiB) because outputs above the decode limits are refused. State v2 of F01 is untouched; old projects load unchanged; the stretched project round-trips through existing `kModAudio` v1 + `source_chop` + `pattern_engine` + `variation_history` payloads. Host parameters: none; manifest version unchanged.

## 5. Public API
Portable `modules/time_stretch/include/chopfractal/time_stretch/stretch.hpp` (non-real-time, thread-safe pure functions, never called from the audio thread):
```cpp
struct Input { const float* const* channels; int numChannels; std::int64_t frames; std::int32_t sampleRate; };  // 1 or 2 channels
using Progress = std::function<bool(double fraction)>;   // return false to cancel; called at least every 50 ms of work
Result<Plan> makePlan(std::int64_t inFrames, std::int64_t outFrames, const std::vector<std::int64_t>& anchorsIn);  // adds 0 and inFrames, maps, de-duplicates
Result<std::vector<float>> stretch(const Input& in, const Plan& plan, Quality q, const Progress& progress = {});   // planar, numChannels*outFrames floats
double lengthRatio(double sourceBpm, double targetBpm);   // = sourceBpm / targetBpm
Status checkRatio(double ratio);                          // OutOfRange outside [0.5, 2.0]
```
Composition (message thread under `withSession`, except the heavy `stretch::stretch` which the plugin runs on a worker with the immutable `plan.input`):
```cpp
Result<StretchPlan> ProjectSession::planStretch(const StretchRequest& r) const;      // validates ratio + size limits, builds Plan from enabled marker positions, snapshots sourceData_
Status ProjectSession::applyStretched(const StretchPlan& plan, std::shared_ptr<const render::SourceData> stretched);  // all-or-nothing
bool ProjectSession::sourceNeedsEmbed() const;
```
Plugin: `void ChopFractalProcessor::stretchSourceAsync(const StretchRequest&)`, `void cancelStretch()`, `bool undoStretch()`, `std::atomic<float> stretchProgress`, `bool canUndoStretch() const`. Reuses F12's `preKeepState_`/`preKeepSource_` one-level undo storage if F12 is merged (renamed `preReplaceState_`/`preReplaceSource_`), else adds it. Module CMake flags: `-ffp-contract=off` (GCC/Clang) and `/fp:precise` (MSVC) for `time_stretch` only.

## 6. Behavior details and edge cases
**Why WSOLA, not a phase vocoder or a library.** WSOLA (waveform-similarity overlap-add) works in the time domain, needs no FFT (no third-party code), keeps drum transients sharp (a phase vocoder smears them), has an exactly specifiable output length, and is simple enough to unit test with synthetic signals. Its weakness is polyphonic/tonal material at large ratios; the High preset and the 0.5x..2x cap bound this. **Forbidden:** Rubber Band (GPLv2-or-later; closed-source distribution requires a paid commercial license from Breakfast Quay, which ChopFractal does not hold, and GPL linking would force the plugin source open and conflicts with `docs/LICENSING.md` "only code written for this project"), SoundTouch (LGPL-2.1; static linking into a closed VST3 breaks the relink obligation), zplane elastique and similar commercial SDKs (no license), and any code copied from such projects. CI guard in the `portability` job: `! grep -rIliE 'rubberband|soundtouch|elastique' --include=CMakeLists.txt --include=*.cmake --include=*.hpp --include=*.cpp --include=module.json modules composition plugin tools`, and `time_stretch/module.json` has `"third_party": []`. A permissively licensed library (for example MIT Signalsmith Stretch) could replace the engine later behind the same API, but only after maintainers record it in `docs/LICENSING.md` and the module's `third_party` list and re-baseline determinism; it is not part of this feature.
**Algorithm (per region, channels processed together, double accumulators, float samples).** Parameters from `QualityParams` and sample rate `sr`: `N = 2*round(windowMs*sr/2000)`, `Hs = N/2`, `D = round(toleranceMs*sr/1000)`. Periodic Hann `w[n] = 0.5*(1 - cos(2*pi*n/N))` (so `w[n] + w[n+Hs] = 1`). Regions are `[inBounds[i], inBounds[i+1])` -> `[outBounds[i], outBounds[i+1])`, lengths `Lin`, `Lout`; reads never leave the region (the source after the region belongs to the next chop and would pre-echo its attack).
1. **Hit mode** if `Lin < 3N` or `Lout < 3N`: output = first `min(Lin, Lout)` source frames; if `Lout < Lin` apply a 5 ms raised-cosine fade-out ending at the cut (`round(0.005*sr)` frames, at most `Lout/2`); if `Lout > Lin` the remainder is zeros. The attack is untouched.
2. Otherwise **WSOLA**: `K = ceil((Lout - N)/Hs) + 1` frames; synthesis start `t_k = min(k*Hs, Lout - N)`; nominal analysis start `q_k = t_k * (Lin - N) / (Lout - N)` (double). Frame 0 uses `p_0 = 0`; frame `K-1` uses `p_{K-1} = Lin - N` (anchors, no search). For `0 < k < K-1`: candidates `p = clamp(round(q_k) + d, 0, Lin - N)`, `d in [-D, D]`; the reference is the natural continuation of the previous frame, `y[n] = x[p_{k-1} + Hs + n]`, `n in [0, Hs)` (clamped to the region); score `corr(p) = sum_c sum_n x_c[p+n]*y_c[n] / sqrt(sum x_c^2 * sum y_c^2 + 1e-12)`. Search is two-stage when `searchDecimation > 1`: coarse over `d` stepping by the decimation on a box-filtered, decimated copy (computed once per region), then full-resolution refinement over `+/- searchDecimation` around the coarse winner. Tie-break: larger score, then smaller `|d|`, then negative `d`. Windows: frame 0 uses a flat first half (`1` for `n < Hs`, Hann falling half after), the last frame a flat second half, all others Hann. Accumulate `acc[t_k+n] += win[n]*x[p_k+n]`, `wsum[t_k+n] += win[n]`; output `acc/wsum` where `wsum > 1e-6`, else 0. The flat head/tail make output sample 0 equal source sample `a` and the last output sample equal source sample `b-1` exactly, so the region joins are seamless and attacks keep their original shape.
3. Regions are written to their exact `outBounds` slots; the total length equals `plan.outFrames` by construction. Output samples are clamped to finite values (non-finite input is read as 0).
4. No regions (no markers): one region `[0, inFrames)`.
Determinism: no random numbers, fixed iteration order, double accumulation, `-ffp-contract=off`; the same binary gives bit-identical output; across compilers/platforms the result is expected within 1e-5 absolute (tested with tolerance, not with a pinned hash).
Time budgets (48 kHz stereo, CI runner): Draft <= 0.5 s, Standard <= 1.5 s, High <= 12 s for a 60 s source; progress callback every <= 50 ms; cancellation latency <= 100 ms; peak extra memory = input + output + 8 MB scratch (the 100 MB cap applies to input and output separately).
**Plan/limits:** `planStretch` rejects (before any work) ratio outside 0.5..2.0 (`OutOfRange`), source BPM/target BPM outside 20..999, equal BPMs within 0.05 (`InvalidArgument`, "Already at the target tempo."), `outFrames` over `min(60*sr, kMaxSourceFrames, 100 MiB/(ch*4))` (`LimitExceeded`, hint = max frames), or a missing source (`sourceMissing()`).
**Apply (all-or-nothing, mirrors `loadState`):** under the session lock: verify `idOf(*sourceData_) == plan.inputId` else `Conflict` ("The source changed while stretching; nothing was applied."); build `newInfo` (new frames, name, path `""`, new `SourceId` from `computeSourceId`), `newMap = ChopMap::rescaled(...)`, `newChops = newMap.snapshot()`; if a pattern exists: `mapped = pattern::mapRegions(current, fn)` -> `sanitize(mapped, *newChops)` -> `flatten` -> `render::makePlayback(newData, flat, lengthTicks)`; only then assign `sourceData_`, `chopMap_`, `chops_`, `patterns_.restore(mapped)` (A/B slots cleared, undo history restarts), `history_ = VariationTree(history_.config())` + `recordHistory("Stretched to <bpm> BPM")`, `pendingActivation_ = kNoNode`, `evolve_.stop()`, `publish`. Roles and rules are kept (IDs preserved). Pattern events are in ticks (musical time), so they keep their rhythm; chop lengths now fit the grid at the target tempo. The renderer is unchanged: it swaps the Playback (new source pointer) through the Mailbox and `processChunk` hard-kills voices pointing into the old buffer, exactly as for `loadSource`. Locks (event/bar/phrase) survive via the restored pattern.
**Undo Stretch:** before apply, the processor stores `saveState(true)` bytes (or, when the source is too large to embed, the old `SourceData` pointer and a state saved with `embed=false` whose resolver returns that pointer) and restores them with `loadState`; one level only; cleared by any structural change.
**Interactions:** Smart Setup after a stretch works on the new audio. Roles from the old source stay (IDs preserved). Capture (F12) then stretch is the supported "record anything, match to host" workflow. Evolve is stopped by apply (existing `loadSource`-like rule) and the Evolve toggle is resynchronized by the editor as after any `run`.
**Sample rate:** stretching operates at the source rate; host rate differences are still handled at playback by the renderer's `src.sampleRate / cfg.sampleRate` ratio. Windows scale with `sr`, so 44.1 and 96 kHz behave equivalently in time.

## 7. Test plan
Unit (`modules/time_stretch/tests/test_time_stretch.cpp`, synthetic signals):
- `output_length_is_exact`: sine, ratios 0.5, 0.75, 1.0, 1.25, 2.0, qualities x3, rates 44100/48000/96000: `outFrames` exact, no region shorter than 1.
- `pitch_is_preserved`: 440 Hz and 220 Hz sines (4 s, 48 kHz), ratios 0.5, 0.8, 1.25, 2.0; measure frequency by linearly interpolated zero crossings over the middle 80% (`crossings / duration`); max error in cents: Draft <= 20, Standard <= 10, High <= 5 (assert `1200*log2(f_out/f_in)`); also a 3-partial harmonic tone (f0 200 Hz) with the same bounds measured on f0 via autocorrelation peak.
- `duration_error_of_events_is_zero`: click train (impulse every 0.5 s, 10 clicks) with anchors at each click; after stretch ratio r the peak frame of click i equals `mapPosition(pos_i, in, out)` exactly (0 frame error), peak amplitude >= 0.99 (attack preserved).
- `map_position_has_no_drift`: 1,000 positions p over 11.52M frames, several (in,out): `|mapPosition - p*out/in| <= 0.5`, monotonic, `mapPosition(0)=0`, `mapPosition(in)=out`, and mapping a boundary computed cumulatively would differ (documents the rule).
- `region_joins_are_seamless`: sine through anchors; max sample-to-sample difference at region boundaries <= 1.5x the signal's own max difference.
- `energy_is_preserved`: white noise (seeded `chop_contracts` RNG), output RMS within 1 dB of input for all ratios/qualities.
- `silence_and_dc_and_nonfinite_inputs`: zeros -> zeros; NaN input -> finite output; DC 0.5 -> 0.5 +/- 1e-6.
- `mono_and_stereo_identical_to_dual_mono`: stereo with L=R equals mono result in both channels; stereo with different channels uses a common alignment (cross-channel phase relation preserved: correlation >= 0.99 for a panned sine pair).
- `hit_mode_keeps_attack_and_fades_cut`: region shorter than `3N`, `Lout < Lin`: first `Lout - fade` samples identical to source, fade monotonic; `Lout > Lin`: tail zeros.
- `cancel_returns_error_quickly`: Progress returns false after 10% -> `ErrorCode::Cancelled` (appended by F01) within 100 ms of the call.
- `deterministic_repeat_and_block_free`: two runs bit-identical; run within a thread while another thread uses a different instance gives identical bytes (no shared state).
- `ratio_limits`: 0.4999 and 2.0001 rejected, 0.5 and 2.0 accepted; `lengthRatio(96,120)=0.8`.
- `time_budget_smoke` (Release only, skipped under sanitizers): Standard 10 s stereo < 1.5 s.
`modules/source_chop/tests`: `rescaled_preserves_ids_flags_and_scales_trims`, `rescaled_rejects_markers_closer_than_min_region_with_count_hint`, `rescaled_roundtrips_serialize`. `modules/pattern_engine/tests`: `mapRegions_scales_nested_regions_and_leaves_hashes_alone` (pattern without regions: `serialize` bytes equal before/after; golden hashes untouched).
Integration (`composition/tests`): `stretch_keeps_roles_pattern_rhythm_and_chop_ids` (96 -> 120 BPM synthetic 4-beat loop, markers at each click; after apply: same ChopIds, `mapPosition` positions, flat event starts identical, roles intact, history has exactly one node labeled `"Stretched to 120.0 BPM"`); `stretch_plan_refuses_out_of_range_and_oversize`; `stretch_apply_conflicts_if_source_changed`; `stretch_apply_is_all_or_nothing` (inject failing `makePlayback` via a pattern referencing an out-of-range region; session unchanged, `saveState` bytes equal); `stretched_project_roundtrips_embedded`; `stretch_then_undo_restores_original_bytes`.
GUI (`plugin/tests/test_plugin_shell.cpp`): `tempo_match_flow`: load a 4-beat 96 BPM click loop (2.5 s = 1 bar at 96 BPM), Detect, Generate; set Loop length `"1 bar"`, Target BPM 120, Quality Draft; click "Match to host" and the confirm "Stretch"; pump until `statusMessage()` contains `"Stretched to 120.0 BPM"`; assert `chopMap()->info().frames == llround(frames*96/120)`, ids unchanged; click "Undo Stretch": frames restored. `tempo_match_out_of_range_disables_button` (Target 20, Source 96 -> ratio 4.8). `tempo_match_cancel_leaves_project_unchanged`.
Real-time/sanitizers: no audio-thread code; run the new module under ASan/UBSan (CI `sanitizers`), and add `test_time_stretch` to the TSan step only for the threaded determinism test.
Manual QA: 1) Import a 4-bar breakbeat at 96 BPM; Smart Setup lists "4 bars at 96.0 BPM". 2) Set host 120 BPM; "Match to host" with Standard; progress shows; the 4-bar loop goes from 10.0 s to 8.0 s. 3) Play the generated pattern: hits stay on the grid, no flams at slice joins. 4) Compare Draft vs High on a bass-heavy loop: High has fewer warbles. 5) Undo Stretch returns the original. 6) Save, reopen: stretched audio present, no file needed. 7) Target 30 BPM from 96: button disabled with the out-of-range text.

## 8. Acceptance criteria
- [ ] `outFrames` equals `llround(in*src/tgt)` exactly; region joins land on `mapPosition` with 0-frame error (tested).
- [ ] Pitch error on sine/harmonic test signals: Draft <= 20 cents, Standard <= 10, High <= 5 for ratios 0.5..2.0.
- [ ] Ratio limits 0.5..2.0 enforced in API and UI; sizes above 60 s / 100 MB refused with the exact message.
- [ ] Stretch of a 10 s stereo 48 kHz loop: Standard <= 1.5 s, High <= 12 s (CI runner); cancel latency <= 100 ms; UI thread never blocked > 50 ms (only the short apply step runs on it).
- [ ] After apply: ChopIds, roles, locks, pattern rhythm preserved; history restarts with one node; A/B cleared with the stated notice; Undo Stretch restores the exact previous state bytes.
- [ ] No GPL/LGPL/commercial stretch code anywhere (CI grep guard green; `module.json` `third_party: []`).
- [ ] Pattern golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 unchanged; existing projects load unchanged; no state schema or manifest change.
- [ ] `tools/check_modules.py`, `tools/gen_module_docs.py --check`, `tools/smoke_transfer.sh` pass with the new module (deps exactly `chop_contracts`).

## 9. Files touched
| path | new/modified | change |
|---|---|---|
| modules/time_stretch/{CMakeLists.txt,module.json,README.md,MIGRATION.md,include/chopfractal/time_stretch/stretch.hpp,src/stretch.cpp,tests/*} | new | WSOLA engine, plan, mapPosition |
| modules/source_chop/{include/.../chop_map.hpp,src/chop_map.cpp,tests} | modified | `ChopMap::rescaled` |
| modules/pattern_engine/{include/.../pattern.hpp,src/*,tests} | modified | `mapRegions` (no serialization change) |
| composition/include/.../project_session.hpp, composition/src/project_session.cpp, composition/CMakeLists.txt | **HOT** modified | `planStretch`, `applyStretched`, `sourceNeedsEmbed` (shared with F12) |
| CMakeLists.txt (root) | **HOT** modified | add `time_stretch` to the module list |
| plugin/src/PluginProcessor.cpp/.h | **HOT** modified | worker, progress, cancel, undo, forced embed |
| plugin/src/FeaturePanel.cpp/.h (from F00; else plugin/src/PluginEditor.cpp **HOT**) | modified | "Tempo match" section |
| plugin/CMakeLists.txt | **HOT** modified | link `chopfractal::time_stretch` in both targets |
| plugin/tests/test_plugin_shell.cpp | modified | GUI/integration tests |
| .github/workflows/ci.yml | **HOT** modified | license grep guard in `portability`, TSan target addition |
| docs/specs/audio-engine-and-routing.md, docs/specs/smart-setup.md, docs/BUILD_STATUS.md, docs/LICENSING.md | modified | document stretch, the forbidden-library rule |

## 10. Risks and mitigations
- WSOLA artifacts (flutter, doubled transients) on dense mixes at large ratios: anchors at markers protect attacks, quality presets, amber warning at >1.5x, Undo Stretch; evaluate on a 10-loop corpus in manual QA and record results.
- Cross-compiler float differences change the correlation winner: `-ffp-contract=off`, deterministic tie-break, tolerance (not hash) assertions across platforms; same-binary determinism is exact.
- Long jobs holding memory: input+output+scratch bounded by the 100 MB rule each; progress/cancel; `std::bad_alloc` -> `LimitExceeded` message `"Not enough memory to stretch this source."`.
- Marker collisions at ratio < 1: reported with count before any change; documented workaround.
- Pattern regions from Zoom not mapped correctly: `mapRegions` unit test with 2-level nesting and sanitize round trip.
- Compounded stretches degrade audio: warning plus Undo Stretch; original is never auto-deleted while undo is available.

## 11. Implementation steps
1. Scaffold `modules/time_stretch` (module.json, CMake with fp flags, docs generation), `mapPosition`/`makePlan`/ratio helpers + tests; CI green; add the license grep guard.
2. WSOLA engine (hit mode, windows, search, accumulation) + synthetic-signal tests; tune thresholds on CI.
3. Progress/cancel + time-budget test.
4. `ChopMap::rescaled` + tests; `pattern::mapRegions` + tests.
5. Composition `planStretch`/`applyStretched`/`sourceNeedsEmbed` + integration tests.
6. Processor worker, progress, cancel, undo, forced embed; shell tests without GUI.
7. FeaturePanel "Tempo match" UI + GUI tests under xvfb.
8. Corpus listening QA, docs, BUILD_STATUS update.
