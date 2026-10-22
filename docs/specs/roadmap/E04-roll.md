# E04 Roll (beat-repeat freeze and roll)

**Status:** Implemented (renderer, codec, pass, host parameters, Sound drawer rows, tests). The Sound drawer replaces the planned call-out.

**What it does.** A beat-repeat style gesture: the hit is frozen at a point inside the chop and that grab repeats, speeding up from `rate` to `rateEnd`, with the level rising from `velStart` to `velEnd` percent of the hit's level. Event block `RollFx` {rate, rateEnd, velStart, velEnd, freeze, flags, pitch}, serialized in bit 2 of the event extension byte (see E03).

- **Freeze.** `freeze` 0 to 100 % is where in the chop the grab starts; the grab length is one repeat. Flag bit 0 `kRollPalindrome`: odd repeats play backwards. Flag bit 1 `kRollSmooth`: geometric acceleration; otherwise the rate steps through the divisions.
- **Voice quality.** Each repeat gets a short release (12 % of its length), and a per-repeat jitter (gain +/-2.4 %, pitch +/-0.3 st, derived from a hash) so a roll does not sound machine-gunned. This is part of the slice-quality stage and is active when `slicePolish` is on.
- **Host parameters:** `allow_roll`, `roll_amount` (0 to 100 %, default 20), `roll_rate` (division, default 1/16), `roll_rate_end` (default 1/32), `roll_freeze` (0 to 100 %, default 0). Bypass bit `fxbit::kRoll`. Stutter wins when the pass picks both for one hit.
- **Tests.** Codec, repeat spacing against the rate ramp, velocity ramp, palindrome direction, frozen start, determinism, bypass.
