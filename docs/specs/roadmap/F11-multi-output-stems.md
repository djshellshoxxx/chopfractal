# F11 Multi-output stems

**Status:** Ready for development  **Size:** L (9 engineer-days)  **Depends on:** F00 (ControlsPanel/StatusBar split), F01 (state v2, trailing-block registry)  **Blocks:** none (F07/F08 only need the routing fields to stay append-only)

## 1. Summary and user value
The plugin exposes the main stereo output plus up to 8 auxiliary stereo outputs ("Out 2" .. "Out 9"). Each chop (or each role) is routed to one output so the DAW can put a different compressor, reverb or sampler channel on kick, snare and hats. Hosts that only give the main bus keep working: everything folds into the main output exactly as today. Producers get a stem workflow without exporting files; it is the main reason to use ChopFractal inside a mixing session.

## 2. User stories and scope
- As a producer I can route the kick role to "Out 2" and the snare role to "Out 3" so that I can compress them separately in my DAW.
- As a producer I can press "Auto: by role" and get one output per role without clicking each chop.
- As a producer I can override a single chop's output so that one special hit goes to its own channel.
- As a producer on a host that exposes only the main bus I hear the full mix and see "1 output (host gave no extra outputs)" so I am not confused.
- **In scope:** bus layout (main stereo in; main stereo out + 8 optional stereo aux outs), routing model (per-role, per-chop, default), renderer per-bus mixing, fold-down when aux buses are off, offline/export interplay, state persistence of routes, routing UI in ControlsPanel, validator and shell tests.
- **Non-goals:** mono aux outputs, more than 8 aux buses, sidechain input, per-bus level/pan/effects inside the plugin, MIDI output, routing changes affecting the audio thread through anything but the immutable Playback, per-stem export from the offline renderer to separate WAV files (listed as a follow-up in section 6), AU/AAX.

## 3. UX
Location: ControlsPanel (below the Dry mix / Output gain sliders), a collapsible section titled **"Outputs"**, collapsed by default.
- **Status line** (always visible, StatusBar-style label inside the section): `"Outputs: main + 3 aux active"` / `"Outputs: main only (the host enabled no extra outputs)"`. Updated from `ChopFractalProcessor::activeAuxBuses()` on the 30 Hz timer.
- **Routing mode** ComboBox "Output routing" with items `"Main only"` (default), `"By role"`, `"By chop"`, `"Custom"`. Selecting By role / By chop runs the auto-assignment (6). Editing any single route switches the box to `"Custom"`.
- **Route table**: one row per enabled chop (ChopSnapshot order), columns: color swatch + `"Chop NN"` (+ role name when set), ComboBox "Output" with items `"Main"`, `"Out 2"` ... `"Out 9"`, `"Role default"` (only when the chop has a role with a route). Scrollable, max visible rows 8.
- **Role table** (above chop table, only rows for roles that exist in `roleMap()`): ComboBox per role with `"Main"`, `"Out 2"` ... `"Out 9"`.
- **Buttons:** `"Auto: by role"`, `"Auto: by chop"`, `"Reset to Main"`.
- Output items beyond the host-enabled aux count are shown but suffixed `" (off in host)"` and greyed in the popup; choosing one is allowed (route is stored) and plays on Main until the host enables it. Tooltip on the row: "This output is not enabled in your DAW; the sound plays on the main output until you enable it."
- Notices (via `ProjectSession::takeNotices`, shown in StatusBar): `"Routed 4 roles to Out 2-5."` (Info); `"More than 8 roles: the extra roles stay on Main."` (Warning).
- No source loaded: whole section disabled; status line `"Outputs: load a source first"`.
- Keyboard: standard JUCE ComboBox focus; every ComboBox gets `setTitle("Output for chop NN")` / `("Output for role <name>")` for accessibility.

## 4. Data model and state
`modules/audio_renderer/include/chopfractal/audio_renderer/renderer.hpp` additions:
```cpp
constexpr int kMaxAuxBuses = 8;     // aux buses 1..8 ("Out 2".."Out 9"); bus 0 is main
struct BusRouting {                 // immutable, resolved, built on a non-RT thread
  std::vector<std::pair<ChopId, std::uint8_t>> chopBus;  // sorted by ChopId; bus 0..kMaxAuxBuses
  std::uint8_t defaultBus = 0;
  std::uint8_t busFor(ChopId c) const;                   // binary search; defaultBus when absent
};
```
`Playback` gains `std::vector<std::uint8_t> eventBus;` (same size/order as `events`, filled by `makePlayback` after the stable sort; empty means "all main"). `Config` gains `int auxBuses = 0;` (0..kMaxAuxBuses, clamped in `prepare`, the number of aux buses the host enabled). No existing field changes; defaults reproduce today's behavior bit for bit.

`composition/project_session.hpp`:
```cpp
enum class RoutingMode : std::uint8_t { MainOnly = 0, ByRole = 1, ByChop = 2, Custom = 3 };
struct OutputRouting {
  RoutingMode mode = RoutingMode::MainOnly;
  std::map<std::string, std::uint8_t> roleBus;   // role name -> bus 0..8
  std::map<ChopId, std::uint8_t> chopBus;        // explicit per-chop override -> bus 0..8
};
```
Effective bus of a chop = `chopBus[chop]` if present, else `roleBus[roleOf(chop)]` if the chop has a role and a route, else 0.

Serialization: optional trailing block in the `composition` module payload (`kModSession` meta), after the existing evolve block, using the trailing-block registry F01 defines for session state v2. Block name `routing`, payload: `u8 mode; u32 nRole; nRole x (str role, u8 bus); u32 nChop; nChop x (u64 chopId, u8 bus)`. Limits: nRole <= 256, nChop <= limits::kMaxChops (256), bus <= 8, role string <= 64 bytes; any violation is `ErrorCode::Corrupt`, load is all-or-nothing as today. The block is written only when `mode != MainOnly || !roleBus.empty() || !chopBus.empty()`; a project that never used routing serializes byte-identically to before. Old projects (no block) load as `MainOnly`. A project saved with routing then opened in a build without F11 ignores the block (trailing, unknown). Route entries whose chop/role no longer exists are dropped at load with Info notice `"Some output routes were dropped because their chops changed."`.
Pattern payload and `pattern::serialize` are untouched: golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 must not change.

Host parameters: none added (routing is state, not automation). The parameter manifest stays at version 2 unless F07/F08 bump it.

## 5. Public API
**portable `modules/audio_renderer` (RT rules apply to process):**
```cpp
struct AuxOutputs {                       // views onto host buffers; each aux bus is a stereo pair
  int count = 0;                          // 0..kMaxAuxBuses, must equal Config::auxBuses or less
  float* const* bus[kMaxAuxBuses] = {};   // bus[i] -> float*[2] for "Out i+2"
};
Result<std::shared_ptr<const Playback>> makePlayback(SourcePtr source, const FlatEventList& events, Ticks lengthTicks,
                                                     const BusRouting* routing);   // new overload; old 3-arg form forwards with nullptr
void Renderer::process(const TransportBlock&, const RenderParams&, const float* const* in, float* const* out,
                       int numChannels, int frames, const AuxOutputs& aux);       // new overload; old form forwards AuxOutputs{}
struct OfflineStems { std::vector<std::vector<float>> main; std::vector<std::vector<std::vector<float>>> aux; };
Result<OfflineStems> renderOfflineStems(std::shared_ptr<const Playback>, const OfflineSettings&, int auxBuses);
```
`renderOffline` remains and returns the main bus with fold-down: it calls the stems path with `auxBuses = 0`, so it is byte-identical to today.
**portable `modules/plugin_host_adapter` (`host_time.hpp`):**
```cpp
bool isSupportedBusLayout(int inputChannels, int outputChannels);                 // unchanged
bool isSupportedBusLayout(int inputChannels, int outputChannels, const int* auxOutputChannels, int numAux);  // aux: each 0 (disabled) or 2; any aux enabled requires main in==out==2
```
**composition:**
```cpp
Status ProjectSession::setOutputRouting(const OutputRouting& r);         // validates bus <= 8, role/chop exist; republish() so the Playback carries eventBus
const OutputRouting& ProjectSession::outputRouting() const;
int ProjectSession::autoRouteByRole();   // fills roleBus, mode=ByRole, returns number of roles routed
int ProjectSession::autoRouteByChop();   // fills chopBus, mode=ByChop
void ProjectSession::setAuxBusesAvailable(int n);  // message thread: used only for the status notice and "off in host" UI, never for the Playback
```
`installPlayback`/`republish()` build a `BusRouting` from the session's `OutputRouting` and pass it to `makePlayback`. Routing is therefore part of every published Playback; a routing edit republishes through the Mailbox (no audio-thread state).
**plugin/PluginProcessor:** `int activeAuxBuses() const` (atomic read), constructor `BusesProperties`, `isBusesLayoutSupported`, `prepareToPlay` reads `getBusCount(false)`/`getBus(false, i)->isEnabled()` into `Config::auxBuses`.
Threading: all setters message thread under `withSession`; `process` audio thread, no allocation/locks.

## 6. Behavior details and edge cases
**Bus layout (PluginProcessor ctor):**
```cpp
BusesProperties p = BusesProperties().withInput("Input", AudioChannelSet::stereo(), true)
                                     .withOutput("Main", AudioChannelSet::stereo(), true);
for (int i = 2; i <= 9; ++i) p = p.withOutput("Out " + String(i), AudioChannelSet::stereo(), false);  // disabled by default
```
`isBusesLayoutSupported(layouts)`: gather `getMainInputChannels()`, `getMainOutputChannels()`, and for `i = 1..8` `layouts.getNumChannels(false, i)` (0 when disabled); return `cf::host::isSupportedBusLayout(in, out, aux, 8)`. Mono main in/out (1/1) stays supported and rejects any enabled aux. The default layout (stereo/stereo, all aux off) is exactly today's, so hosts that never touch the extra buses see identical behavior and the validator's bus tests pass as before.

**processBlock:** `buffer` holds main channels first, then aux channels in bus order. Use `getBusBuffer(buffer, false, i)` per enabled aux bus; build `AuxOutputs` from `getArrayOfWritePointers()` on the stack (fixed `float* auxPtrs[8][2]`, no allocation). `channels = min(2, main channel count)` as today.

**Renderer mixing (no allocation):** `prepare` replaces `mixL/mixR` with one `std::vector<float> mix` of size `(1 + kMaxAuxBuses) * 2 * maxBlock` (9*2*2048*4 B = 147 KiB at the default block; 9*2*16384*4 = 1.2 MiB worst case, allocated only in `prepare`). `Voice` gains `std::uint8_t bus`; `renderVoice` adds into `mix[(bus*2+0)*maxBlock + i]` and `+1`. `startFromEvent` sets `v.bus = effective(pb.eventBus[idx])` where `effective(b) = b <= activeAux ? b : 0` and `activeAux = min(aux.count, cfg.auxBuses)` for the current block; `Trigger` carries the event index (`event - pb->events.data()`). Previews (`requestPreview`) always go to bus 0 so auditioning in the editor is always heard on Main. Voice allocation, stealing, fade and ordering logic are unchanged (voices are still one shared pool of `maxVoices`; stolen voices keep their bus while fading). Main-bus voice summation order is unchanged, so with no routing the float output is bit-identical to today.
Output stage: per sample, `outGain` and `dry` are advanced exactly once (as today); then main = `(mix0 + dry*in) * outGain`, aux k = `mix_k * outGain` (output gain applies to every bus; dry mix is main only, so a stem never contains the dry input). Aux output is flushed with the same `isfinite/1e-30` rule. Every aux frame is written every block (zeros when silent) because hosts do not guarantee cleared output buffers; all early-return paths (disabled, no source, passThrough) must call `clearAux(aux, frames)`; passThrough sends previews and input to main only.
Mono main (`numChannels == 1`): aux must be empty (layout rule); behavior as today.
Block larger than `maxBlock`: the existing chunk loop advances aux pointers by `done` using a stack array `float* chunkAux[8][2]`.

**Fold-down:** a chop routed to bus 3 while the host enabled 1 aux bus plays on Main. The editor shows `" (off in host)"`. Enabling the bus mid-session triggers `prepareToPlay` (JUCE guarantees a prepare after `setBusesLayout`), so no mid-block change occurs.

**Auto assignment (deterministic, no RNG):** `autoRouteByRole`: iterate `chops()->chops` in snapshot order; collect distinct non-empty role names in first-appearance order; assign buses 1..8 in that order; roles beyond 8 and chops with no role stay on Main (bus 0) and emit the Warning notice. `autoRouteByChop`: chop at snapshot index n gets bus `1 + (n % 8)`; chops with n >= 8 share buses (documented wrap) and an Info notice `"More than 8 chops: outputs are shared."`. Both clear previous `chopBus`/`roleBus` first and are not undoable history nodes (routing is project config, like the Dry mix slider), but they republish atomically.

**Interactions:** marker edits (`reconcileAfterMarkerChange`) prune routes of vanished chops through `routing_.chopBus` retention; roles follow markers already (`roles_.retainOnly`). Locks, Evolve, A/B, history activation do not touch routing (routing lives outside patterns). `loadSource` of a new source resets routing to default (new chops); `relinkSource` keeps it. `clearSource`/`reset` reset it.

**Offline/export:** `exportWav` keeps rendering the main-bus fold-down of all voices (`auxBuses = 0` => every voice on bus 0), so the exported WAV equals what a main-only host hears and equals today's output. Follow-up (not in this feature): `ExportWavOptions::stems` writing `<name>_out2.wav` ... via `renderOfflineStems`; the function and its test land here so the follow-up is a UI change only. `exportKit` ignores routing.

**Failure/rollback:** `setOutputRouting` builds the new `BusRouting`, runs `makePlayback`; on failure state is unchanged (same pattern as `installPlayback`).

## 7. Test plan
Unit, `modules/audio_renderer/tests/test_renderer.cpp`:
- `routing_default_is_bit_identical_to_legacy_process`: random pattern, render 4 s via old `process` and new overload with `AuxOutputs{}`; `memcmp` equal.
- `chop_routed_to_aux_appears_only_on_that_bus`: two chops (sine 440, sine 880), chop A -> bus 1, B -> bus 0; aux bus 1 energy > 0 and its 880 Hz bin < -60 dB re A; main has B only.
- `aux_sum_equals_legacy_main_mix`: routes spread over 4 buses; sample-wise `main+aux1+..+aux4` within 1e-6 of the unrouted main (dry=0).
- `disabled_aux_folds_into_main`: routing to bus 3 with `Config::auxBuses=1`: main equals unrouted main bit-exactly.
- `dry_mix_reaches_main_only` and `output_gain_applies_to_all_buses` (-6 dB halves every bus).
- `aux_buffers_are_fully_written_when_silent`: pre-fill aux buffers with 0.7f, process silence/disabled/passThrough; all frames 0.
- `block_size_independence_with_aux`: block sizes 1,7,64,333,2048 give identical aux output.
- `process_with_aux_does_not_allocate`: reuse the global-`operator new` counter test; zero allocations with 8 aux buses.
- `stems_offline_sum_matches_renderOffline`.
`modules/plugin_host_adapter/tests`: `bus_layout_rules_with_aux` (2/2 + aux {2,0,...} ok; 1/1 + aux {2} rejected; aux 1 channel rejected; 2/1 rejected as today; existing assertions at lines 138-139 unchanged).
Integration, `composition/tests`: `routing_roundtrips_and_old_projects_load_main_only` (save with routing, load, equal `OutputRouting`; a state saved with no routing is byte-identical to a pre-F11 fixture); `routing_pruned_when_chop_deleted`; `auto_route_by_role_is_deterministic` (two sessions, same assignments; 9 roles => 8 routed + Warning); `exportWav_ignores_routing_and_matches_golden_bytes`.
GUI (`plugin/tests/test_plugin_shell.cpp`): `only_matching_mono_and_stereo_bus_layouts_are_supported` extended: `setBusesLayout` with Out 2 enabled stereo accepted, mono main + Out 2 rejected. `editor_routing_buttons_drive_the_session`: load source with 3 roles, click `"Auto: by role"`, assert `session.outputRouting().mode == ByRole` and `roleBus` size 3; choose `"Out 2"` for chop 1 in the route ComboBox, assert mode `Custom` and `chopBus` has it; click `"Reset to Main"`, assert empty. `processor_writes_aux_buses`: enable bus 1 via `setBusesLayout`, `prepareToPlay`, process a 512-frame block of a 10-channel `AudioBuffer`, assert aux channels non-zero for the routed chop and zero for others.
Determinism/sanitizers: golden hash tests unchanged and green; ASan/UBSan and TSan CI jobs run the new renderer tests (TSan: routing republish while processing from a second thread).
Manual QA (REAPER first): 1) Load a drum loop, Smart Setup, Generate. 2) Outputs > Auto: by role. 3) In REAPER, FX window > Pin connector (routing button) > set Out 2/3/4 as "2 audio outs per bus" (track channels = 8). 4) Create 3 child tracks receiving channels 3/4, 5/6, 7/8. Expected: kick only in child 1, snare in child 2, hats in child 3. 5) Mute child 2: snare disappears, others keep playing. 6) Set track to 2 channels: all sounds return on the master pair and status line reads `"Outputs: main only (the host enabled no extra outputs)"`. 7) Save project, reopen: routes intact.

## 8. Acceptance criteria
- [ ] With default layout (aux off) `memcmp` of 60 s of output vs the pre-F11 build is identical for the fixed-seed corpus.
- [ ] pattern_engine golden hashes 2397844821793184813, 12084884237330071892, 13710596758976548913, 18237151833431327128 unchanged; pre-F11 project files load and re-save byte-identical when routing was never used.
- [ ] Official validator (tools/validate_vst3.sh, SDK 3.8.0) reports 0 failed tests with 9 output buses declared (previously 47/47; the count may grow, none fail).
- [ ] `sum(main, aux1..aux8)` equals the unrouted main within 1e-6 per sample (dry 0).
- [ ] Zero allocations in `process` with 8 aux buses (instrumented test) and no mutex use (code review + TSan).
- [ ] Main-only host: no audio lost; status line shows the main-only message.
- [ ] CPU: process() with 8 aux buses and 8 voices costs <= 1.15x the single-bus cost in `bench` (add a timing assertion with a generous 1.5x bound in CI).
- [ ] Routes survive save/load and marker edits as specified; routes of deleted chops are pruned.
- [ ] `exportWav` output byte-identical to pre-F11 for the same project.

## 9. Files touched
| path | new/modified | change |
|---|---|---|
| modules/audio_renderer/include/.../renderer.hpp | modified | kMaxAuxBuses, BusRouting, AuxOutputs, Playback::eventBus, Config::auxBuses, overloads, renderOfflineStems |
| modules/audio_renderer/src/renderer.cpp | modified | per-bus mix buffer, Voice::bus, Trigger index, output stage, clearAux |
| modules/audio_renderer/tests/test_renderer.cpp, module.json/README (generated via tools/gen_module_docs.py) | modified | tests, docs |
| modules/plugin_host_adapter/{include/.../host_time.hpp, src/parameters.cpp, tests} | modified | aux-aware isSupportedBusLayout |
| composition/include/.../project_session.hpp, composition/src/project_session.cpp | **HOT** modified | OutputRouting, setters, republish passes BusRouting, routing trailing block |
| composition/src/features.cpp | modified | none functional (documented unchanged exportWav); test only |
| plugin/src/PluginProcessor.cpp/.h | **HOT** modified | BusesProperties, isBusesLayoutSupported, prepareToPlay aux count, processBlock AuxOutputs |
| plugin/src/ControlsPanel.cpp/.h (from F00; else PluginEditor.cpp **HOT**) | modified/new | "Outputs" section |
| plugin/tests/test_plugin_shell.cpp | modified | new tests |
| docs/specs/audio-engine-and-routing.md | modified | replace "No sidechain bus or auxiliary outputs in the first release" with the F11 contract |

## 10. Risks and mitigations
- Hosts mishandle disabled optional buses (older Cubase/FL wrappers): keep aux disabled by default and fold-down; validate in the DAW matrix (F15). Fallback: build flag `CHOPFRACTAL_AUX_OUTPUTS=OFF` declares only the main bus.
- Uncleared aux buffers cause noise: explicit full write in every code path; test `aux_buffers_are_fully_written_when_silent`.
- `makePlayback` signature growth touching callers: keep the 3-arg overload; only `installPlayback` uses the new one.
- Bus pointer layout differences between JUCE and hosts: always derive from `getBusBuffer`, never from channel index arithmetic; covered by `processor_writes_aux_buses`.
- Routing changes republish an identical event list (CPU only, bounded by kMaxPreparedEvents).

## 11. Implementation steps
1. host_time.hpp aux overload of `isSupportedBusLayout` + tests.
2. Renderer: `BusRouting`, `Playback::eventBus`, 4-arg `makePlayback`, `Trigger` index; tests for `busFor` and eventBus ordering (no behavior change yet).
3. Renderer: per-bus `mix`, `Voice::bus`, `AuxOutputs` process overload, `clearAux`, fold-down; bit-identity, sum, allocation, block-size tests.
4. `renderOfflineStems` + test; `renderOffline` forwards.
5. composition: `OutputRouting`, setters, auto-assign, republish integration, trailing block (after F01 registry), integration tests.
6. PluginProcessor: BusesProperties, layout rules, processBlock wiring, shell tests; run validator.
7. ControlsPanel "Outputs" section + GUI tests.
8. Docs update and manual QA in REAPER, record results in docs/BUILD_STATUS.md.
