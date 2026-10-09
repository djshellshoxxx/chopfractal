# Evolve Mode

**Status:** Creation spec  
**Module ID:** `evolve`  
**Purpose:** The fun feature. Switch Evolve on and the beat **slowly mutates itself while it plays**: every few loops the pattern takes a small, controlled step. Locked bars, locked beats and the user's manual edits stay put, so you can protect the parts you love and let the rest wander. Because every step comes from a seed, a wander you like can be replayed exactly or kept as a named branch.

## User behavior

- Controls: on/off, **Every** 1, 2, 4, 8 or 16 loops, **Amount** 0 to 1 (how much of the unlocked pattern each step may change), and an optional **Intensity ramp** that increases the amount a little with each step (a build-up) up to a ceiling.
- A step is the same operation as the Mutate button (so locks, user-owned content, roles grammar and event caps all apply), using a seed derived from the start seed and the step number.
- Steps happen **in the middle of a loop**, so the new pattern is ready before the next loop begins and playback never glitches at the loop boundary.
- Steps are undoable one by one, but Evolve does **not** flood the variation family tree: only an explicit **Keep** records the current pattern as a branch (labelled "Evolve keeper").
- Evolve stops by itself after 3 consecutive failed steps (for example everything locked) and says why. It never runs when no pattern exists, when the transport is stopped, or while a quantized variation switch is pending.
- Project state stores the Evolve settings (always loaded switched off, so a project never starts changing unprompted).

## Contract

`evolve` (no dependencies):
- `Settings{enabled, everyLoops, amount, ramp, rampCeiling, startSeed}`; `validate(settings)`.
- `Controller`: `start(settings)`, `stop()`, `onLoopMidpoint() -> optional<Step{index, seed, amount}>`, `reportFailure()/reportSuccess()`, `stepsTaken()`, `failures()`.
- `seedForStep(startSeed, stepIndex)` and `amountForStep(settings, stepIndex)` are pure and deterministic.

Composition: `ProjectSession::setEvolve(settings)`, `evolveStep()` (called from the plugin at each loop midpoint), `keepEvolved(label)`.

## Limits

Every-loops 1 to 16; amount and ramp 0 to 1; at most 64 steps are remembered for display; steps are never taken faster than once per loop.

## Tests and acceptance

- The same start seed and settings reproduce the same sequence of seeds and amounts; the sequence is independent of wall-clock time.
- Steps occur on the configured cadence; disabled or invalid settings never step; the ramp is monotonic and capped.
- Locked content is byte-identical after any number of steps; the event cap holds.
- Three consecutive failures stop Evolve and produce a notice; history does not grow unless Keep is pressed.
- Replaying N steps from a reloaded project (same start seed) reproduces the same pattern.

## Migration to another project

Copy `modules/evolve/` (it has no dependencies). Drive `onLoopMidpoint()` from your own transport and apply each step with your own mutate function.
