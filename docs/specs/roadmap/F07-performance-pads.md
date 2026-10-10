# F07 Performance pads

**Status:** Ready for development  **Size:** L (6 engineer-days)  **Depends on:** F00 (PadPanel in `plugin/src/panels/`, command layer), F01 (the `pads` StateSection); soft dependency
on F08 (reuses `render::SpscQueue` and `render::LiveInput`; whichever merges second adapts, section 5)  **Blocks:** F11 (shares `processBlock` and bus/MIDI declarations; lands after F07
per README)

## 1. Summary and user value
Play the chops like a sampler: MIDI notes from the DAW (or a 4x4 grid of on-screen pads) trigger the current chops sample-accurately on top of the running pattern, with velocity,
poly/mono/gate behavior and hi-hat style choke groups. An optional record mode captures the hits in time with the song, quantizes them to the grid and merges them into the pattern as a
single undo step. Today the plugin ignores MIDI (`acceptsMidi() == false`) and the only way to hear a chop on demand is the editor audition; F07 makes ChopFractal playable and turns
performances into pattern content.

## 2. User stories and scope
- As a producer I can play a MIDI keyboard or drum pad and hear chop N on note `baseNote + N` exactly on the note's sample, so I can finger-drum the chops.
- As a producer I can click or hold on-screen pads when my host cannot send MIDI to an effect.
- As a producer I choose Poly, Mono or Gate behavior and a velocity curve (both automatable), and hats choke each other.
- As a producer I can arm Record, play pads over the looping pattern, and find the quantized hits as hand-placed (protected) events in the pattern, undoable in one step.
- **In scope:** `acceptsMidi`/CMake MIDI input, audio-thread note handling in `Renderer` (pad voices, choke, velocity), `PadBank` mailbox, UI->audio and audio->UI lock-free queues,
  PadPanel, take recording and merge (`ProjectSession::mergeTake`), `pads` state section, parameters `perf_mode` and `perf_velocity_curve` (manifest v3), validator and host notes,
  tests.
- **Non-goals:** MIDI output (`producesMidi` stays false), MIDI clock/SPP sync, sustain pedal, pitch bend, aftertouch, per-note pitch or retune of chops (pitch is a pattern-event
  transform, F02/F08), MPE, a note-to-chop map editor beyond base note, recording to the host's MIDI track, count-in/metronome, recording while the transport is stopped, an
  Instrument-category build (see risks).

## 3. UX
**Entry:** FeaturePanel gets a toggle **"Pads"** (title "Show the performance pads") that shows/hides **PadPanel** (new `plugin/src/panels/PadPanel.{h,cpp}`, a 132 px row docked below
PatternPanel; hidden by default). **PadPanel contents** (left to right, each with `setTitle`):
- 4x4 grid of `PadButton` (subclass of `juce::Button`, component names `"Pad 1"` .. `"Pad 16"`, `getButtonText()` = `"Pad N"`, titles `"Pad N: chop M, note C1"`). Pad N of page P is
  chop index `P*16 + N - 1` (0-based) in `ChopSnapshot` order; the pad paints `M`, the chop's role and the note name (convention C3 = MIDI 60, so 36 = C1). Mouse down = NoteOn, up or
  exit = NoteOff; velocity = `127 - round(87 * y / height)` (127 at the top edge, 40 at the bottom; ignored by the Fixed curve). Pads beyond the chop count are disabled and empty. A pad
  lights while its note sounds (set from MIDI too, section 5 `padActivity_`). Right-click opens a `PopupMenu`: `"Choke group: Off"`, `"Choke group 1"`, ..., `"Choke group 4"`.
- ComboBox **"Pad page"** (`"Chops 1-16"`, `"Chops 17-32"`, ...; hidden when 16 or fewer chops).
- ComboBox **"Pad mode"** (parameter `perf_mode`: `"Poly"`, `"Mono"`, `"Gate"`; default Poly) and ComboBox **"Velocity curve"** (parameter `perf_velocity_curve`: `"Linear"`, `"Soft"`,
  `"Hard"`, `"Fixed"`; default Linear), both through `ComboBoxAttachment`.
- Slider **"Base note"** (0 to 111 integer, default 36, text `"C1 (36)"`), ComboBox **"MIDI channel"** (`"Omni"` default, `"1"` to `"16"`).
- ToggleButton **"Record"** (title "Record pad hits into the pattern"), ComboBox **"Quantize"** (`"Off"`, `"1/4"`, `"1/8"`, `"1/16"` default, `"1/32"`, `"1/16T"`), ComboBox **"Merge"**
  (`"Add to pattern"` default, `"Replace slots"`), Button **"Discard Take"**. **Enable rules:** pads and settings need a loaded source with chops (otherwise the panel shows `"Load a
  source and detect chops to play the pads."`); Record needs a pattern; Discard Take enabled while uncommitted hits exist (label shows `"N hits waiting"`). Tab order: pads (row-major),
  page, mode, curve, base note, channel, Record, Quantize, Merge, Discard Take. **Messages (StatusBar):** `"Recording armed: start the transport and play the pads."`, `"Recording needs
  the host to report its song position."`, `"Recorded 5 hits."`, `"Recorded 5 hits. 2 skipped: their beat is locked."`, `"... 1 skipped: the event cap (512) is reached."`, `"... 3
  skipped: they matched no chop."`, `"Take buffer full (4096 hits); extra hits were ignored."`, `"Pads need a source with chops."`, `"This host does not send MIDI to effects; use the
  on-screen pads."` (shown once, if a Record is armed and no MIDI note has ever arrived after 20 s of playback - informational only).

## 4. Data model and state
**Host parameters (manifest version 3, appended; IDs permanent; positions follow merge order):** `{"perf_mode", "Pad Mode", "", ParamKind::Choice, 0, 2, 0, 1, 0, 3, "Poly|Mono|Gate"}`
and `{"perf_velocity_curve", "Velocity Curve", "", ParamKind::Choice, 0, 3, 0, 1, 0, 3, "Linear|Soft|Hard|Fixed"}`; enum entries `kPerfMode`, `kPerfVelocityCurve` after the previous
last entry (`kChaos` if F08 merged first, else `kFxIntensity`). `kManifestVersion` = 3 (set by whichever of F08/F07 merges first). Defaults (0 = Poly, 0 = Linear) are the values a
project without these parameters gets.
```cpp
// modules/audio_renderer/include/chopfractal/audio_renderer/pads.hpp  (portable, no JUCE)
namespace chopfractal::render {
constexpr int kPadNotes = 128;
constexpr int kMaxPadEventsPerBlock = 256;
enum class PadMode : std::uint8_t { Poly = 0, Mono = 1, Gate = 2 };
enum class VelocityCurve : std::uint8_t { Linear = 0, Soft = 1, Hard = 2, Fixed = 3 };
float velocityGain(VelocityCurve c, int velocity);  // x = clamp(v,1,127)/127: Linear x, Soft sqrt(x), Hard x*x, Fixed 1.0
struct PadEvent { std::int32_t frame = 0; enum class Type : std::uint8_t { NoteOn, NoteOff, AllNotesOff } type = Type::NoteOn; std::uint8_t note = 0; std::uint8_t velocity = 100; };
struct PadParams { PadMode mode = PadMode::Poly; VelocityCurve curve = VelocityCurve::Linear; };
struct PadSlot { SampleRange region; std::uint32_t fadeInFrames = 0, fadeOutFrames = 0; std::uint8_t chokeGroup = 0; };  // region.empty() = unmapped
struct PadBank { std::array<PadSlot, kPadNotes> slots{}; };  // indexed by MIDI note; immutable once published
}
```
`LiveInput` (declared by F08, final shape): `{ const GestureEvent* gestures; int numGestures; GestureParams gestureParams; const PadEvent* pads = nullptr; int numPads = 0; PadParams
padParams; }`. If F07 merges first it declares `LiveInput` with the pad fields only and F08 adds the gesture fields. **Session settings** `PadSettings { std::uint8_t baseNote = 36;
std::uint8_t midiChannel = 0; /*0 omni, 1..16*/ std::uint8_t quantizeDivision = 16; /*0 off, 4, 8, 16, 32*/ bool quantizeTriplet = false; std::uint8_t merge = 0; /*0 add, 1 replace*/
std::map<std::uint64_t, std::uint8_t> chokeOverride; /*chop id -> 0 off, 1..4; absent = default (role "hat" -> 1, else off)*/ }`. **StateSection `pads` (schema 1, F01 API):** `u8
baseNote, u8 midiChannel, u8 quantizeDivision, bool triplet, u8 merge, u32 n (<= 256), n x {u64 chop, u8 group}`. Written only if any field differs from the defaults above, so a project
that never touches pads is byte-identical to the previous build; loader validates ranges (`baseNote <= 111`, `midiChannel <= 16`, division in {0,4,8,16,32}, group <= 4) and fails the
load with `ErrorCode::Corrupt` otherwise. Pad mode and curve live in the APVTS state. Takes are never saved (transient). **Take types (composition):** `struct TakeHit { double ppqOn;
double ppqOff; /* < 0 = none */ std::uint8_t note; std::uint8_t velocity; };` `struct TakeOptions { int quantizeDivision = 16; bool triplet = false; std::uint8_t merge = 0;
render::VelocityCurve curve; render::PadMode mode; int baseNote = 36; };` `struct TakeReport { int added = 0, replaced = 0, skippedLocked = 0, skippedCap = 0, skippedUnmapped = 0,
skippedDuplicate = 0; };` Limits: 128 MIDI notes; 256 pad events per block (excess dropped and counted in `padDropped()`); UI->audio pad queue 128 events; audio->UI take queue 1024
events; take buffer 4096 hits.

## 5. Public API
- **Renderer** (`audio_renderer`, hot): `Mailbox<PadBank>& Renderer::padMailbox()` (publish from non-RT, acquire once per block on the RT thread); `void Renderer::process(..., const
  LiveInput& live)` (the F08 overload; the 6-argument `process` forwards `LiveInput{}`); `std::uint64_t Renderer::padDropped() const`, `int Renderer::activePadVoices() const`. `Voice`
  gains `std::uint8_t tag` (0 none, 1 stutter (F08), 2 pad), `std::uint8_t padNote`, `std::uint8_t chokeGroup`.
- **SpscQueue** (`audio_renderer/spsc_queue.hpp`; if F08 has not merged, create it exactly as specified in F08 section 4).
- **Composition** (`composition/src/pads.cpp`, declarations in `project_session.hpp`, hot): `void attachPadMailbox(render::Mailbox<render::PadBank>*)`; `Status setPadSettings(const
  PadSettings&)` (validates, republishes the bank); `const PadSettings& padSettings() const`; `Result<TakeReport> mergeTake(const std::vector<TakeHit>& hits, const TakeOptions& opt)`;
  `Status setChokeGroup(ChopId, std::uint8_t group)`. The bank (`slots[baseNote + i]` for chop i, i < N and `baseNote + i <= 127`; region = `ChopInfo::range`, fades from `ChopInfo`,
  `chokeGroup` per above) is rebuilt and published by one private `publishPadBank()` called from `publish(pb)` (every playback publish), `assignRole`, `clearRole` (F06),
  `setPadSettings` and `setChokeGroup`. Threading: message thread under `withSession`.
- **Plugin** (`PluginProcessor`, hot): `acceptsMidi() { return true; }`, `producesMidi()`/`isMidiEffect()` stay false; `void padTrigger(int note, int velocity, bool on)` (any thread;
  pushes to `SpscQueue<PadEvent, 128> uiPadQueue_`); atomics `padChannel_`, `recordArmed_`; `SpscQueue<TakeEvent, 1024> takeQueue_` with `struct TakeEvent { double ppq; std::uint8_t
  note, velocity; bool on; };`; `std::array<std::atomic<std::uint32_t>, 4> padActivity_` (bit per note, set on NoteOn by the audio thread, `exchange(0)` by the editor timer);
  preallocated `std::array<PadEvent, 256> padEvents_`. `void setRecordArmed(bool)`, `void discardTake()`, `int takeHits() const`.
- **Editor commands (F00 layer):** `commands.setPadSettings(...)`, `commands.padPress(note, vel)` / `padRelease(note)` -> `proc_.padTrigger`.

## 6. Behavior details and edge cases
**CMake / plugin declaration.** `plugin/CMakeLists.txt`: `NEEDS_MIDI_INPUT TRUE` (JUCE then defines `JucePlugin_WantsMidiInput=1` and the VST3 wrapper adds one event input bus);
`IS_SYNTH FALSE`, `VST3_CATEGORIES Fx`, `NEEDS_MIDI_OUTPUT FALSE`, `IS_MIDI_EFFECT FALSE` unchanged; `isBusesLayoutSupported` unchanged (stereo/mono in == out). `getLatencySamples()`
stays 0: a pad costs no look-ahead. **MIDI parse in `processBlock` (audio thread, no allocation).** Before `renderer_.process`: drain `uiPadQueue_` into `padEvents_` with `frame = 0`;
then iterate `juce::MidiBuffer` (`for (const auto meta : midi)`): `msg = meta.getMessage()`; skip if `padChannel_ != 0 && msg.getChannel() != padChannel_`; `frame =
clamp(meta.samplePosition, 0, frames - 1)`; NoteOn with velocity > 0 -> `NoteOn{note, velocity}` (also `padActivity_[note >> 5] |= 1u << (note & 31)`); NoteOff or NoteOn velocity 0 ->
`NoteOff`; `isAllNotesOff()`/`isAllSoundOff()` -> `AllNotesOff`; controllers go to F08's `gestureFromCc` when F08 is present; everything else ignored. At most 256 events; extras
increment `padDropped_`. The MIDI buffer is not modified or cleared. Events are already sorted by sample position (JUCE guarantees); the UI events at frame 0 are placed first. Pad input
is ignored (and held voices killed) while `effect_enable` is off, like all voices. **Renderer, per `NoteOn(note, vel)` at frame f** (the event is merged into the existing trigger loop
in `processChunk`: render up to f, then act; at equal frames pattern triggers run first). The same merged loop is used by the pass-through branch (source loaded, no pattern yet), and
pad events work with the transport stopped (like previews). Steps: (1) `slot = bank.slots[note]`; if `region.empty()` or `region.end > src.frames` -> ignore (counts `padUnmapped`). (2)
Mono: `releaseVoice` every active, non-fading voice with `tag == 2`. (3) Gate: also release any voice with the same `padNote`. (4) Choke: if `slot.chokeGroup != 0`, `releaseVoice` every
active voice with `tag == 2 && chokeGroup == slot.chokeGroup` (not pattern voices). (5) `tx.level = velocityGain(curve, vel)`, pan 0, pitch 0, no reverse (F08's Reverse gesture may flip
it); gate = `ceil(region.length / rate)` with `rate = (src.sampleRate / cfg.sampleRate)`, so the whole chop plays; fades from the slot (0 = renderer defaults 32/64 frames);
`allocateVoice()` as for pattern voices (quietest voice stolen with a fade), then set `tag = 2`, `padNote`, `chokeGroup`. `NoteOff` in Gate mode: `releaseVoice` for voices with that
`padNote` (64-frame fade, `kKillFade`); in Poly/Mono it is ignored (one-shot). `AllNotesOff`: release all `tag == 2` voices. Pad voices survive transport stop, seek and loop wrap
(`releaseAll()` skips `tag == 2`); they are killed by `hardKillAll` (source change, bypass) and `Renderer::reset()`. **Latency and budgets.** Added plugin latency: 0 samples; the voice
starts at the note's sample; the first non-zero output sample is at `f + 1` (attack ramp starts from 0; the 32-frame default attack is about 0.7 ms at 48 kHz). Worst-case extra
audio-thread work per block: 256 events x O(voices = 16) comparisons, no allocation. UI pad press to sound: one block (the event is applied at frame 0 of the next block). **Velocity.**
Table in section 4. v = 127 is exactly 1.0 for Linear, Soft, Hard; Fixed is 1.0 for all; the result multiplies the chop's natural level (the chop's samples are not normalized).
**Recording.** `recordArmed_` is set by the Record toggle. In `processBlock`, when armed and `tt.block.playing && tt.block.positionValid`, every pad NoteOn/NoteOff (MIDI and UI) is also
pushed to `takeQueue_` with `ppq = tt.block.ppq + frame * tt.block.bpm / (60 * sampleRate)` (UI events use frame 0); a full queue increments `takeDropped_`. `pollAudioFlags()` (30 Hz)
drains `takeQueue_` into `takeBuffer_` (a `std::vector<TakeHit>` with `reserve(4096)`; beyond 4096 hits new ones are dropped with the buffer-full message), pairing each NoteOff with the
latest open hit of the same note. The take is merged at the next **loop midpoint** (`midpointFlag_`, the same moment Evolve steps, so hits already heard cannot be re-triggered by the
pattern in the same pass) and when Record is switched off or the transport stops (`hostPlaying` falls). Merge = `ProjectSession::mergeTake` inside one `edit()` (one undo step, no
family-tree node; the user presses Keep to add one). `mergeTake` algorithm. `L = lengthTicks(pattern)`, `barT = barTicks(settings)`, `g = gridTicks({division, triplet})`. For each hit:
`idx = note - baseNote`; unmapped if outside `[0, chops.size())` (counted). `t = fmod(ppqOn * 960, L)` (positive); with Quantize on `q = floor(t / g + 0.5) * g`, `if (q >= L) q -= L`;
Quantize off `q = llround(t)`. `bar = q / barT`, `start = q % barT`. Duration: Gate mode with `ppqOff >= 0`: `llround((ppqOff - ppqOn) * 960)` rounded up to a multiple of `g` (Quantize
on) and at least `g` (or 30 ticks when off); otherwise `g` (Quantize on) or `gridTicks(pattern grid)`; always `min(duration, barT - start)`. Add mode: skip if the bar already has an
event with the same chop and `start` (`skippedDuplicate`). Replace mode: for each distinct `(bar, slot)` first `deleteEvent` every unlocked event whose `start` lies in `[slotStart,
slotStart + g)` (`replaced`), then add. Add via `addEvent(p, bar, chop, start, duration, chops)` (sets `userOwned`, so Mutate/Evolve keep it), find the new id as `result.nextId - 1`,
then `setEventTransform(p, id, tx)` with `tx.level = velocityGain(curve, velocity)`. `ErrorCode::Blocked` -> `skippedLocked`; `LimitExceeded` -> `skippedCap` and stop adding. If nothing
changed, `edit()` is not called (no undo entry). Hits are applied in time order (`ppqOn`, then note). **Edge cases.** Record armed but the host reports no ppq: pads still sound, nothing
is recorded, status message above. Seek/loop in the host during recording: hits keep their own ppq, which maps to the pattern loop by `fmod`; no special handling. Pattern replaced while
a take is waiting (Generate/Mutate/load): the take is kept and merges into the new pattern (hits are positions, not event refs); loading a project or `clearSource` discards it. Mono
mode with a very short chop: the previous voice's 64-frame release overlaps; this is intended. Poly with 8 voices busy: the quietest voice is stolen with a fade (existing logic); pad
voices are not protected. Chop count above 128 - baseNote: the extra chops have no note (pads page still plays them through `padTrigger` with `note` clamped: pad N of page P sends note
`baseNote + P*16 + N - 1` only if <= 127, otherwise the pad is disabled with tooltip `"No MIDI note available for this chop; lower the base note."`). Audition (existing preview voices)
is unchanged and independent. **Determinism.** No randomness is introduced. Same MIDI + block partition -> same audio; pad timing is independent of block size because events are applied
at their exact frame (tested). Pattern generation and the four golden hashes are untouched. **Interactions.** Evolve: recorded events are userOwned, so Evolve/Mutate preserve them;
Evolve steps and merge both run at the midpoint (message thread, same lock, sequential). Locks: locked beats/bars refuse hits (counted). F08 gestures: Reverse and Tape Stop apply to pad
voices too; Stutter freezes the pattern only (pad voices unaffected). F10 morph / F09 scenes: recording is refused while a morph preview or scene playback is active (`"Stop scenes or
cancel the morph before recording."`).

## 7. Test plan
**Unit, `modules/audio_renderer/tests/test_pads.cpp`** (helpers from `test_renderer.cpp`: click source, `makePlayback`, replaced `operator new`):
`pad_note_on_starts_a_voice_on_the_exact_frame` (note at frame 100 of a 512 block: samples 0..99 are 0, first non-zero index in [101, 102]); `velocity_curves_are_exact`
(`velocityGain(Linear,127)==1`, `(Linear,64)==64/127`, `(Soft,1)==sqrt(1/127)`, `(Hard,64)==(64/127)^2`, `(Fixed,1)==1`, v clamped 1..127); `poly_layers_and_respects_the_voice_limit`;
`mono_releases_previous_pad_voice_but_not_pattern_voices`; `gate_note_off_releases_with_a_64_frame_fade_and_one_shot_ignores_note_off`;
`choke_group_releases_same_group_pad_voices_only`; `all_notes_off_releases_every_pad_voice`; `pads_sound_with_stopped_transport_and_in_pass_through_playback`;
`pad_voices_survive_transport_stop_seek_and_loop_wrap`; `unmapped_notes_and_regions_outside_the_source_are_ignored`; `pad_events_are_block_size_independent` (blocks 64, 333, 512, 2048
produce identical output); `oversize_host_blocks_rebase_pad_frames`; `output_without_pads_is_bit_identical_to_the_six_argument_process`; `pad_path_performs_no_heap_allocation`;
`pad_bank_mailbox_handoff_across_threads` (TSan); `uipad_queue_order_and_overflow` (full queue returns false, nothing lost or duplicated). **Host adapter, `test_host_adapter.cpp`:**
manifest golden gains `perf_mode` and `perf_velocity_curve` (ranges, defaults 0, step 1, `sinceVersion` 3, choice strings exactly as above); `kManifestVersion == 3`. **Integration,
`composition/tests/test_pads.cpp`:** `pad_bank_maps_chops_in_snapshot_order_from_the_base_note` (base 36, 5 chops -> slots 36..40 mapped, 35 and 41 empty; base 126 -> only 2 mapped);
`bank_is_republished_after_marker_edits_roles_and_settings`; `hat_role_defaults_to_choke_group_1_and_override_wins`;
`merge_take_quantizes_wraps_and_adds_user_owned_events_in_one_undo_step` (2 bars, 120 BPM, L = 7680: ppq 1.04 -> start 960 bar 0; ppq 7.95 -> wraps to 0 bar 0; `undo()` removes both);
`merge_take_levels_follow_the_velocity_curve`; `merge_take_respects_locks_cap_and_dedupes` (locked beat -> `skippedLocked`; 511 events + 3 hits -> 1 added, `skippedCap` 2; duplicate hit
skipped); `replace_mode_deletes_unlocked_events_in_the_same_slot_only`; `gate_duration_is_quantized_and_clipped_to_the_bar`; `empty_or_unchanged_merge_creates_no_undo_entry`;
`pads_section_roundtrips_rejects_bad_ranges_and_is_absent_for_defaults` (`decode(saveState()).modules` has no `pads` key by default; truncation and byte-flip sweeps). **Plugin shell,
`plugin/tests/test_plugin_shell.cpp` (update line asserting `!p.acceptsMidi()` to `p.acceptsMidi() && !p.producesMidi() && !p.isMidiEffect()`) and `plugin/tests/test_pads_gui.cpp`:**
`midi_note_on_triggers_the_mapped_chop_at_the_event_sample` (build `juce::MidiBuffer` `noteOn(1, 36 + k, (juce::uint8) 100)` at sample 100; use the real processor with the drum loop
session; assert silence before 100 and peak > 0.05 within 2000 samples; with a stopped playhead too); `midi_channel_filter_ignores_other_channels`;
`note_off_in_gate_mode_stops_the_voice` (set `perf_mode` to Gate via `setParam`); `all_notes_off_cc123_silences_pad_voices`; `midi_is_ignored_when_effect_enable_is_off`;
`ui_pad_queue_triggers_like_midi_one_block_later`; `recording_a_take_adds_quantized_events_at_the_midpoint` (FakePlayHead playing, `setRecordArmed(true)`, notes at ppq 1.04 and 7.95 as
in the composition test, run to the loop midpoint, `pollAudioFlags()`, assert events); `record_flushes_when_disarmed_and_when_the_transport_stops`;
`take_queue_overflow_is_counted_not_fatal`. GUI (xvfb, F00 recursive helpers and mouse helper): `pad_panel_pads_trigger_and_record`: click "Pads" to show; `findButton("Pad
1")->setState(buttonDown)`, pump, run 10 blocks: output peak > 0.05 and `padActivity_` bit set; `setState(buttonNormal)`; set "Record" toggle on, play the FakePlayHead, press "Pad 2" at
a known ppq, run to the midpoint, assert one new userOwned event with `chop == chops[1]`; right-click menu choice sets `chokeOverride`; all controls have titles. **Validator and
platform:** `tools/validate_vst3.sh` must report 0 failed (47/47 or more; the count may rise because the wrapper now exposes an event input bus); the shell test counts
`getParameters().size() == manifest.size()`. **Real-time / sanitizer:** allocation test with pad events, TSan on queues + mailbox, ASan/UBSan on all new tests incl. the `pads`
hostile-state sweep. **Manual QA:** (1) Load a loop, Detect Chops, open Pads. (2) Click pad 1 (hold, then release): sound plays; all 16 pads trigger the right slice. (3) In REAPER or
Bitwig route a MIDI keyboard to the plugin: note C1 triggers pad 1; velocity changes loudness (Linear). (4) Mode Mono: hitting pads rapidly never overlaps; Gate: releasing the key stops
the sound in about 1.3 ms. (5) Right-click a hat pad, Choke group 1 on two hats: the second silences the first. (6) Generate, press play, arm Record, play a 1/8 pattern on pads for two
loops: the hits appear in PatternPanel as protected events on the grid and Undo removes the last pass. (7) In a host that cannot route MIDI to effects (Ableton Live): the on-screen pads
still work, and the QA note in the manual says so. (8) Open the plugin in the validator, then delete it from the project: no stuck notes.

## 8. Acceptance criteria
- [ ] `acceptsMidi()` is true, `producesMidi()` and `isMidiEffect()` false, bus layouts unchanged (stereo/mono in == out), `getLatencySamples()` is 0, and the VST3 validator reports 0
      failures.
- [ ] A MIDI NoteOn at sample S produces output starting at S+1 (+-1) regardless of host block size, with the transport playing, stopped, or with no pattern yet.
- [ ] Note `baseNote + N` triggers chop N (snapshot order); default base note 36 equals the Producer Kit's `kit.txt` mapping (chop n -> note 36 + n).
- [ ] Poly, Mono, Gate, choke groups and the four velocity curves behave exactly as specified (tests).
- [ ] With no MIDI/pad input the audio is bit-identical to the previous build; golden hashes `2397844821793184813`, `12084884237330071892`, `13710596758976548913`,
      `18237151833431327128` are unchanged.
- [ ] `Renderer::process` with pad events performs no allocation, lock or logging; queues are fixed-size SPSC and pass TSan.
- [ ] A recorded take merges on the message thread as exactly one undo step, with hits quantized as specified, `userOwned` set, locks and the event cap respected, and a summary message.
- [ ] Manifest version 3 contains `perf_mode` and `perf_velocity_curve` as specified; v1/v2 IDs unchanged.
- [ ] A project that never uses pads has no `pads` section and loads/saves identically to before; a corrupted `pads` section is rejected without touching the live session.
- [ ] All new controls are labelled and keyboard-reachable; pad state is never conveyed by colour alone.

## 9. Files touched
| Path | New/Mod | Change |
|---|---|---|
| `modules/audio_renderer/include/.../pads.hpp`, `src/pads.cpp`, `tests/test_pads.cpp` | New | types, `velocityGain`, tests |
| `modules/audio_renderer/include/.../renderer.hpp`, `src/renderer.cpp` | Mod **(hot)** | `LiveInput` pad fields, pad voices, `padMailbox`, merged trigger loop, `releaseAll` change |
| `modules/audio_renderer/include/.../spsc_queue.hpp` | New or reused | from F08 |
| `modules/plugin_host_adapter/include/.../parameters.hpp`, `src/parameters.cpp` | Mod **(hot)** | two parameters, version 3 |
| `modules/plugin_host_adapter/tests/test_host_adapter.cpp` | Mod | golden manifest |
| `composition/src/pads.cpp`, `composition/tests/test_pads.cpp` | New | bank, settings, take merge, section codec |
| `composition/include/.../project_session.hpp`, `composition/src/project_session.cpp` | Mod **(hot)** | declarations, `publishPadBank()` call in `publish`, member `PadSettings` |
| `plugin/CMakeLists.txt` | Mod **(hot)** | `NEEDS_MIDI_INPUT TRUE`, new sources/tests |
| `plugin/src/PluginProcessor.h/.cpp` | Mod **(hot)** | MIDI parse, queues, record, poll, `acceptsMidi` |
| `plugin/src/panels/PadPanel.h/.cpp`, `FeaturePanel.cpp` | New/Mod | UI, "Pads" toggle |
| `plugin/tests/test_plugin_shell.cpp` | Mod | updated assertion + new tests; `test_pads_gui.cpp` New |
| `docs/specs/audio-engine-and-routing.md`, `docs/BUILD_STATUS.md` | Mod | MIDI input, host notes |

## 10. Risks and mitigations
- **Hosts that do not route MIDI to effects (Ableton Live; FL Studio varies).** On-screen pads are the guaranteed path; the manual states it. Product decision (open): ship an additional
  `VST3_CATEGORIES "Instrument Fx"` variant; not done here because it changes plugin discovery.
- **JUCE may expose MIDI CC emulation parameters** (`JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS`) once MIDI input is on, bloating the host parameter list. Check the validator output and
  a host's parameter list in step 1; if present, add `JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0` to `target_compile_definitions`.
- **Validator behavior change with an event bus.** Do the CMake/`acceptsMidi` change in its own first commit and run the validator before anything else; fallback: revert that commit
  alone.
- **Double trigger on merge** (hit heard live, then also played by the new pattern event in the same pass). Merge at the midpoint limits it to hits whose quantized position is ahead of
  the playhead; documented in QA; accepted.
- **Voice starvation** (pads steal pattern voices). Matches the existing quietest-voice policy; raising `maxVoices` is a separate decision.
- **Stuck notes** (Gate). All clear paths: NoteOff, AllNotesOff, `Renderer::reset()`, `prepareToPlay`, bypass, editor close for UI pads (`padTrigger` release on `~PadPanel`).
- **Merging into a pattern changed meanwhile.** Takes store positions only; covered by a test.

## 11. Implementation steps
1. CMake `NEEDS_MIDI_INPUT TRUE`, `acceptsMidi() true`, updated shell assertion; run the validator (alone, revertible).
   2. `spsc_queue.hpp` (if F08 absent) + `pads.hpp`/`velocityGain` + unit tests. 3. Renderer pad voices, `PadBank` mailbox, merged trigger loop, tests (bit-identical without pads,
      no-alloc). 4. Manifest v3 rows + host adapter tests.
   5. Composition: bank publishing, `PadSettings`, `pads` section + tests. 6. Processor MIDI parse, UI pad queue, shell tests. 7. PadPanel (pads, settings) + GUI tests. 8. Recording:
      take queue, `mergeTake`, midpoint flush, tests. 9. Docs, host QA matrix (REAPER, Bitwig, Live), manual QA.
