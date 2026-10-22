# E03 Stutter (buffer repeat)

**Status:** Implemented (renderer, codec, pass, host parameters, Sound drawer rows, tests). The Sound drawer replaces the planned call-out.

**What it does.** A hit repeats the start of itself on a note grid, like a buffer-repeat or stutter effect. Per event block `StutterFx` {rate, rateEnd, gate, tail, decay, pitch, probability, pan}, serialized in bit 1 of the event extension byte (the old has-child byte; bits 3 to 7 are reserved and rejected on read).

- **Rates.** Division indices 0 to 9 = 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, 1/64T (`kStutterDivisionTicks`). `rate == rateEnd` is constant; otherwise the rate walks one division at a time from `rate` to `rateEnd` across the hit and ends on the grid.
- **Shape.** `gate` 10 to 100 % of each repeat is audible; `tail` is the release share of that part (low = choppy, high = washy). `decay` is the per-repeat gain slope in 0.1 dB (negative decays, positive crescendos, capped). `pitch` is the total ramp over the hit in 0.1 semitone, `probability` the chance each repeat after the first plays (a hash of event id and repeat index, so deterministic), `pan` the width of the left/right alternation.
- **Rendering.** Repeats are expanded into the playback by `makePlayback` (non-real-time); the audio thread only plays voices, with the usual allocation-free, block-size-independent path. Repeats are secondary voices: they never steal primaries.
- **Host parameters** (manifest rows after E02): `allow_stutter` (bool), `stutter_amount` (0 to 100 %, default 25), `stutter_rate` (10 choices, default 1/16), `stutter_gate` (10 to 100 %, 80), `stutter_decay_db` (-12 to +6, -1.5), `stutter_pitch` (-12 to +12 st, 0). The effect pass picks events at least a 1/8 note long with probability `stutter_amount`.
- **Bypass.** `fxbit::kStutter` in `RenderParams::fxMask`; with it clear the original hit plays.
- **Tests.** Codec round trip and rejection, onset spacing on the grid, gate and tail shape, decay, pitch ramp across repeats, probability, determinism, bypass, allocation-free `process()`.
