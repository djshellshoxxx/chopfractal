# Event effects: Filter, Tape Glide, Crunch

**Status:** Creation spec  
**Modules touched:** `chop_contracts` (data), `pattern_engine` (generation, edits, state), `audio_renderer` (DSP), `plugin_host_adapter` (parameters)  
**Purpose:** Three per-hit effects chosen because they suit chopped loops: tone shaping, movement, and character. All are per event, so they can be generated, edited, locked, zoomed into and exported like any other event property.

## The effects

1. **Filter** (tone shaping). A 12 dB/octave state-variable filter, low-pass or high-pass. Cutoff is a normalized control mapped logarithmically, `f = 20 Hz * 1000^c` (20 Hz to 20 kHz, never above 0.45 times the sample rate); resonance 0 to 1 maps to Q 0.71 to 8. The spec's optional filter, now approved.
2. **Tape Glide** (movement). The playback rate ramps exponentially across the hit by a number of semitones (-24 to +24). Negative values give a **tape stop**, positive a rise. The ramp is per sample, so it is independent of block size.
3. **Crunch** (character). Combined bit-depth and sample-rate reduction controlled by one amount 0 to 1: bit depth falls from 16 to 4 bits and the sample hold grows from 1 to 16 frames as the amount rises (hold grows with the square of the amount, so low settings stay subtle).

## Signal order (extends the fixed transform order)

source region -> reverse -> playback rate (pitch, then glide) -> **crunch** -> gate and fades -> **filter** -> level and pan -> voice sum. Crunch comes before the envelope so the fades stay clean; the filter comes after the envelope as the original spec's order requires. Changing this order requires listening tests and a state-version bump.

## Data

`EventFx { filter: Off|LowPass|HighPass, cutoff 0..1, resonance 0..1, glideSemitones -24..24, crush 0..1 }` on every event and flat event. Defaults are all off, and events with default effects serialize **exactly as before** (a flag bit marks the presence of an effect block), so existing projects load unchanged and existing golden pattern hashes stay valid. Children inherit the parent's effects unless transform inheritance resets them. Retriggered sub-hits keep the effects.

## Generation and control

- Pattern settings gain `allowFilter`, `allowGlide`, `allowCrunch` (default off) and `fxIntensity` 0..1 (default 0.5). When enabled, each event draws effects from its own random stream with probabilities scaled by the event variation level and magnitudes scaled by intensity: low-pass cutoffs fall toward 35%, high-pass toward 65%, glide is mostly downward (tape stop, 70%), crunch up to 0.9. New random draws are appended to each event's stream, so output for projects that do not enable effects is unchanged.
- Host parameters (appended, manifest version 2): `allow_filter`, `allow_glide`, `allow_crunch`, `fx_intensity`.
- `setEventFx(pattern, event, fx)` edits one hit (refused inside locked scopes); effects are part of Mutate like any other event property, and locked or user-owned events keep theirs.

## Real-time rules

Coefficients are computed when a voice starts (one `tan` and one `exp2`); per-sample work is a few multiplies. All state lives in the preallocated voices. Non-finite values are replaced and filter state is flushed against denormals; output is never NaN or infinite for any input.

## Tests and acceptance

- Filter: a low-pass attenuates a high sine and passes a low one by the expected amounts; a high-pass does the reverse; resonance raises the peak; cutoff above Nyquist is clamped; silence stays silence.
- Glide: a rising ramp consumes the source faster (measured by the consumed range), a falling ramp slower; zero glide equals no effect bit-exactly.
- Crunch: output takes at most 2^bits distinct levels and holds values for the hold length; amount 0 is bit-exact bypass.
- Default effects are bit-exact with the pre-effect renderer; output is identical for block sizes 1 to 2048; no allocation in `process()`; sanitizer-clean.
- Serialization round-trips events with and without effects; old (effect-free) state loads; corrupt effect blocks are rejected; generation with effects is deterministic and respects the caps and locks.

## Migration to another project

`EventFx` is plain data in `chop_contracts`; the DSP lives in `audio_renderer` and has no other dependency.
