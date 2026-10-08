# Testing and acceptance

**Status:** Draft for review

## Test layers

### Unit tests

- Chop marker validity, ordering, conversion, and source boundaries.
- Deterministic generation and seed behavior.
- Lock preservation and mutation scope.
- Event caps, density constraints, and transformation ranges.
- State serialization, migration, corruption rejection, and source hash checks.
- Scheduler conversion from musical position to sample frame.

### DSP tests

- Mono/stereo and sample-rate conversion.
- Reverse, pitch/resample, gate, fade, retrigger, pan, and level behavior.
- No NaN/Inf output from silence, denormals, extreme valid values, or malformed host timing.
- Edge continuity and voice-stealing behavior.
- Deterministic render comparison for fixed inputs.

### Plugin tests

- VST3 validation with the current validator available at implementation time.
- Bus layout and mono-host behavior.
- Parameter IDs, ranges, defaults, and automation.
- State save/restore, preset recall, bypass, suspend/resume, and editor reopen.
- Host transport start, stop, seek, loop wrap, tempo and meter change, and missing playhead data.

### Host matrix

Select representative hosts during implementation. At minimum, test current supported versions of:
- REAPER
- FL Studio
- Ableton Live
- Cubase
- One additional host with strict plugin validation if available

Record OS, host, plugin format, sample rate, buffer size, and test outcome for each run. Do not claim host compatibility based only on a successful standalone test.

## Performance and reliability

- Test block sizes 32, 64, 128, 256, 512, 1024, and 2048 frames.
- Test 44.1, 48, 88.2, 96, and 192 kHz where the host and system support them.
- Test maximum specified source length, chop count, event count, and active voices.
- Instrument the callback in debug builds to detect allocation, locks, and unexpected work.
- Run long-duration playback, repeated source replacement, and repeated state restore.
- Establish CPU and memory thresholds on a documented reference workstation before beta.

## Manual usability tests

A producer should be able to:
1. Load a loop and hear it pass through before generation.
2. Detect chops, correct one marker, and audition a chop.
3. Generate a variation, lock one bar, and mutate another.
4. Save/reopen the DAW project and hear the same pattern.
5. Recover a missing source file or see a clear error.

Record friction points and revise the GUI before release.

## Release gates

A release candidate must have:
- All critical unit and DSP tests passing.
- No known crash, corrupted state, stuck voice, or audio-thread safety defect.
- Documented compatibility results for the host matrix.
- Clean plugin validation.
- Installation, uninstallation, and project recall verified on each supported OS.
- Versioned release notes and a reproducible build record.
