# Product and system architecture

**Status:** Draft for review

## Product definition

ChopFractal is an audio effect plugin that accepts a mono or stereo loop, divides it into playable chops, and creates rhythmic rearrangements with optional per-event transformations. The source can be imported from disk or captured from the plugin input. The plugin is designed for producers who want to turn a break, percussion loop, vocal phrase, or other rhythmic audio into controlled variations without losing the original source.

The primary workflow is: load or capture source → detect/edit chops → generate → lock useful regions → mutate the rest → play in sync with the host → render or export the result.

## Product goals

1. Produce usable rhythmic variations quickly, with repeatable results.
2. Keep the source and generated pattern visible and editable.
3. Make complexity bounded by musical controls rather than opaque randomization.
4. Preserve timing and state across DAW sessions.
5. Operate locally with no account, cloud service, or model dependency.

## Product principles

- **Deterministic by default:** a pattern is fully determined by source, settings, and seed.
- **Musical constraints:** user limits on density, subdivision, and variation are always respected.
- **Inspectable:** the UI exposes the event sequence and source chops used.
- **Recoverable:** generation, marker edits, and pattern edits support undo/redo.
- **Audio-thread safe:** the real-time callback never performs file I/O, blocking waits, or dynamic allocation.

## Supported product form

- Initial target: 64-bit VST3 audio effect on Windows and macOS, with Linux support assessed during toolchain setup.
- Recommended implementation framework: JUCE, subject to a repo/toolchain audit at the start of development.
- One stereo input and one stereo output; mono hosts are supported by duplicating mono to the internal stereo path and downmixing safely as required.
- No MIDI output, multi-output buses, standalone app, or proprietary sample library in the first milestone.
- No cloud processing or machine-learning model is required.

## Source and playback model

The source is a finite clip stored in an internal sample buffer. The plugin schedules events into the timeline and reads the selected region through one or more playback voices. It does not attempt to continuously slice arbitrary live input with zero latency. Input capture is explicit and produces a fixed clip that can be analyzed and sequenced.

With no source loaded or capture completed, the plugin passes input through unchanged. With a source active, generated playback is the primary output; a Dry/Source control determines whether current input is also mixed in. The host can bypass the effect as usual.

## Architecture boundaries

The implementation should keep the following parts independent:

1. **Plugin shell:** host buses, playhead, lifecycle, parameter exposure, state.
2. **Source model:** sample data, source metadata, markers, detected features.
3. **Analysis service:** transient detection and optional descriptive features, off the audio thread.
4. **Pattern model and generator:** deterministic event tree, constraints, edits, seed.
5. **Scheduler:** converts pattern events to sample-accurate playback commands using host time.
6. **Voice renderer:** reads source chops and applies event transforms.
7. **Output stage:** dry/source blend, output level, safety limiting only if explicitly enabled.
8. **Editor:** visualization and edits communicated through safe immutable snapshots/queues.

Audio callback reads immutable/prepared state and performs bounded processing. UI actions build replacement state outside the callback and publish it atomically or through a bounded lock-free handoff.

## Terminology

- **Source:** imported or captured audio clip.
- **Chop:** a start/end region within the source.
- **Event:** one scheduled playback of a chop, including timing and transforms.
- **Pattern:** ordered events across one or more bars.
- **Phrase tree:** nested organization of phrase, bar, beat, and event variation.
- **Lock:** a user constraint that prevents the selected scope from changing during mutation.
- **Seed:** stable integer input to deterministic generation.

## Market context and positioning

Existing products demonstrate that loop slicing, built-in sequencing, and randomized micro-slice workflows are established categories. Initial Audio Slice describes loop slicing/rearrangement and a sequencer ([product page](https://initialaudio.com/slice/)); BeatForge describes sample tracks, slicing, and MIDI pattern output ([product page](https://www.beatforge.nl/)); WHORL has been described as a microslice sequencer with randomized slice and pattern functions ([coverage](https://rekkerd.org/scrap-brain-audio-releases-whorl-microslice-sequencer-plugin-vst3-au/)). This establishes category activity, not market size or a guaranteed unmet demand.

ChopFractal should be positioned around transparent hierarchical variation, reproducible seeds, lockable scopes, and source-to-pattern editing. Do not use “first,” “only,” or “never done before” claims without a separate, current competitive review.

## Success measures

- A new user can import a loop, create a first variation, and hear it within two minutes without reading documentation.
- The same source/settings/seed produce the same event pattern across sessions and supported platforms.
- A user can preserve a useful bar while changing another bar in one action.
- Project recall restores source linkage or embedded source, markers, pattern, seed, and parameter values.
- The plugin produces no avoidable audio dropouts during the defined host test matrix.
