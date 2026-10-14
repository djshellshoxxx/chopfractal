# ChopFractal Advanced Section Effects: developer specifications and delivery checklist

Version 1.0 • 2026-10-13 • Status: proposed, ready for engineering review; not implemented by this document.

## 1. Scope and existing integration baseline

Enable a producer to apply editable, non-destructive effects to one chop, a subregion of a chop, or multiple contiguous/noncontiguous chops. Provide linked editing, independent instances, automation, deterministic randomization, audition and matching export. All 23 effect families specified below are in scope; priorities order delivery rather than removing advanced controls.

Repository inspection: README.md, root CMakeLists.txt, docs/specs/README.md, docs/MODULARITY_MAP.md and modules/audio_renderer/module.json were read from default branch during preparation. The modularity map is historical: use it for boundaries, not current release/CI status. Actual public headers must be inspected before implementation; proposed names below are contracts to add, not claims about existing classes.

Existing modules include chop_contracts, audio_renderer, state_codec, variation_history, source_chop, pattern_engine, recursive_hit_zoom, evolve, preset_store, plugin_host_adapter and plugin_ui_adapter. composition/ProjectSession wires modules; plugin/ is JUCE glue. Portable modules are C++17 with no JUCE types. Existing EventFx provides filter, glide and crunch. The renderer currently has an 8-voice limit, a 4096 prepared-event cap and a fixed legacy transform order. Preserve these limits until separately measured and approved changes are made.

Compatibility requirement: sessions without advanced effects must retain the old path and existing golden tests. Do not move legacy effects into a new order on load. Legacy filter/glide/crunch remain represented by their old state and compatibility backend until an explicit undoable conversion. Converting must show that chain ordering may change the sound.

## 2. Product decisions

Recommended: portable DSP processors plus a prepared selection graph, with existing renderer integration and shared offline/live execution. Alternatives considered: JUCE-only processors would couple core rendering to the shell; pre-rendering every edit would simplify realtime DSP but impair immediate automation. Use worker rendering selectively for expensive source transforms.

Default scope is event-instance processing: selecting a source chop highlights its current occurrences, but assignment targets the selected event IDs only. Provide an explicit “all uses of source chop” command. A source-chop assignment template is stored separately and materialized into event assignments; later generation applies that template only when the user enables template inheritance.

Source coordinates are integer source frames plus source asset ID. Event selection uses stable event IDs and normalized local interval [0,1]; intervals are half-open. Resolve to sample boundaries using floor(start*N), ceil(end*N), clamped to [0,N]. Empty selections cannot receive effects. Selection alone never changes audio.

Linked editing means independent DSP state sharing settings. Continuous group processing means a shared DSP state after selected events are summed. These are distinct choices. A noncontiguous continuous group receives silence during gaps, preserving musical timing. An unselected event never enters the group detector or feedback input unless explicitly chosen as a sidechain.

Overlapping assignments execute once in hierarchy order: leaf event chain, nearest nested parent group, outer selection group, global chain if added later. Each group node has a stable ID; duplicate selection membership cannot process audio twice. Reject cyclic group membership and arbitrary feedback graph edges. Reordering changes order within one chain, not hierarchy. Simultaneous independent chains sum using existing gain/pan rules.

## 3. Proposed modular architecture

| Module | Owns | Dependencies |
|---|---|---|
| effect_contracts | IDs, parameter metadata, chain/assignment records, process context, capability flags, resource estimates | chop_contracts |
| effect_dsp | DSP primitives and one isolated processor implementation per effect family | effect_contracts |
| section_effects | Selection commands, assignment validation, linked edits, immutable render-plan preparation | effect_contracts, chop_contracts |
| effect_automation | Envelope evaluation, modulation routing, seeded random streams | effect_contracts |
| Existing audio_renderer | Scheduling, voice pools, executing prepared insert/group processors, shared offline render | above portable runtime interfaces |
| Existing state_codec/preset_store | Versioned effect payloads and presets through explicit adapters | effect_contracts |
| Existing plugin_ui_adapter | Selection/effect view models, mixed values, command validation feedback | section_effects |
| composition | Wires session commands, preparation jobs, history and publication | declared modules |
| plugin panels | JUCE widgets, accessible controls, host parameter bridge | composition and adapters |

Do not create one module per knob. Effect implementations live in separate headers/sources inside effect_dsp; split a family into a separate portable module only when its dependency/backend or transfer needs justify it. Every new independent module gets module.json, standalone-capable CMake, public headers, focused tests, example and MIGRATION.md. Update portability tooling manifests and root build ordering.

### 3.1 Runtime contracts

Use typed EffectTypeId, EffectInstanceId, ChainId, AssignmentId, EventId and AssetId; never use display strings as identity. Persist stable textual type/parameter IDs, not enum ordinals. Each processor provides:
- prepare(ProcessSpec, validated configuration): worker/control thread; owns buffers, may fail with structured error.
- process(AudioSpan, ProcessContext, ParameterSpan) noexcept: audio thread; fixed prepared resources, no allocation/locks/I/O/throwing.
- reset(ResetReason) noexcept: clears runtime state without allocation.
- latencySamples(), tailDescriptor(), capabilities(), resourceUsage(): prepared metadata.
Source transforms instead implement prepareSourceTransform(SourceView, RegionMap, configuration, cancellationToken) and expose a prepared source reader. Do not force reverse/warp into causal insert APIs.

ProcessContext includes absolute sample timestamp, local selection frame, sample rate, validated tempo/PPQ validity, transport discontinuity, input/sidechain spans and event/instance seed. AudioSpan has explicit channel/sample counts and lifetime. Buffers and DSP objects are uniquely owned by the prepared graph. No UI object or mutable session pointer may enter DSP.

Worker publishes a complete validated prepared plan through the existing mailbox pattern. Audio thread acknowledges replaced plans; reclamation and destruction happen off audio thread. Stale preparation results cannot overwrite a newer session revision. Cancellation preserves the previous audible plan. Control edits may coalesce on worker; undo history remains complete.

### 3.2 Signal flow and scheduling

Source region -> source transforms -> legacy event transforms in original order -> advanced event insert chain -> existing event gain/pan -> group sums/chains inside-out -> existing dry mix -> existing output gain.

This location deliberately leaves current dry-source routing intact. Advanced effects act on the generated/chopped branch; users wanting the original input processed must choose an explicit input target in a future routing feature. Do not silently change current dry-mix meaning.

A source transform card occupies the source-stage lane in the same drawer. It cannot be dragged after an insert. Explain the placement in its tooltip. Multiple source transforms execute top-to-bottom as a composed source mapping or prepared intermediate render; mappings and caches are revision keyed.

Subregion inserts keep the full event timing: route the selected interval through the effect and crossfade the result into the untouched event. Process silence outside the interval for tails when enabled. Default boundary fade 2 ms, clamped to half interval duration. Tail modes: cut, fade (default 10 ms), spill. Spill continues processor state with zero new input; it does not repeat source input.

At group scope, preserve timeline gaps. At independent scope, each event occurrence owns independent state. Voice stealing follows existing deterministic policy and releases associated effect state without allocation. Detached tails use a prepared bounded tail pool: default 16, maximum 32. When full, fade the quietest tail over 5 ms; ties break by oldest timestamp then stable ID. Freeze tails require the same bounded pool.

Limits: at most 8 processors per chain, 8 taps/voices where specified, 32 group chains, 256 saved chains and 4096 assignments per session. These are product ceilings, not guaranteed simultaneous realtime capacity. Initial realtime budget is 64 active processor instances, including detached tails; activation priority is event start then stable ID. Plan validation rejects known over-budget playback; unexpected runtime overflow bypasses only the newest unavailable instance with a telemetry flag. Never substitute random voice dropping.

### 3.3 Latency and tails

Latency is the maximum compensated path through the prepared graph. Align parallel wet/dry, unaffected events and group branches with preallocated delay compensation. Convert delay buffers on worker. Host latency must not change mid-callback. Structural edits requiring a new host latency are queued while playing and visibly marked pending; commit at stop or explicit user-requested transport restart. Report via shell host notification outside audio processing. Offline export compensates leading latency and includes selected tail policy.

Finite tails end at both -90 dBFS for 100 ms and a declared algorithm cap. Default export tail cap 30 s, user 0..120 s. Freeze is infinite in metadata; export requires the finite cap and displays it. Hard Stop clears voices/tails; ordinary stop uses selected tail policy. Seek, loop wrap and audition reset rules are explicit: seek clears; loop wrap preserves only continuous groups configured to carry tails; event-local modulation always follows its event retrigger policy.

## 4. Common controls and parameter rules

All effects inherit enabled=true, inputGainDb=-24..24 default 0, outputGainDb=-24..24 default 0, wet=0..1 default 1, mixLaw=linear/equal-power default linear, bypassFadeMs=1..20 default 5. They are inactive until added. Default per-effect parameters need not be neutral; an empty chain is exact legacy bypass. Linear mix is (1-w)*dry+w*wet with latency alignment; equal-power is explicit because correlated signals can gain level.

Continuous gains/mix/pan smooth over 5 ms; frequency/Q use 10 ms multiplicative/appropriate-domain smoothing; delay time uses 20 ms glide or 20 ms dual-read crossfade. Sample-accurate internal automation is evaluated in segments at event/envelope boundaries. Block size must not change event timing. Booleans crossfade affected audio where relevant.

Tables below list unique stable parameter suffixes; persisted namespace is typeId.parameterId. Numeric floats are continuous/automatable unless described otherwise. Integers/enums use discrete automation only when a prepared backend can apply them without latency/resource changes. Structural fields (algorithm, FFT/oversampling/quality, voice count/pool size, IR asset/trim/stretch, sidechain routing, warp markers and topology) are session edits prepared off-thread and are not host-automatable. Momentary commands are timestamped commands, never persistent “pressed” booleans. Read-only meters are not state parameters.

Ranges are product limits, clamped further to valid sample-rate bandwidth (cutoffs ≤0.45*sampleRate). Invalid nonfinite values are rejected on load/edit, not propagated. Logarithmic UI for frequency/time/ratio; linear UI for dB, pan and normalized amounts; explicit unit-aware text entry and double-click default. Defaults and valid ranges for dynamic collections apply to every active element; inactive entries retain settings.

Tempo divisions span 1/64 note through 4 bars unless narrowed; bar length uses time signature. Dotted/triplet selectors multiply duration by 1.5 or 2/3. Free-time controls retained when sync is enabled. Invalid host tempo uses saved session tempo, default 120 BPM; UI displays the fallback. Scale harmony is interval-by-degree relative to the configured root/scale, not automatic polyphonic key detection.

Feedback networks normalize combined tap/cross-channel sends so their small-signal loop gain remains below the configured feedback cap. Saturation/limiting and nonfinite guards are additional containment, not a substitute for a stable feedback matrix. Freeze uses dedicated bounded gain control.

## 5. Selection editor and user interaction

Advanced Effects drawer contains selection summary, source-stage lane, insert chain, expanded controls and automation lanes. Main waveform remains usable. Drag selects a time interval; Ctrl-click toggles events; Shift extends; Escape clears; keyboard equivalents supported. Noncontiguous selection shows count, total selected duration and elapsed span separately.

Shared edit defaults to absolute set for selected compatible instances. Relative mode applies additive changes in displayed units (dB, ms, semitones), multiplicative frequency changes in octaves, and clamps per instance. Mixed fields show an explicit mixed state until edited. One gesture is one undo transaction. Linked groups use shared settings IDs but distinct DSP states; “make independent” clones settings with fresh IDs.

Chain cards expose enable, mix, primary controls and menu. Expanded views use Tone, Timing, Modulation, Dynamics and Advanced tabs only as applicable. No hidden required controls. Add dropdown groups effects by function. Unsupported controls are disabled with a reason, never silent no-ops. A/B stores complete chain state and envelopes; switching uses prepared crossfade. Copy/paste creates fresh instance IDs while preserving parameter/envelope values. Presets support effect/chain and optional automation; asset references handled explicitly.

Audition offers green Play, red Stop, loop selection and context playback. Tail controls are adjacent to selection processing mode. Scope is always visible. Every non-obvious control has what/why/how tooltip, units, reset default and keyboard-accessible help. Tooltips, status messages and effect controls do not obstruct dragging. Status view scrolls; dismissible messages stay dismissible.

Meters are telemetry snapshots at ≤30 Hz; no audio-thread GUI calls. Accessibility includes logical tab order, screen-reader labels with units/mixed state, keyboard reorder, scalable layout and adequate contrast.

## 6. Automation, generation and nested rhythm integration

Store envelope points as local normalized time or absolute source/beat time with explicit timeBasis. Up to 256 points per lane, 32 lanes per chain, 8 LFOs and 8 macro controls per session. Curve choices step/linear/smooth; clamp duplicate positions deterministically with last edit winning. Scope chooses per-event replay versus selection-span envelope including gaps.

Modulation order: base value -> envelope -> summed bounded LFO/envelope follower/step offsets -> macro offsets -> final physical clamp/smoothing. Each routing has sourceId, targetInstanceId/parameterId, depth, polarity and min/max. No modulation cycles. A maximum of 8 incoming routes per parameter is validated.

LFO controls: free 0.01..40 Hz default 1, sync/division, sine/triangle/square/saw/seeded-random, depth, phase 0..1, retrigger event/selection/free. Follower controls: attack 0.1..500 ms default 10, release 1..3000 default 100, filtered self or explicit sidechain, depth/polarity. Step controls: 1..32 steps, normalized values, sync division, smoothing 0..50 ms. Macros have name, 0..1 default 0 and explicit target ranges.

Generation keeps existing RNG streams untouched. New randomness derives from stable session seed, event ID, instance ID, parameter ID and pass index. Randomization offers bounds, distributions uniform/log-uniform where meaningful, parameter locks and trigger once/per-event/per-playback. Persist seed/pass index for replay; export uses a captured pass index. Generation does not alter effect settings unless effect randomization is explicitly enabled.

Recursive Hit Zoom: child events own leaf chains; parent effects process the child sum once. Parent-local envelopes span the parent slot, child-local envelopes restart per child. Mutation preserves assignments on surviving stable IDs; newly created events inherit only explicit templates. Deleted-event assignments are removed in the same undoable transaction. Variation history stores effect state and asset references alongside the pattern.

Determinism: identical seeds must preserve event/automation schedules across platforms. Preserve existing cross-platform pattern hashes. Floating DSP output can differ across CPUs/backends; use declared numeric tolerances, not a new unsupported bit-identical-audio promise.

## 7. Persistence, host automation and exports

Version effect payload independently in state_codec, using validated counts/lengths and CRC conventions. Store chains, assignments, setting links, type versions, all parameters, envelopes, routes, seeds, group membership and asset references. Do not serialize live delay buffers by default; restore begins with empty runtime tails. Freeze recall stores its captured source/frame specification or explicit rendered asset, not an unbounded memory dump.

Unknown effect types remain round-trippable opaque records and render bypassed with visible status. Corrupt individual payloads fail with structured diagnostics and preserve the last valid session. Load validates file sizes, reference integrity and resource budgets before publication. Existing sessions with no payload load unchanged.

Host automation uses append-only fixed IDs. Do not dynamically expose arbitrary per-event parameter IDs. Add 8 fixed Advanced Macro host parameters, retaining existing IDs unchanged; macros map to saved per-section targets. Existing global controls retain semantics. All per-effect controls support internal automation where indicated; tooltip distinguishes internal from host automation. Undo on message thread; host automation gestures use begin/end edit from the shell.

Offline WAV, audition and realtime use the same prepared graph/DSP. WAV includes section effects and tail policy. Producer Kit slice WAVs include independent source/event effects; continuous group effects cannot be represented by isolated slices, so export includes an additional full effected-pattern WAV plus a manifest explaining slice limitations. MIDI contains timing/notes only, never claims to carry audio effects. Include source sample rate, engine/effect versions, seed/pass index and policy in export metadata/manifest.

Freeze cache keys include source content hash, selection mapping, chain order/settings, automation, tempo map, sample rate, quality and backend version. Worker cancellation, invalidation and asset deletion are reference-counted off-thread. Cache is an optimization: project state remains sufficient to rebuild.

## 8. Effect specifications

Every family below inherits sections 2–7 and 9–10. Each implementation owns its DSP runtime exclusively. Event/group insert families expose primary controls on their card and all listed fields in the expanded editor; source-transform families use the source-stage lane. Every listed control must be implemented, tested, or explicitly marked unavailable in the UI and delivery checklist; no empty callbacks.


### FX01 — Delay and echo

**Stable type ID:** `advanced.delay.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/delay/` (proposed path). 

**Processing contract:** Stateful per-instance delay lines; digital/tape use interpolated reads, ping-pong uses a stereo feedback matrix, multi-tap supports 8 taps. Feedback filtering precedes saturation. Freeze stops new input and uses bounded feedback.

| Parameter ID | Range / options and default |
|---|---|
| `mode` | enum digital/tape/ping-pong/multi-tap = digital |
| `timeMs` | 1..4000 ms = 250 |
| `sync` | bool = false |
| `division` | enum 1/64..4 bars plus dotted/triplet = 1/8 |
| `leftRightLink` | bool = true |
| `rightTimeMs` | 1..4000 ms = 250 |
| `feedback` | 0..0.98 = 0.35 |
| `wetLevelDb` | -60..6 dB = 0 |
| `width` | 0..2 = 1 |
| `pan` | -1..1 = 0 |
| `crossFeedback` | 0..1 = 0 |
| `highPassHz` | 20..20000 Hz = 20 |
| `lowPassHz` | 20..20000 Hz = 12000 |
| `resonanceQ` | 0.5..4 = 0.707 |
| `driveDb` | 0..24 dB = 0 |
| `modulationRateHz` | 0.01..10 Hz = 0.3 |
| `modulationDepthMs` | 0..10 ms = 0 |
| `wowDepthMs` | 0..10 ms = 0 |
| `flutterDepthMs` | 0..2 ms = 0 |
| `duckAmountDb` | 0..36 dB = 0 |
| `duckThresholdDb` | -60..0 dBFS = -24 |
| `duckAttackMs` | 0.1..100 ms = 5 |
| `duckReleaseMs` | 10..2000 ms = 200 |
| `timeChangeMode` | enum crossfade/glide = crossfade |
| `freeze` | bool = false |
| `clear` | momentary command |
| `tapCount` | 1..8 integer = 1 |
| `tapTimeMs[i]` | 1..4000 ms = 250 |
| `tapLevelDb[i]` | -60..6 dB = 0 |
| `tapPan[i]` | -1..1 = 0 |
| `tapEnabled[i]` | bool = true |
| `tapFeedbackSend[i]` | 0..1 = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Impulse repeats occur at expected samples ±1; feedback amplitudes follow geometric decay within 0.1 dB; ping-pong alternates channels; clear/freeze/tail transitions are click-controlled.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX02 — Reverb

**Stable type ID:** `advanced.reverb.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/reverb/` (proposed path). 

**Processing contract:** Algorithmic room/hall/plate/chamber share a bounded feedback-delay network. Optional convolution is a separate backend with asynchronous IR preparation and partitioned convolution.

| Parameter ID | Range / options and default |
|---|---|
| `algorithm` | enum room/hall/plate/chamber/convolution = room |
| `preDelayMs` | 0..250 ms = 20 |
| `decaySeconds` | 0.1..30 s = 1.5 |
| `size` | 0..1 = 0.5 |
| `earlyDb` | -60..6 dB = -6 |
| `lateDb` | -60..6 dB = 0 |
| `diffusion` | 0..1 = 0.7 |
| `lowDampingHz` | 20..1000 Hz = 200 |
| `highDampingHz` | 1000..20000 Hz = 6000 |
| `lowDecayRatio` | 0.25..2 = 1 |
| `highDecayRatio` | 0.1..2 = 0.5 |
| `inputHighPassHz` | 20..20000 Hz = 20 |
| `inputLowPassHz` | 20..20000 Hz = 18000 |
| `width` | 0..2 = 1 |
| `modulationRateHz` | 0.01..5 Hz = 0.3 |
| `modulationDepthMs` | 0..5 ms = 0 |
| `duckAmountDb` | 0..36 dB = 0 |
| `duckAttackMs` | 0.1..100 ms = 5 |
| `duckReleaseMs` | 10..2000 ms = 200 |
| `freeze` | bool = false |
| `impulseAsset` | asset reference = none |
| `impulseTrimStartMs` | 0..10000 ms = 0 |
| `impulseTrimEndMs` | 1..10000 ms = asset duration |
| `impulseStretch` | 0.25..4 = 1 |
| `impulseNormalize` | bool = false |
| `impulseGainDb` | -24..24 dB = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Measure finite decay and bounded freeze; predelay matches ±1 sample; missing IR retains state and reports an error without audio-thread file access; convolution matches direct convolution on short reference signals.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX03 — Filter

**Stable type ID:** `advanced.filter.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/filter/` (proposed path). 

**Processing contract:** Extract or adapt existing EventFx filter without changing legacy rendering. New SVF backend supports continuous filters; comb uses its own delay primitive.

| Parameter ID | Range / options and default |
|---|---|
| `type` | enum low-pass/high-pass/band-pass/notch/peak/low-shelf/high-shelf/comb = low-pass |
| `frequencyHz` | 20..20000 Hz = 1000 |
| `resonanceQ` | 0.1..20 = 0.707 |
| `slopeDbOct` | enum 12/24/36/48 = 12 |
| `gainDb` | -24..24 dB = 0 |
| `driveDb` | 0..24 dB = 0 |
| `compensate` | bool = false |
| `envelopeAmountOct` | -6..6 oct = 0 |
| `envelopeAttackMs` | 0.1..500 ms = 10 |
| `envelopeReleaseMs` | 1..3000 ms = 100 |
| `lfoRateHz` | 0.01..40 Hz = 1 |
| `lfoDepthOct` | 0..6 oct = 0 |
| `lfoWave` | enum sine/triangle/saw/square/random = sine |
| `lfoSync` | bool = false |
| `lfoDivision` | enum 1/64..4 bars = 1/4 |
| `lfoPhase` | 0..1 cycle = 0 |
| `stereoOffsetOct` | -2..2 oct = 0 |
| `combFeedback` | -0.98..0.98 = 0.5 |
| `combDampingHz` | 20..20000 Hz = 8000 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Frequency response matches documented type/slope; cutoff clamped below Nyquist; rapid cutoff/Q sweeps remain finite; legacy filter fixtures remain unchanged.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX04 — Parametric and dynamic EQ

**Stable type ID:** `advanced.eq.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/eq/` (proposed path). 

**Processing contract:** Eight independent biquad bands; optional detector and bounded dynamic gain per band. Spectrum analyzer runs from decimated telemetry on a worker.

| Parameter ID | Range / options and default |
|---|---|
| `bandEnabled[i]` | bool = false |
| `bandType[i]` | enum bell/low-shelf/high-shelf/low-cut/high-cut/notch = bell |
| `frequencyHz[i]` | 20..20000 Hz = 1000 |
| `gainDb[i]` | -24..24 dB = 0 |
| `q[i]` | 0.1..24 = 0.707 |
| `slopeDbOct[i]` | enum 12/24/36/48 = 12 |
| `channelMode[i]` | enum stereo/left/right/mid/side = stereo |
| `bandAudition[i]` | bool = false |
| `analyzerEnabled` | bool = true |
| `analyzerFloorDb` | -120..-30 dB = -90 |
| `dynamicEnabled[i]` | bool = false |
| `dynamicThresholdDb[i]` | -80..0 dBFS = -24 |
| `dynamicRangeDb[i]` | -24..24 dB = -6 |
| `dynamicAttackMs[i]` | 0.1..500 ms = 10 |
| `dynamicReleaseMs[i]` | 5..3000 ms = 100 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Disabled bands null against input; band response within 0.2 dB of reference; mid/side encode-decode preserves stereo; analyzer never blocks processing; dynamic gain stays within range.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX05 — Distortion, saturation and wavefolding

**Stable type ID:** `advanced.distortion.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/distortion/` (proposed path). 

**Processing contract:** Nonlinear processor with selectable curves and pre/post tone stages; oversampling backend prepared outside processing. DC blocker follows asymmetric stages.

| Parameter ID | Range / options and default |
|---|---|
| `type` | enum soft-clip/hard-clip/tape/tube/rectify/wavefold = soft-clip |
| `driveDb` | 0..48 dB = 6 |
| `thresholdDb` | -36..0 dBFS = -6 |
| `hardness` | 0..1 = 0.5 |
| `bias` | -1..1 = 0 |
| `asymmetry` | -1..1 = 0 |
| `foldAmount` | 1..16 = 1 |
| `preHighPassHz` | 20..20000 Hz = 20 |
| `preLowPassHz` | 20..20000 Hz = 20000 |
| `postHighPassHz` | 20..20000 Hz = 20 |
| `postLowPassHz` | 20..20000 Hz = 16000 |
| `tone` | -1..1 = 0 |
| `dcBlock` | bool = true |
| `oversampling` | enum 1x/2x/4x/8x = 2x |
| `autoCompensate` | bool = false |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Curves match reference transfer functions; DC blocker removes steady offset; oversampling changes report latency correctly; compensation is bounded and does not imply peak normalization.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX06 — Bitcrusher and rate reduction

**Stable type ID:** `advanced.bitcrusher.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/bitcrusher/` (proposed path). 

**Processing contract:** Reuse legacy crunch through compatibility mode. Advanced backend uses sample-hold and quantization with seeded jitter/dither.

| Parameter ID | Range / options and default |
|---|---|
| `bitDepth` | 1..24 integer = 12 |
| `reductionFactor` | 1..128 = 1 |
| `quantization` | enum round/truncate = round |
| `dither` | enum off/rectangular/TPDF = off |
| `jitter` | 0..0.5 = 0 |
| `stereoClockLink` | bool = true |
| `antiAlias` | bool = false |
| `smoothing` | 0..1 = 0 |
| `gateThresholdDb` | -120..-20 dBFS = -120 |
| `driveDb` | 0..24 dB = 0 |
| `seed` | uint32 = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Known inputs map to expected quantization levels; linked clocks align; same seed repeats jitter/dither on the supported build; legacy crunch golden tests stay unchanged.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX07 — Compressor

**Stable type ID:** `advanced.compressor.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/compressor/` (proposed path). 

**Processing contract:** Feed-forward gain computer with peak/RMS detector, soft knee, optional lookahead and filtered detector source.

| Parameter ID | Range / options and default |
|---|---|
| `thresholdDb` | -60..0 dBFS = -18 |
| `ratio` | 1..20 = 4 |
| `attackMs` | 0.05..200 ms = 10 |
| `releaseMs` | 5..3000 ms = 100 |
| `autoRelease` | bool = false |
| `kneeDb` | 0..24 dB = 6 |
| `makeupDb` | -12..24 dB = 0 |
| `detector` | enum peak/RMS = peak |
| `rmsWindowMs` | 1..100 ms = 10 |
| `lookaheadMs` | 0..20 ms = 0 |
| `detectorHighPassHz` | 20..20000 Hz = 20 |
| `detectorLowPassHz` | 20..20000 Hz = 20000 |
| `stereoLink` | 0..1 = 1 |
| `sidechain` | enum self/plugin-input/external = self |
| `gainReductionMeter` | read-only dB |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Steady-state transfer follows ratio/knee within 0.2 dB; attack/release reference step tests pass; no stereo drift at full link; missing external sidechain uses silence and shows status.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX08 — Gate and expander

**Stable type ID:** `advanced.gate.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/gate/` (proposed path). 

**Processing contract:** Shared detector primitive with compressor, separate hysteretic gate state machine and gain computer.

| Parameter ID | Range / options and default |
|---|---|
| `mode` | enum gate/expander = gate |
| `thresholdDb` | -90..0 dBFS = -40 |
| `ratio` | 1..20 = 4 |
| `rangeDb` | 0..100 dB = 60 |
| `attackMs` | 0.05..200 ms = 1 |
| `holdMs` | 0..1000 ms = 20 |
| `releaseMs` | 1..3000 ms = 100 |
| `hysteresisDb` | 0..24 dB = 3 |
| `lookaheadMs` | 0..20 ms = 0 |
| `detectorHighPassHz` | 20..20000 Hz = 20 |
| `detectorLowPassHz` | 20..20000 Hz = 20000 |
| `stereoLink` | 0..1 = 1 |
| `sidechain` | enum self/plugin-input/external = self |
| `detectorListen` | bool = false |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Threshold chatter prevented by hysteresis; hold time matches configured samples; range limits attenuation; lookahead preserves onset and aligns dry path.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX09 — Transient shaper

**Stable type ID:** `advanced.transient.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/transient/` (proposed path). 

**Processing contract:** Fast/slow envelope comparison drives bounded attack/sustain gain; optional two-band mode uses phase-consistent crossover and recombination.

| Parameter ID | Range / options and default |
|---|---|
| `attackDb` | -24..24 dB = 0 |
| `sustainDb` | -24..24 dB = 0 |
| `sensitivity` | 0..1 = 0.5 |
| `attackWindowMs` | 0.5..100 ms = 10 |
| `sustainTimeMs` | 10..2000 ms = 150 |
| `detectorHighPassHz` | 20..20000 Hz = 20 |
| `detectorLowPassHz` | 20..20000 Hz = 20000 |
| `maxGainChangeDb` | 0..24 dB = 12 |
| `stereoLink` | 0..1 = 1 |
| `multiband` | bool = false |
| `crossoverHz` | 80..5000 Hz = 300 |
| `lowAttackDb` | -24..24 dB = 0 |
| `lowSustainDb` | -24..24 dB = 0 |
| `highAttackDb` | -24..24 dB = 0 |
| `highSustainDb` | -24..24 dB = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Zero settings produce unity; impulse attacks and sustained tones respond independently; crossover recombination meets chosen response reference; all gain changes remain bounded.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX10 — Limiter and clipper

**Stable type ID:** `advanced.limiter.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/limiter/` (proposed path). 

**Processing contract:** Per-section limiting is creative processing, not an implicit master limiter. True-peak backend uses documented interpolation; declare its measured ceiling tolerance.

| Parameter ID | Range / options and default |
|---|---|
| `mode` | enum limiter/soft-clip/hard-clip = limiter |
| `driveDb` | 0..36 dB = 0 |
| `ceilingDb` | -24..0 dBFS = -1 |
| `lookaheadMs` | 0..20 ms = 5 |
| `releaseMs` | 5..2000 ms = 100 |
| `autoRelease` | bool = false |
| `softness` | 0..1 = 0.5 |
| `stereoLink` | 0..1 = 1 |
| `truePeak` | bool = false |
| `oversampling` | enum 1x/2x/4x/8x = 4x |
| `reductionMeter` | read-only dB |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Sample ceiling tolerance ≤0.1 dB in limiter mode; true-peak mode ≤0.3 dB on standardized stress fixtures at supported rates; clipping harmonics and latency match configuration.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX11 — Chorus

**Stable type ID:** `advanced.chorus.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/chorus/` (proposed path). 

**Processing contract:** Up to eight modulated delay voices; independent stereo phases and normalized voice summation.

| Parameter ID | Range / options and default |
|---|---|
| `baseDelayMs` | 1..40 ms = 15 |
| `rateHz` | 0.01..20 Hz = 0.8 |
| `sync` | bool = false |
| `division` | enum 1/64..4 bars = 1/4 |
| `depthMs` | 0..15 ms = 3 |
| `voices` | 1..8 integer = 2 |
| `detuneCents` | 0..30 cents = 0 |
| `spread` | 0..1 = 0.5 |
| `waveform` | enum sine/triangle/random = sine |
| `stereoPhase` | 0..1 cycle = 0.25 |
| `width` | 0..2 = 1 |
| `feedback` | -0.9..0.9 = 0 |
| `highPassHz` | 20..20000 Hz = 20 |
| `lowPassHz` | 20..20000 Hz = 16000 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Depth never produces negative delay; voice count changes crossfade; stereo phase is measurable; extreme feedback remains finite.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX12 — Flanger

**Stable type ID:** `advanced.flanger.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/flanger/` (proposed path). 

**Processing contract:** Short modulated delay and feedback; through-zero backend delays the reference path and explicitly reports latency.

| Parameter ID | Range / options and default |
|---|---|
| `baseDelayMs` | 0.05..20 ms = 2 |
| `rateHz` | 0.01..20 Hz = 0.2 |
| `sync` | bool = false |
| `division` | enum 1/64..4 bars = 1/2 |
| `depthMs` | 0..10 ms = 1 |
| `feedback` | -0.95..0.95 = 0.5 |
| `feedbackPolarity` | enum normal/inverted = normal |
| `waveform` | enum sine/triangle = sine |
| `phase` | 0..1 cycle = 0 |
| `stereoPhase` | 0..1 cycle = 0.25 |
| `manualPosition` | 0..1 = 0.5 |
| `modulationEnabled` | bool = true |
| `throughZero` | bool = false |
| `dampingHz` | 20..20000 Hz = 16000 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Comb notch spacing matches delay; feedback sign and polarity combine once; through-zero crossing is continuous; normal mode bounds depth to available delay.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX13 — Phaser

**Stable type ID:** `advanced.phaser.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/phaser/` (proposed path). 

**Processing contract:** Cascade of all-pass stages, bounded feedback and LFO; manual mode fixes sweep position.

| Parameter ID | Range / options and default |
|---|---|
| `stages` | enum 2/4/6/8/12 = 6 |
| `minimumHz` | 20..10000 Hz = 200 |
| `maximumHz` | 20..20000 Hz = 4000 |
| `rateHz` | 0.01..20 Hz = 0.3 |
| `sync` | bool = false |
| `division` | enum 1/64..4 bars = 1/2 |
| `depth` | 0..1 = 1 |
| `feedback` | -0.95..0.95 = 0.3 |
| `spacing` | 0..1 = 0.5 |
| `waveform` | enum sine/triangle = sine |
| `phase` | 0..1 cycle = 0 |
| `stereoPhase` | 0..1 cycle = 0.25 |
| `manualPosition` | 0..1 = 0.5 |
| `modulationEnabled` | bool = true |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** All-pass stages preserve magnitude before mixing/feedback; min≤max validation; stage changes crossfade; sweep and feedback remain stable.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX14 — Tremolo, autopan and rhythmic volume

**Stable type ID:** `advanced.tremolo.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/tremolo/` (proposed path). 

**Processing contract:** One reusable modulation generator drives gain/pan; 32-step pattern maximum with interpolation and sample-aligned retrigger.

| Parameter ID | Range / options and default |
|---|---|
| `mode` | enum tremolo/autopan/both = tremolo |
| `rateHz` | 0.01..40 Hz = 4 |
| `sync` | bool = true |
| `division` | enum 1/64..4 bars = 1/8 |
| `depth` | 0..1 = 0.5 |
| `waveform` | enum sine/triangle/square/saw/custom = sine |
| `dutyCycle` | 0.01..0.99 = 0.5 |
| `smoothingMs` | 0..50 ms = 2 |
| `phase` | 0..1 cycle = 0 |
| `retrigger` | enum chop/selection/free = chop |
| `panCentre` | -1..1 = 0 |
| `panWidth` | 0..1 = 1 |
| `panLaw` | enum linear/equal-power = equal-power |
| `stepCount` | 1..32 integer = 16 |
| `stepGain[i]` | 0..1 = 1 |
| `stepPan[i]` | -1..1 = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Zero depth preserves audio; step boundaries match musical positions; smoothing prevents discontinuities when enabled; equal-power pan follows reference law.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX15 — Pitch shifter and harmonizer

**Stable type ID:** `advanced.pitch.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/pitch/` (proposed path). 

**Processing contract:** Source transform when duration changes; insert backend when preserving duration. Shared pitch backend with granular/warp only through a narrow interface; licenses and quality validated before selection.

| Parameter ID | Range / options and default |
|---|---|
| `semitones` | -24..24 st = 0 |
| `cents` | -100..100 cents = 0 |
| `preserveDuration` | bool = true |
| `quality` | enum draft/standard/high = standard |
| `preserveFormants` | bool = false |
| `formantSemitones` | -12..12 st = 0 |
| `transientPreservation` | 0..1 = 0.5 |
| `voiceCount` | 1..4 integer = 1 |
| `voiceSemitones[i]` | -24..24 st = 0 |
| `voiceLevelDb[i]` | -60..6 dB = 0 |
| `voicePan[i]` | -1..1 = 0 |
| `voiceDelayMs[i]` | 0..100 ms = 0 |
| `harmonyMode` | enum chromatic/scale = chromatic |
| `root` | enum C..B = C |
| `scale` | enum major/minor/dorian/mixolydian/pentatonic/custom = minor |
| `customScaleMask` | 12-bit mask = natural minor |
| `intervalDegrees[i]` | -14..14 integer = 0 |
| `pitchFeedback` | 0..0.95 = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps. Duration-changing pitch mode also uses the source-stage contract; preserve-duration mode stays an insert. UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Sine pitch within 5 cents after settling; preserve-duration output length exact; voice normalization documented; transient and vocal reference audition; mode changes never silently move later chops.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX16 — Frequency shifter and ring modulator

**Stable type ID:** `advanced.frequency.v1`. **Delivery:** P2. **Implementation:** `effect_dsp/frequency/` (proposed path). 

**Processing contract:** Quadrature/Hilbert frequency-shift path and separate carrier multiplication path; ring mode does not pretend to expose independent sidebands.

| Parameter ID | Range / options and default |
|---|---|
| `mode` | enum frequency-shift/ring-mod = frequency-shift |
| `frequencyHz` | -10000..10000 Hz = 0 |
| `fineHz` | -10..10 Hz = 0 |
| `carrierWave` | enum sine/triangle/square/saw = sine |
| `upperLevelDb` | -60..6 dB = 0 |
| `lowerLevelDb` | -60..6 dB = -60 |
| `stereoOffsetHz` | -1000..1000 Hz = 0 |
| `lfoRateHz` | 0.01..40 Hz = 1 |
| `lfoDepthHz` | 0..5000 Hz = 0 |
| `envelopeAmountHz` | -5000..5000 Hz = 0 |
| `envelopeAttackMs` | 0.1..500 ms = 10 |
| `envelopeReleaseMs` | 1..3000 ms = 100 |
| `feedback` | -0.9..0.9 = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Sine sidebands occur at expected sum/difference frequencies; negative shift direction verified; quadrature unwanted-sideband rejection ≥40 dB over declared passband; carrier frequency clamped to supported bandwidth.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX17 — Granular processor

**Stable type ID:** `advanced.granular.v1`. **Delivery:** P3. **Implementation:** `effect_dsp/granular/` (proposed path). 

**Processing contract:** Read-only source-backed grain scheduler with fixed pool of 64 grains; deterministic per-event RNG, voice stealing and bounded density.

| Parameter ID | Range / options and default |
|---|---|
| `grainSizeMs` | 2..500 ms = 60 |
| `densityHz` | 0.1..200 Hz = 20 |
| `position` | 0..1 = 0.5 |
| `spray` | 0..1 = 0 |
| `speed` | -4..4 = 1 |
| `pitchSemitones` | -24..24 st = 0 |
| `pitchRandomCents` | 0..1200 cents = 0 |
| `envelope` | enum Hann/triangle/trapezoid = Hann |
| `overlap` | 0..0.95 = 0.5 |
| `densityMode` | enum density/overlap = density |
| `reverseProbability` | 0..1 = 0 |
| `stereoSpread` | 0..1 = 0.5 |
| `freeze` | bool = false |
| `maxVoices` | 1..64 integer = 32 |
| `seed` | uint32 = 0 |
| `boundaryMode` | enum clamp/wrap/neighbour = clamp |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  Grain reads use the source-access capability; this processor is unavailable on an arbitrary group sum unless a bounded group capture is prepared. UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Same seed produces same grain schedule; no reads beyond source; enforce pool limit and deterministic stealing; overlap mode derives density from grain duration; neighbour mode requires explicit user selection.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX18 — Stutter, retrigger and beat repeat

**Stable type ID:** `advanced.stutter.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/stutter/` (proposed path). 

**Processing contract:** Use prepared event expansion where possible; bounded realtime retrigger path for automation. Maximum 64 repeats per trigger and global prepared-event cap remains enforced.

| Parameter ID | Range / options and default |
|---|---|
| `lengthMs` | 1..2000 ms = 125 |
| `sync` | bool = true |
| `division` | enum 1/64..1 bar = 1/16 |
| `repeatCount` | 1..64 integer = 4 |
| `triggerPosition` | 0..1 = 0 |
| `quantization` | enum off/1/64/1/32/1/16/1/8/1/4/bar = off |
| `direction` | enum forward/reverse/alternate = forward |
| `gate` | 0.01..1 = 1 |
| `fadeInMs` | 0..50 ms = 2 |
| `fadeOutMs` | 0..50 ms = 2 |
| `pitchPerRepeat` | -12..12 st = 0 |
| `gainPerRepeatDb` | -24..6 dB = 0 |
| `acceleration` | 0.25..4 = 1 |
| `probability` | 0..1 = 1 |
| `seed` | uint32 = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Repeat count/timestamps exact; fades never exceed fragment duration; gate and acceleration respect minimum one-sample fragments; probability deterministic; caps reject oversized expansion without partial edits.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX19 — Reverse and tape stop/start

**Stable type ID:** `advanced.reverse.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/reverse/` (proposed path). 

**Processing contract:** Extend existing source traversal and glide via compatibility wrapper. This is a source transform with random access, not a generic causal audio insert.

| Parameter ID | Range / options and default |
|---|---|
| `mode` | enum reverse/tape-stop/tape-start/stop-start = reverse |
| `reverseWindowMs` | 1..10000 ms = 250 |
| `reverseWholeSelection` | bool = true |
| `stopMs` | 1..10000 ms = 500 |
| `startMs` | 1..10000 ms = 500 |
| `curve` | enum linear/exponential/custom = exponential |
| `initialSpeed` | 0..4 = 1 |
| `targetSpeed` | 0..4 = 0 |
| `holdMs` | 0..10000 ms = 0 |
| `preservePitch` | bool = false |
| `triggerPosition` | 0..1 = 0 |
| `boundaryCrossfadeMs` | 0..100 ms = 5 |
| `lengthPolicy` | enum fit/change-duration = fit |
| `curvePoints` | up to 32 normalized points = linear endpoints |

**VST integration:** Source-stage transform; reads the assigned source region and returns a prepared mapping/asset. Duration changes must be a single session transaction with explicit fit/ripple/overlap policy.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Known ramp reverses exactly in whole-selection mode with fades disabled; speed reaches endpoints at configured times; zero-speed hold bounded; legacy tape glide preserved; change-duration requires explicit sequence policy.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX20 — Time stretch and rhythmic warp

**Stable type ID:** `advanced.warp.v1`. **Delivery:** P3. **Implementation:** `effect_dsp/warp/` (proposed path). 

**Processing contract:** Prepared source transform with immutable marker map; worker render by default, realtime mode only for a validated bounded backend. Time-changing operations use session edit transactions.

| Parameter ID | Range / options and default |
|---|---|
| `stretchRatio` | 0.25..4 = 1 |
| `targetDurationMs` | 1..600000 ms = selection duration |
| `targetBeats` | 0.0625..256 beats = source beats |
| `durationMode` | enum ratio/duration/beats = ratio |
| `algorithm` | enum rhythmic/tonal/general = rhythmic |
| `preservePitch` | bool = true |
| `transientSensitivity` | 0..1 = 0.5 |
| `transientProtection` | 0..1 = 0.75 |
| `preserveFormants` | bool = false |
| `gapHandling` | enum preserve/fade/fill = fade |
| `sequencePolicy` | enum fit/ripple/overlap = fit |
| `markers` | up to 256 monotonic source/output pairs = endpoints |
| `localCurves` | enum linear/smooth per segment = linear |

**VST integration:** Source-stage transform; reads the assigned source region and returns a prepared mapping/asset. Duration changes must be a single session transaction with explicit fit/ripple/overlap policy.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Exact target frame count; markers monotonic and endpoint anchored; fit leaves event slots fixed; ripple shifts subsequent events atomically; cancel worker leaves session untouched; no collapsed or zero-length segments.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX21 — Stereo utility

**Stable type ID:** `advanced.stereo.v1`. **Delivery:** P1. **Implementation:** `effect_dsp/stereo/` (proposed path). 

**Processing contract:** Explicit mono/stereo and mid/side matrices, fractional alignment delay and complementary low-frequency width crossover.

| Parameter ID | Range / options and default |
|---|---|
| `pan` | -1..1 = 0 |
| `balance` | -1..1 = 0 |
| `width` | 0..2 = 1 |
| `midGainDb` | -60..12 dB = 0 |
| `sideGainDb` | -60..12 dB = 0 |
| `mono` | bool = false |
| `swap` | bool = false |
| `invertLeft` | bool = false |
| `invertRight` | bool = false |
| `leftDelayMs` | 0..30 ms = 0 |
| `rightDelayMs` | 0..30 ms = 0 |
| `bassMonoEnabled` | bool = false |
| `bassMonoHz` | 20..1000 Hz = 120 |
| `panLaw` | enum linear/equal-power = equal-power |
| `correlation` | read-only -1..1 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Default matrix nulls against source; mid/side round-trip error <1e-6; mono output follows existing (L+R)/2 convention; mono input ignores unavailable side controls; alignment delay exact within interpolation tolerance.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX22 — Resonator

**Stable type ID:** `advanced.resonator.v1`. **Delivery:** P3. **Implementation:** `effect_dsp/resonator/` (proposed path). 

**Processing contract:** Up to eight tuned comb or modal resonances with normalized excitation and bounded feedback; tuning changes smooth or crossfade.

| Parameter ID | Range / options and default |
|---|---|
| `type` | enum comb/modal = comb |
| `rootMidi` | 0..127 integer = 60 |
| `fineCents` | -100..100 cents = 0 |
| `tuningMode` | enum free/scale/chord = free |
| `scale` | enum major/minor/pentatonic/custom = minor |
| `chord` | enum major/minor/sus2/sus4/diminished/custom = minor |
| `voiceCount` | 1..8 integer = 4 |
| `voiceSemitones[i]` | -48..48 st = 0 |
| `voiceLevelDb[i]` | -60..6 dB = -6 |
| `voicePan[i]` | -1..1 = 0 |
| `decaySeconds` | 0.02..30 s = 1 |
| `dampingHz` | 20..20000 Hz = 6000 |
| `excitation` | 0..1 = 0.5 |
| `brightness` | 0..1 = 0.5 |
| `inharmonicity` | 0..1 = 0 |
| `feedback` | 0..0.98 = 0.5 |
| `decayMode` | enum time/feedback = time |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Resonances within 5 cents in supported tuning range; measured decay matches selected mode within 10%; maximum voices normalized; low notes outside buffer range rejected visibly; silence does not create runaway output.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

### FX23 — Spectral freeze and shaping

**Stable type ID:** `advanced.spectral.v1`. **Delivery:** P3. **Implementation:** `effect_dsp/spectral/` (proposed path). 

**Processing contract:** Prepared STFT overlap-add processor with fixed maximum FFT buffers and capture commands timestamped in selection time. Structural FFT changes use prepared replacement.

| Parameter ID | Range / options and default |
|---|---|
| `capturePosition` | 0..1 = 0.5 |
| `capture` | momentary command |
| `freeze` | bool = false |
| `freezeMs` | 1..60000 ms = 1000 |
| `fftSize` | enum 256/512/1024/2048/4096/8192 = 2048 |
| `overlap` | enum 4x/8x = 4x |
| `frequencyBlurBins` | 0..64 bins = 0 |
| `temporalBlurMs` | 0..2000 ms = 0 |
| `tiltDbOct` | -12..12 dB/oct = 0 |
| `minimumHz` | 20..20000 Hz = 20 |
| `maximumHz` | 20..20000 Hz = 20000 |
| `magnitudeSmoothingMs` | 0..2000 ms = 50 |
| `phaseMode` | enum continue/locked/seeded-random = continue |
| `pitchSemitones` | -24..24 st = 0 |
| `captureCrossfadeMs` | 1..500 ms = 50 |
| `seed` | uint32 = 0 |

**VST integration:** Runs in the selected event insert chain or continuous group chain. Each independent event has its own state; group mode processes the selected sum with silence in gaps.  UI uses parameter metadata, with conditional controls enabled only in the applicable mode. Persist all fields under the stable type ID; group edit and automation use the common commands.

**Effect-specific acceptance:** Unmodified STFT reconstruction error <1e-5 after latency compensation; captures resolve at documented hop boundary; reset clears held spectrum; randomized phase repeats with seed; every FFT/overlap combination satisfies reconstruction window normalization.

**Implementation checklist:**
- [ ] Implement validated configuration and parameter descriptors, including every row above.
- [ ] Implement DSP/backend, reset, prepared memory estimate, latency and tail metadata.
- [ ] Wire editor controls, tooltips, selection assignments, modulation eligibility and preset/state round-trip.
- [ ] Add effect-specific reference tests plus common realtime/selection/export tests.
- [ ] Measure CPU/memory and pass Windows VST3/host audition gates before marking complete.

## 9. Verification and acceptance contract

1. Legacy compatibility: original session fixtures, event transform order, generator hashes and existing tests unchanged. Advanced-disabled output must be bit-identical to the legacy path on the same build.
2. Selection isolation: one selected event, three contiguous events, three noncontiguous events, subregion, nested parent/children, duplicate membership and unselected overlapping audio. Outside selected region, audio matches the baseline except explicitly enabled tails.
3. Audio reference: impulse, DC, silence, sine sweep, stereo phase, drum transient and long noise fixtures at 44.1/48/96/192 kHz, mono/stereo, block sizes 1/17/64/257/1024 and blocks larger than prepared maximum. DSP output/timing remains equivalent across block sizes within declared algorithm tolerance.
4. Realtime: allocation/deallocation instrumentation on process/reset, no locks/I/O, preallocated command queues, stress plan publication while playing, worker cancellation, plan retirement, rapid enable/reorder and sample-rate/device changes. ASan/UBSan; targeted TSan publication tests.
5. State: old-session load, new-session round-trip, missing source/IR, unknown effect, invalid enum, NaN/Inf, excessive counts, cycles, truncated payload, stale cache and parameter ID migration.
6. Playback/export: same prepared plan and seed render within 1e-5 absolute sample difference on the same build when stochastic pass index and quality match. Include overlap, lookahead, latency, loop, seek, tempo changes, tails and freeze caps. Spectral capture tolerances documented in its spec.
7. UI: selection and dragging, mixed edits, atomic undo, keyboard operation, tooltip coverage, scrolling/dismissal, A/B, presets and accurate pending/error state. Four essential smoke cases: selection assignment isolation; effect audible with bypass comparison; save/reload exact settings; matching export.
8. 35-second interactive QA script after smoke cases: 0–5 s import and drag-select; 5–10 s play/stop/loop; 10–15 s add/reorder/filter/delay; 15–20 s linked/mixed multi-edit and undo; 20–25 s automation/zoom/tooltips; 25–30 s seek/tails/bypass; 30–35 s save/reload and start export. This supplements, not replaces, automated tests.
9. Performance: measure Release on a recorded Windows reference machine at 48 kHz/128 samples. A P1 reference session with 8 overlapping voices, each filter+crunch, and one shared delay+reverb must have p99 callback time <50% block deadline over 60 s, no missed deadlines and no allocation. Record CPU/OS/build/backend. Heavier supported plans need measured budgets; freeze recommendation appears before overload. Default effect-memory budget 128 MiB, configurable 32..512 MiB outside playback; reject prepared plans exceeding it. Source assets/cache have separate displayed accounting.
10. Build gates: portable standalone module builds and transfer smoke, repository portability/include tools, GCC/Clang/MSVC warnings-as-errors, sanitizers, Windows VST3 and standalone build where target exists, Steinberg validator and actual Windows host smoke (at least two installed hosts, names/versions recorded). No fabricated host results.
11. Compare audio at matched loudness by listening to drum, bass, voice and tonal material. Log zipper noise, clicks, metallic artifacts and CPU spikes with reproducible presets. Major faults block delivery; minor defects are recorded with reproduction steps and affected version.

## 10. Ordered to-do list and completion evidence

All items start unchecked: this is a specification delivery, not an implementation status claim.

### Phase A — map and protect existing behaviour
- [ ] Read current public headers, effect state schema, feature specs, renderer tests and plugin parameter manifest.
- [ ] Record commit baseline, existing open work and actual build targets; reconcile documentation conflicts without overwriting concurrent changes.
- [ ] Add characterization fixtures for legacy EventFx, source traversal, export and zero-advanced-effects rendering.
- [ ] Map stable event/source/parent IDs and define migration adapters against actual types.

### Phase B — shared contracts and state
- [ ] Add effect_contracts with unit-aware descriptors, stable IDs, capabilities and structured errors.
- [ ] Add section assignment/group contracts, deterministic membership validation and resource limits.
- [ ] Add versioned state/preset payloads, unknown-type preservation and legacy-empty defaults.
- [ ] Add standalone module manifests, examples, migration docs and CMake dependencies.

### Phase C — execution and selection
- [ ] Add worker preparation, cancellation, revision checks, mailbox publication and off-thread retirement.
- [ ] Add insert execution, independent/group state, latency compensation, bounded tails and bypass crossfades.
- [ ] Add source-stage mapping/pre-render interface and undoable duration policies.
- [ ] Add single/multi/subregion selection commands and headless hit-test tests.
- [ ] Add drawer/cards/expanded controls, mixed-state edits, linked/relative settings and copy/paste.
- [ ] Add audition loop/context playback and visible tail/scope controls.

### Phase D — first effect release (P1)
- [ ] FX03 Filter and FX06 Bitcrusher through compatibility wrappers.
- [ ] FX21 Stereo utility; FX04 Parametric EQ.
- [ ] FX05 Distortion and FX07 Compressor.
- [ ] FX01 Delay and FX02 algorithmic Reverb.
- [ ] FX18 Stutter and FX19 Reverse/tape stop.
- [ ] FX15 Pitch shift/harmonizer, after validated backend selection.
- [ ] Finish every P1 advanced parameter, including dynamic EQ and optional convolution backend, before calling P1 complete; smaller alpha releases must explicitly list deferred controls.

### Phase E — modulation and richer processing (P2)
- [ ] Add envelopes, LFO/follower/steps, macro mapping, deterministic randomization and locks.
- [ ] Add fixed host macro parameters using append-only IDs.
- [ ] Integrate generation, Evolve, recursive zoom and variation-history inheritance.
- [ ] FX08 Gate, FX09 Transient shaper, FX10 Limiter.
- [ ] FX11 Chorus, FX12 Flanger, FX13 Phaser.
- [ ] FX14 Tremolo/autopan and FX16 Frequency shift/ring mod.

### Phase F — advanced source/spectral processing (P3)
- [ ] FX17 Granular with bounded source access and deterministic grains.
- [ ] FX20 Warp with marker editing and explicit length transactions.
- [ ] FX22 Resonator and FX23 Spectral freeze.
- [ ] Complete backend licensing review and reproducible dependency pinning; never copy third-party code without retaining its required notices.

### Phase G — integration, QA and release
- [ ] Add freeze cache, invalidation and resource telemetry.
- [ ] Finish WAV/Producer Kit limitations/manifest and full-pattern render.
- [ ] Complete common and per-effect acceptance tests; fix major defects.
- [ ] Complete 35-second UI smoke and longer realtime/performance checks.
- [ ] Record minor defects in repository QA notes with owner/status.
- [ ] Update module catalog, help with screenshots/arrows, tooltips and build status.
- [ ] Review actual diff, run required CI, merge through repository workflow, then produce Windows alpha from merged commit.
- [ ] Release notes identify implemented effect families, unavailable controls, limitations, tested hosts and exact commit.

## 11. Implementation practices and handoff

Implement thin vertical slices: descriptor -> DSP/reference test -> selection command -> UI -> persistence -> export check. Do not build all UIs before DSP exists. Reuse existing primitives and validated backends; do not rewrite legacy working effects merely to fit naming. Keep DSP math, session commands, serialization and presentation in separate files and layers.

Use RAII, explicit ownership, const source views, bounded containers at realtime boundaries, enum classes and unit-bearing names. Avoid raw owning pointers, global mutable state, generic string lookups in the callback and broad catch-all exception handling. Comments explain DSP equations, stability limits, threading/ownership and compatibility decisions. Public headers document units, preconditions, lifetime and error cases.

Dependency choices must be verified against primary API documentation at implementation time; this specification does not pin an unverified DSP library or claim current API signatures. Reuse existing JUCE shell facilities only behind adapters. Record algorithm equations/references, license and quality/latency trade-offs alongside each backend. Expose only working algorithms; never label generic saturation as a measured hardware emulation.

Each completion record contains changed files, parameter coverage, tests/measurements, migration impact, known limitations and commit. No checkbox becomes complete merely because a UI card exists. This document does not authorize a new standalone product or changes to other audio repositories.
