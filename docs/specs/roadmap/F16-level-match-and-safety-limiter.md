# F16 Level Match and Safety Limiter

**Status:** Implemented (Sound drawer controls, Match and Limit toggles, gain-reduction readout)  **Size:** M (6 engineer-days)  **Depends on:** none hard. Soft: F05 (its `MeterStrip` shows the gain-reduction readout; F16 ships its own readout until F05 lands), F15 (nothing). F00 for the row-E placement (before F00 the toggles go into the existing feature rows)  **Blocks:** F18 (arrangement export renders through the same output stage), F11 (the limiter acts on the main bus only, section 6)

## 1. Summary and user value
A chopped, re-arranged loop is usually quieter or louder than the loop that went in (fewer hits, more retriggers, stacked pitch and filter effects), and a busy variation can clip the output. **Level Match** measures the loudness of the loaded loop and of one cycle of the generated pattern and applies one smooth, clamped gain so the pattern plays at the loop's loudness. The **Safety Limiter** is a look-ahead, true-peak-aware soft limiter on the plugin output so nothing leaves the plugin above a chosen ceiling. Both are deterministic (same source, pattern and settings give the same gain and the same bytes), both are off by default so existing projects sound exactly as before, and both are bypassable and automatable. Producers get "drop a loop, press Generate, it is already at the right level and cannot clip" without touching gain staging. Usability.

## 2. User stories and scope
- As a producer I switch on **Level match** and every Generate, Mutate, variation switch and Evolve step plays at the same perceived loudness as my loop, so I stop riding the output gain.
- As a producer I can bias the match by +/- 6 dB with **Match trim** when I want the pattern hotter or lower than the loop.
- As a producer I switch on **Limiter** and the output never exceeds the ceiling (default -1.0 dBTP), even with Chaos at 100 %, retriggers and effects stacked; I see how much it is working.
- As a mixing engineer I can leave both off and get a bit-exact unprocessed output.
- As a producer exporting WAV or the arrangement (F18) the exported file contains the same level match and limiting I hear.

**In scope:** the portable `level_match` module (BS.1770 style loudness measurement, match-gain computation, headroom rule); the real-time limiter in `audio_renderer`; a background measurement job in `composition`; four host parameters; a state section holding the cached measurement; the UI controls and readouts; export integration; tests.
**Non-goals:** mastering-grade multiband or program-dependent limiting, a user-adjustable attack or release, LUFS-targeted normalization of exports (`wav::Options::normalize` stays as is), loudness metering for the user (LUFS history, LRA; F05 stays a peak meter), matching to a LUFS number typed by the user, matching the input signal (dry mix) rather than the source loop, oversampled effects processing, limiting the aux buses of F11, automatic gain staging of individual hits.

## 3. UX
**Controls (FeaturePanel row E, 100 + 80 + 80 px; before F00 they are appended to the first feature row with room, see the README "FeaturePanel row budget"):**
- `ToggleButton` **"Level match"** (title `Level match`; tooltip `Match the pattern's loudness to the loaded loop. Off: no gain change.`). Bound to host parameter `level_match`. Default off.
- `ToggleButton` **"Limiter"** (title `Safety limiter`; tooltip `Keep the output below the ceiling with a look-ahead true-peak limiter. Adds 1.8 ms of latency while on.`). Bound to `limiter_enable`. Default off.
- `TextButton` **"Output..."** (80 px, title `Output safety settings`) opens a call-out (260 x 190, `juce::CallOutBox`) with: slider **"Match trim"** (-6.0 to +6.0 dB, step 0.1, default 0, `SliderAttachment` to `match_trim_db`, text `+1.5 dB`); slider **"Ceiling"** (-6.0 to 0.0 dBTP, step 0.1, default -1.0, `limiter_ceiling_db`); read-only labels `Loop: -14.2 LUFS`, `Pattern: -19.8 LUFS`, `Match: +5.6 dB` (or `Match: off`), `Limiter: -2.3 dB` (gain reduction, peak over the last second, updated at 10 Hz; `Limiter: off`), `Peak: -0.4 dBTP` (true-peak estimate of the last second at the plugin output); button **"Re-measure"** (forces a new measurement now).
**Enable rules.** Level match is enabled (clickable) only when a source and a pattern exist; otherwise the toggle is disabled with the tooltip `Load a loop and Generate first.` and the parameter still exists for automation (its value is kept, the effective gain is 0 dB without a pattern). The Limiter toggle is always enabled. Re-measure is enabled while Level match is on.
**States.** While a measurement runs the Match label reads `Match: measuring...`; the previous gain stays in effect until the new value arrives (no jump to 0 dB). If the pattern is silent: `Match: pattern is silent` and gain 0 dB. If the source is silent or shorter than 100 ms: `Match: loop is too quiet or too short to measure` and gain 0 dB.
**Messages (StatusBar via `setStatusMessage`, Info unless noted):** `Level match: +5.6 dB (loop -14.2 LUFS, pattern -19.8 LUFS).` (once per new value, only when it changes by 0.5 dB or more since the last message); `Level match limited by headroom: +3.1 dB instead of +5.6 dB. Switch the limiter on to allow more.` (Warning); `Level match clamped at +12.0 dB: the pattern is much quieter than the loop.` (Warning); `Limiter on: output latency is now 1.8 ms (the host compensates).` (Info, first time per session).
**Keyboard / accessibility.** Both toggles and the button are in the F04 focus order after the gesture controls; titles as above; the call-out labels are `juce::Label`s with titles so screen readers read the numbers; no information by colour alone (gain reduction is text).
**Empty/error:** a measurement failure (render error) shows `Level match could not measure this pattern: <module message>` (Warning) and keeps the previous gain.

## 4. Data model and state
### 4.1 Host parameters (manifest version 3, appended; IDs permanent; indices follow merge order after the last existing entry)
```
{"level_match",        "Level Match",     "",   ParamKind::Bool,  0.0,  1.0, 0.0, 1.0, 0.0,  3, nullptr}
{"match_trim_db",      "Match Trim",      "dB", ParamKind::Float, -6.0, 6.0, 0.0, 0.0, 10.0, 3, nullptr}
{"limiter_enable",     "Safety Limiter",  "",   ParamKind::Bool,  0.0,  1.0, 0.0, 1.0, 0.0,  3, nullptr}
{"limiter_ceiling_db", "Limiter Ceiling", "dB", ParamKind::Float, -6.0, 0.0, -1.0, 0.0, 10.0, 3, nullptr}
```
(same column order as the existing rows: id, name, unit, kind, min, max, default, step, smoothing ms, sinceVersion, choices). Enum entries `kLevelMatch`, `kMatchTrimDb`, `kLimiterEnable`, `kLimiterCeilingDb`. A project saved before these exist loads with all four at their defaults (off, 0 dB, off, -1 dB), which reproduces the old sound exactly. Defaults are **off** on purpose: turning them on by default would change the sound and add latency to every existing project. Whether new instances should default to on is a maintainer decision for the manifest freeze (roadmap README, "Manifest policy"); this spec ships them off.
### 4.2 Module `modules/level_match` (portable, depends only on `chop_contracts`; namespace `chopfractal::levelmatch`)
```cpp
constexpr double kSilenceLufs = -70.0;     // absolute gate of BS.1770-4
constexpr double kMaxBoostDb = 12.0, kMaxCutDb = 12.0;   // the match gain is clamped to [-12, +12]
constexpr double kMinMeasureSeconds = 0.1;
struct Loudness {
  double lufs = -200.0;      // integrated, gated; -200 means "silent / too short"
  double samplePeak = 0.0;   // linear, max |x| over the (stereo-expanded) signal
  bool valid = false;
};
struct MatchResult {
  double gainDb = 0.0;       // rounded to 0.1 dB, 0.0 when !valid
  double requestedDb = 0.0;  // before clamping and headroom
  bool valid = false;
  bool clamped = false;      // hit +/-12 dB
  bool headroomLimited = false;
};
// planar input; mono is measured as if duplicated to both channels (what actually plays).
Loudness measure(const float* const* planar, int channels, std::int64_t frames, int sampleRate);
// sourceLufs - patternLufs + trim, clamped, rounded; if !limiterOn the boost is reduced so patternPeak * gain <= 10^(-0.1/20).
MatchResult computeMatch(const Loudness& source, const Loudness& pattern, double trimDb, bool limiterOn, double ceilingDb);
```
`measure` implements ITU-R BS.1770-4 integrated loudness: K-weighting (high-shelf pre-filter then the RLB high-pass, biquad coefficients computed from the standard analog prototypes with the bilinear transform in `double` for any sample rate; the published 48 kHz coefficients are the unit-test reference), 400 ms blocks with 75 % overlap, absolute gate -70 LUFS, relative gate -10 LU below the ungated mean, channel weights 1.0 for L and R. For signals shorter than one 400 ms block (but at least `kMinMeasureSeconds`) a single zero-padded block is used and the relative gate is skipped. All accumulation is in `double`; the result does not depend on the block size the caller uses (it takes the whole buffer).
Persistence: none in the module. Serialization of the cache entry is in 4.4.
### 4.3 Limiter (in `modules/audio_renderer`, new `limiter.hpp/.cpp`)
```cpp
struct LimiterParams { bool enabled = false; float ceilingDb = -1.f; };      // read per block from RenderParams
struct LimiterTelemetry {                                                    // written by the audio thread, read by the UI
  std::atomic<float> grDb{0.f};      // most negative gain reduction since last take (<= 0)
  std::atomic<float> peakDb{-120.f}; // true-peak estimate of the output since last take
  float takeGr(), takePeak();        // exchange with the idle value (message thread)
};
class Limiter {                      // stereo, linked, no allocation after prepare()
 public:
  void prepare(double sampleRate, int maxBlock);   // non-RT; sizes the delay line and the minimum window
  int latencyFrames() const;                       // lookahead + detector delay, constant for a sample rate
  void reset() noexcept;
  void process(const LimiterParams&, float* l, float* r, int frames) noexcept;   // in place
  LimiterTelemetry& telemetry();
};
```
`RenderParams` gains three fields, all defaulted so the existing 6-argument call sites compile unchanged: `float matchGainDb = 0.f; bool limiterEnabled = false; float limiterCeilingDb = -1.f;`.
### 4.4 Project state: F01 extras block `levelmatch` (tag 15, schema 1, 20 bytes; absent when no measurement has been cached)
`f32 gainDb, f32 sourceLufs, f32 patternLufs, u64 keyHash`. `keyHash` = FNV-1a 64 over: the source content hash from `SourceInfo`, `pattern::serialize(current)` bytes, the integer BPM used (rounded), the effect-allow bytes, the groove (F17) block bytes when present, and the Chaos-independent settings (Chaos only changes the pattern, which is already in the bytes). On load, if `keyHash` equals the freshly computed key, the cached gain applies immediately (no audible jump after restore); otherwise it is ignored and a measurement runs. The section is a cache: losing it never loses information. Without F01 the cache is simply not persisted.

## 5. Public API
Module `level_match` as in 4.2 (message/worker thread only; pure functions; no JUCE, no renderer include). Renderer: `RenderParams` fields and `Limiter` as in 4.3; `Renderer::process` applies them in the order in section 6. `Renderer::latencyFrames() const` returns the limiter latency when `limiterEnabled` was true in the last block, else 0 (relaxed atomic written at block start).
Composition (`composition/include/.../project_session.hpp`, **hot**, implementation in new `composition/src/level_match.cpp`):
```cpp
struct LevelMatchStatus { bool measuring = false; bool valid = false; double gainDb = 0.0; double sourceLufs = -200.0, patternLufs = -200.0; bool clamped = false, headroomLimited = false; std::string message; };
void setLevelMatchInputs(bool enabled, double trimDb, bool limiterOn, double ceilingDb, double bpm);   // message thread; cheap; schedules a measurement when the key changed
LevelMatchStatus levelMatchStatus() const;      // message thread
double levelMatchGainDb() const;                // the gain currently in effect (0 when disabled or no pattern); also published to the audio side
Status remeasureLevelMatch();                   // force
```
`ProjectSession` owns a `LevelMatchJob` (one `std::thread` per measurement, at most one running; a newer request cancels the older through an atomic token checked between the two renders; the result is applied only if its key still equals the latest key). The result gain is published through `std::atomic<float> matchGainDb_` that `ChopFractalProcessor::processBlock` reads into `RenderParams::matchGainDb` (one relaxed load per block).
Plugin (`plugin/src/PluginProcessor.h/.cpp`, **hot**): `pollAudioFlags()` (30 Hz) reads the four parameters and calls `setLevelMatchInputs` when any changed or after `ProjectSession` notifies a pattern/source/tempo change; sets `RenderParams` fields in `processBlock`; calls `setLatencySamples(renderer_.latencyFrames())` from the message thread when the limiter state changes (never from `processBlock`).
Threading: `measure` and the offline renders run on the job thread; nothing allocates or locks on the audio thread beyond what exists; the `Limiter` buffers are allocated in `prepare()`.

## 6. Behavior details and edge cases
**Signal order in `Renderer::process` (extends the fixed order, bit-exact when both features are off):** voice sum -> x `matchGain` (skipped entirely when `matchGainDb == 0.f`, so the unprocessed path performs no multiply) -> + dry mix -> x output gain -> **limiter** (skipped when disabled) -> meter push. Match gain applies to the generated branch only, so the dry/source mix keeps its meaning; the limiter sees everything the plugin outputs. Auditions and previews go through the same sum and are matched like pattern hits.
**Match gain smoothing.** The gain is smoothed with a 50 ms linear ramp per change, evaluated per sample (not per block), so block size never changes the output. A new measurement result ramps from the current gain.
**Measurement procedure (job thread).** (1) Source: `measure` over the whole source (stereo-expanded). (2) Pattern: `renderOffline` of the current `Playback` with `cycles = 1`, `tailSeconds = 0`, `RenderParams{dryMix 0, outputGainDb 0, matchGainDb 0, limiter off}`, sample rate = the source rate (so no rate conversion is measured), `bpm` = the active tempo rounded to an integer BPM (host tempo, else the manual BPM), block size 512; then `measure` of the result. One cycle is the loop length of the pattern (1 to 8 bars). (3) `computeMatch(source, pattern, trim, limiterOn, ceiling)`. A pattern whose loudness is below -70 LUFS is reported silent. The headroom rule: when the limiter is off, the boost is reduced so that `patternPeak * 10^(gain/20) <= 10^(-0.1/20)` (so Level match alone can never cause clipping at unity output gain); when the limiter is on, the cap is the +12 dB clamp only. Rounding: `gainDb = std::round(x * 10.0) / 10.0` (single rounding, no fused ops), so the same inputs give the same gain on every platform.
**Re-measurement triggers (debounced 300 ms):** new pattern (Generate, Mutate, undo, variation switch, Evolve step, hand edit commit), source change, marker edit that changes chops, tempo change of 1 BPM or more, a change of any Allow flag or the groove (F17), trim or limiter state change (trim and limiter only re-run `computeMatch` on the cached loudness numbers; no render). Evolve steps and rapid edits therefore coalesce into at most about three measurements per second. Playback is never blocked: the audio side keeps the old gain until the ramp to the new one starts.
**Measurement cost.** One offline render of at most 8 bars plus two K-weighted passes: on the 2-core CI runner under 150 ms for a 4-bar loop at 48 kHz (logged by a benchmark test, not asserted).
**Limiter algorithm (per sample, stereo-linked, double-precision gain state, float audio).** `D = max(8, lround(0.0015 * sampleRate))` frames of look-ahead (72 at 48 kHz, 66 at 44.1 kHz, 144 at 96 kHz). The true-peak estimate is the maximum of the four-times oversampled signal using the 12-taps-per-phase polyphase FIR of ITU-R BS.1770-4 Annex 2 at 44.1/48 kHz, 2x oversampling at 88.2/96 kHz and sample peaks at 176.4 kHz and above (the Annex states the oversampling is unnecessary there); the FIR adds `6` frames of detector delay, so the audio delay line length is `L = D + 6` and `latencyFrames() == L`. Steps per frame n: (1) `p = max(|l|,|r|)` true-peak estimate attributed to the delay-line index of the audio it belongs to; (2) static curve in dB: `T = C - 6` (C = ceiling), below T no reduction; above T, `out_dB = T + (C - T) * tanh((p_dB - T) / (C - T))`, `gt = out_dB - p_dB` (so the soft knee approaches C asymptotically and never exceeds it); (3) sliding-window minimum of `gt` over `D + 1` frames (monotonic deque in a preallocated ring); (4) release stage `g_rel[n] = min(gmin[n], g_rel[n-1] + (gmin[n] - g_rel[n-1]) * (1 - e^(-1/(0.120 * sr))))` (instant attack into the window, 120 ms exponential release); (5) boxcar average of `g_rel` over the last `D + 1` frames (this is the attack ramp; because the minimum window and the boxcar have the same length the smoothed gain at the instant the delayed peak reaches the output is never above the gain that peak needs); (6) output = delayed input x linear gain, then a **sample-peak backstop** clamp to `+/- 10^(C/20)` (it should never engage; the telemetry counts engagements for tests). Nonfinite input samples are replaced by 0 before the detector. Denormals are flushed. `grDb` = the minimum smoothed gain in dB over the block; `peakDb` = max true-peak estimate of the output.
**Guarantee (tested):** for any input, the true peak of the limiter output measured by an independent 4x oversampled reference meter is at most `C + 0.1 dB`, and the sample peak is at most C.
**Enable/disable.** Enabling primes the delay line with the unprocessed stream for one block and crossfades from the bypass path over 256 frames (the limited path lags by `L`; the brief comb is inaudible and avoids a click); disabling does the reverse. Latency is reported to the host by the message thread within about 33 ms; hosts apply plugin delay compensation on their own schedule, which is why the toggle is documented as adding latency. `reset()` (transport jump, `prepareToPlay`) clears the delay line.
**Offline render / export.** `renderOffline` passes the three fields through `OfflineSettings::params`. When the limiter is on, the render discards the first `L` frames of output and renders `L` extra frames at the end (zero input, `scheduleEvents` false) so the exported file is latency-compensated and its length is unchanged. `ExportWavOptions` gains no field: export uses the current parameter values (`ProjectSession::renderParams()` builds `RenderParams` from the same atomics the processor uses). The Producer Kit slice WAVs are **not** limited or matched (they are raw chops), documented in `kit.txt`.
**Interactions.** Locks, history, undo, Evolve, Chaos: none of them care; they only trigger re-measurement. F11 stems: the limiter and match gain apply to the main bus mix only (aux buses carry raw voices), documented in the Outputs section. F17 groove changes timing, not level, but its accent curve changes level, so it is part of the measurement key. F18 renders each section with its own match gain and applies one limiter pass to the final mix. Host bypass (`effect_enable` off): pass-through, limiter state reset. Mono hosts: mono output is (L+R)/2 as before; the limiter runs on the stereo internal path before the downmix, so the mono sum can still exceed the ceiling by up to 6 dB for fully correlated hard-panned content; the true-peak guarantee is stated for the stereo output and the mono case is covered by the match headroom rule (documented limit).
**Determinism.** The measurement is a pure function of its key; the limiter has no randomness; output is bit-identical for block sizes 1 to 2048 (state is per sample); with `matchGainDb == 0 && !limiterEnabled` the output equals the pre-feature renderer bit for bit (recorded hash test).

## 7. Test plan
**Unit, `modules/level_match/tests/test_level_match.cpp`:**
- `k_weighting_coefficients_match_the_published_48k_values` (1e-9 on the four biquad coefficient sets).
- `full_scale_997hz_sine_measures_minus_3_01_lufs_mono_expanded_to_0_lufs` ... precisely: a 997 Hz sine of amplitude 1.0 in both channels measures 0.0 LUFS +/- 0.1 and a half-amplitude sine -6.0 +/- 0.1 (gain scaling is exact in dB); mono input measures the same as the same signal in both channels.
- `gating_ignores_silence_and_quiet_sections` (10 s of a -20 dBFS tone followed by 10 s of silence equals the tone alone within 0.1 LU; a section 12 LU below the rest is excluded by the relative gate).
- `short_signals_use_one_padded_block_and_below_100ms_are_invalid`.
- `measurement_is_independent_of_how_the_caller_chunks_data` (same buffer, plain call vs. two halves concatenated: identical result).
- `sample_rates_44100_48000_96000_give_equal_loudness_for_the_same_tone_within_0_1_lu`.
- `compute_match_clamps_rounds_and_applies_trim` (requested +20 -> +12, `clamped`; 5.04 -> 5.0; trim +2 adds 2.0 before the clamp).
- `headroom_rule_reduces_boost_when_the_limiter_is_off` (pattern peak 0.9 and requested +6 dB -> gain such that peak * gain <= 0.9886; `headroomLimited`), `no_headroom_reduction_when_the_limiter_is_on`.
- `silent_or_invalid_inputs_return_gain_zero_and_not_valid`; `nonfinite_samples_are_ignored_not_propagated`.
**Unit, `modules/audio_renderer/tests/test_limiter.cpp`:**
- `disabled_limiter_and_zero_match_gain_are_bit_identical_to_the_pre_feature_renderer` (FNV hash of a 4-cycle offline render recorded from `main` before the change; implementation step 1 commits the hash alone).
- `output_never_exceeds_the_ceiling_sample_peak` over: sine at +6 dBFS, noise at +12 dB, impulse train, square wave at fs/4, DC step; ceiling in {-6, -3, -1, 0}.
- `true_peak_stays_within_0_1_db_of_the_ceiling` using an independent 8x oversampling reference meter (separate windowed-sinc kernel in the test) on the 0.5 fs/2 intersample-peak signal (sine at fs/4 with 45 degree phase peaks +3 dB between samples), a 0 dBFS full-scale sine at fs/4 offset by one quarter sample, and white noise; this test fails if the detector is removed (mutation check recorded in the PR).
- `signals_below_the_knee_are_transparent` (input at -12 dBFS with ceiling -1 comes out equal to the delayed input within 1e-6).
- `latency_is_exactly_lookahead_plus_detector_delay` (impulse response peak position equals `latencyFrames()` at 44.1, 48, 96, 192 kHz).
- `output_is_identical_for_block_sizes_1_7_64_333_2048` (hash of a 10 s render).
- `release_recovers_toward_unity_in_about_120ms` (gain after a 6 dB peak returns to -1 dB within 5 time constants +/- 10 %).
- `stereo_link_applies_equal_gain_to_both_channels` (a loud left and a quiet right keep their ratio).
- `nonfinite_input_is_sanitized_and_state_never_nan`; `enable_disable_crossfade_has_no_discontinuity_above_-40_dBFS` (max sample step during the toggle is bounded by the signal's own slope plus 0.01).
- `process_performs_no_heap_allocation` (extends the replaced-operator-new harness), TSan on telemetry take/store.
**Integration, `composition/tests/test_level_match.cpp`:**
- `quiet_pattern_gets_a_positive_match_and_loud_pattern_a_negative_one` (drum fixture, density 0.1 vs 0.9; the rendered pattern loudness after applying the gain is within 0.3 LU of the source, measured by the module).
- `match_gain_is_deterministic_and_rounded` (two sessions, same inputs, equal to the bit).
- `measurement_runs_on_a_worker_and_a_newer_request_cancels_the_older` (stub job clock; the stale result is discarded).
- `cache_section_roundtrip_and_ignored_when_the_key_differs` (F01 section; with F01 absent the test is skipped by a compile-time flag, not by `#if 0`).
- `silent_pattern_reports_silent_and_gain_is_zero`; `export_wav_includes_match_and_limiter_and_is_latency_compensated` (offline render with limiter on has the same length as without and the first transient lands at the same frame within 1).
- `manifest_gains_the_four_parameters_with_since_version_3_and_old_ids_are_unchanged` (host adapter golden list).
**GUI / shell (`plugin/tests/test_level_match_gui.cpp`, xvfb):** `output_button_opens_the_callout_with_sliders_and_labels` (click `Output...`, find sliders by title `Match trim` and `Ceiling`, set values, assert parameters); `level_match_toggle_disabled_without_a_pattern` then enabled after Generate; `limiter_toggle_changes_reported_latency` (`proc.getLatencySamples()` is 0, then `renderer latency` after toggle and a poll, then 0 again); `status_messages_use_the_exact_strings`; all new controls have titles and tooltips (extends F04's walk test).
**Determinism / sanitizer:** golden pattern hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 unchanged (no pattern code touched); ASan/UBSan on all new tests; TSan on `test_limiter` and the job cancellation; allocation test above.
**Manual QA:** (1) Load a loud break, Detect, Generate at density 0.2: Level match off, note the output level; switch it on: it rises to about the loop's loudness (compare with the Dry/Source mix at 100 %). (2) Raise Density to 0.9: the match gain falls. (3) Chaos 100 % with reverse/pitch/retrigger on and the limiter off: the meter clips; limiter on at -1 dB: no clip, `Limiter: -x dB` shows. (4) Toggle the limiter during playback: no click, host shows a latency change of 88 samples at 48 kHz. (5) Export WAV with both on: file peak <= -1 dBFS, loudness within 1 LU of the loop. (6) Save, reopen: the match gain is applied immediately (cache) and the label agrees.

## 8. Acceptance criteria
- [ ] With `level_match` off and `limiter_enable` off, the renderer output is bit-identical to the pre-feature renderer; the four pattern golden hashes are unchanged; old projects load with the four new parameters at their defaults.
- [ ] `measure` reproduces the BS.1770 reference values within 0.1 LU (tests above) and does not depend on caller chunking or on the sample rate of the same tone.
- [ ] After Generate with Level match on, one cycle of the pattern, rendered with the match gain, measures within 0.3 LU of the source loop unless the +/-12 dB clamp or the headroom rule applied (and then the UI says so).
- [ ] Match gain is a pure function of its key, rounded to 0.1 dB, identical on GCC, Clang, MSVC and Apple Clang (CI).
- [ ] The limiter output sample peak never exceeds the ceiling and its true peak is within ceiling + 0.1 dB on every stress signal; signals more than 6 dB below the ceiling pass unchanged (delayed).
- [ ] Limiter latency equals `latencyFrames()`, is reported to the host only while the limiter is on, and offline exports are latency-compensated with unchanged length.
- [ ] Output is identical for host block sizes 1 to 2048 with both features on; `process()` stays allocation- and lock-free (test); TSan clean.
- [ ] A new measurement never interrupts or mutes playback; stale measurement results are discarded; at most one measurement runs at a time.
- [ ] Exact status strings of section 3 are produced by the tests; every new control has a title and tooltip.
- [ ] Module `level_match` has `module.json`, README, MIGRATION.md, an example, passes `check_modules.py`, `check_includes.py`, `smoke_transfer.sh`; manifest golden list updated in the same PR sequence.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/level_match/**` (CMakeLists, module.json, README, MIGRATION, include/.../level_match.hpp, src/level_match.cpp, tests, examples) | New | loudness and match math |
| `modules/audio_renderer/include/.../limiter.hpp`, `src/limiter.cpp`, `tests/test_limiter.cpp` | New | limiter, telemetry, true-peak FIR |
| `modules/audio_renderer/include/.../renderer.hpp`, `src/renderer.cpp` | Mod **(hot)** | `RenderParams` fields, match gain ramp, limiter stage, `latencyFrames()`, offline compensation |
| `modules/plugin_host_adapter/include/.../parameters.hpp`, `src/parameters.cpp`, `tests/test_host_adapter.cpp` | Mod **(hot)** | four parameters, golden list |
| `composition/src/level_match.cpp` | New | job, status, cache section |
| `composition/include/.../project_session.hpp`, `composition/src/project_session.cpp` | Mod **(hot)** | declarations, re-measure triggers, `renderParams()` |
| `composition/CMakeLists.txt`, root `CMakeLists.txt` | Mod **(hot)** | add `level_match` to the module list before `composition` |
| `plugin/src/PluginProcessor.h/.cpp` | Mod **(hot)** | parameter poll, `RenderParams`, latency reporting |
| `plugin/src/panels/FeaturePanel.cpp` (F00; else `PluginEditor.cpp` **hot**), `plugin/src/OutputCallout.h/.cpp` | Mod/New | toggles, button, call-out |
| `plugin/tests/test_level_match_gui.cpp`, `test_plugin_shell.cpp` | New/Mod | GUI and shell tests |
| `docs/BUILD_STATUS.md`, generated module docs | Mod | status, module table |

## 10. Risks and mitigations
- **Latency change confuses a host or breaks sample-accurate alignment.** Mitigation: off by default, the latency exists only while the limiter is on, it is announced once in the status line, the manual explains it; verify PDC behavior per host in the F15 matrix (REAPER, Live, FL, Cubase, Bitwig). Fallback if a host mishandles a runtime change: report the limiter latency constantly once the limiter has been enabled in the session and bypass inside the delay (cost: fixed 1.8 ms).
- **Loudness model mismatch with perception for sparse hits.** BS.1770 gating makes a sparse pattern measure louder than its RMS suggests, so the match can be a little low for very sparse patterns. Mitigation: the trim slider and the clamp; the gate threshold is a named constant; QA listens to sparse and dense patterns.
- **The limiter hides a clipping problem the user should fix.** Mitigation: gain reduction is shown; the headroom message points at Output Gain; limiter stays optional.
- **Measurement thread safety.** Mitigation: the job copies the `Playback` shared_ptr and the source pointer, never touches the session; results are applied on the message thread through the key check; TSan test.
- **True-peak FIR coefficients transcribed wrongly.** Mitigation: the test compares against an independent reference meter; the coefficients are stored once in `limiter.cpp` with the Annex reference and a checksum test.
- **Match gain interacts with Evolve: loudness pumps as the pattern wanders.** Mitigation: 50 ms ramps, 300 ms debounce, and the gain only changes when the rounded value changes; if QA finds pumping, add hysteresis of 0.3 dB (named constant) without changing the format.
- **Cross-platform double-precision differences in K-weighting.** Mitigation: rounding to 0.1 dB absorbs ulp-level differences; the CI test compares gains across platforms on the fixture and fails only on a different rounded value (one tolerance step of 0.1 dB is reported, not failed, on borderline cases, with the fixture chosen away from a rounding boundary).

## 11. Implementation steps
1. Record the pre-change renderer hash test on unmodified code (commit alone). 2. Module `level_match`: `measure`, `computeMatch`, tests, CMake/module scaffolding, docs generation. 3. `Limiter` with true-peak FIR and tests (renderer untouched). 4. Renderer integration: `RenderParams` fields, ramped match gain, limiter stage, `latencyFrames()`, offline compensation, bit-exact-when-off test. 5. Manifest rows and host adapter tests; processor parameter plumbing and latency reporting. 6. Composition job, status, triggers, export integration, cache section (after F01) and tests. 7. FeaturePanel toggles, `Output...` call-out, messages, GUI tests. 8. Docs (BUILD_STATUS table, module docs), snapshot review, manual QA; update the roadmap README registries in the first commit of step 5.


## Implementation notes (as built, RND)

- **Detector.** The BS.1770 Annex 2 12-tap FIR underestimates broadband peaks (white noise by about 2 dB), so the limiter uses its own 32-taps-per-phase Kaiser-windowed sinc polyphase interpolator (beta 2.5, unity DC gain), 8x oversampling below 100 kHz, 4x up to 200 kHz and sample peaks above. The detector delay is 16 frames, so `L = D + 16` (88 frames at 48 kHz, 82 at 44.1 kHz, 160 at 96 kHz) and the limiter reports 1.8 ms. Aim point is 0.1 dB under the ceiling; the test measures an independent 8x/65-tap reference at no more than ceiling + 0.1 dB on a sine, noise at +12 dB, impulse train, square, DC step and an fs/4 sine at 45 degrees.
- **Offline export.** The limiter is primed (as if it had run on silence) before the render, so the first transient is limited like every other; the first `L` frames are dropped and `L` frames added at the end, length unchanged. `Renderer::primeLimiter()` is the non-real-time hook.
- **Measurement job.** `ProjectSession::updateLevelMatch(values, bpm)` runs on the calling thread (the processor's 30 Hz poll, only when the playback, level-match parameters or tempo change) instead of a dedicated `std::thread`; it is deterministic and a few tens of milliseconds for a 4-bar pattern. The result is published through an atomic read once per block. The cached-measurement state section (tag 15) is not built; nothing is persisted beyond the parameters, which carry the state.
- **UI.** No controls were added; the GUI is being rewritten elsewhere. The parameters appear in the host's generic parameter list.
