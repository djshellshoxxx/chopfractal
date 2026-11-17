# User Guide

## What it does

You load a loop. ChopFractal finds chop points (or you place them) and then generates variations of the loop. Everything is organised as:

- **Phrase**: the whole loop
- **Bar**: one bar of the phrase
- **Beat**: one beat in a bar
- **Hit**: one chop, played as a hit

The generator uses a seed. The same seed and settings always give the same pattern, so you can recall a result.

## Main features

- **Chop and generate.** Detect or place chops. Generate variations with the same seed for the same result. Set roles and rules for the parts. Locks keep the bars, beats or hits you like.
- **Recursive Hit Zoom.** Open any hit into a nested rhythm of its own, up to three levels deep.
- **Fractal Rhythm.** Type a motif such as `x.xx` and apply it at every scale. Each hit subdivides by the same motif.
- **Evolve.** The beat mutates as it plays. Locked bars and your edits stay. Every step can be reproduced from its seed.
- **Smart Setup.** Detects chops, suggests kick, snare, hat and cymbal roles from the audio, and suggests bars and tempo. Nothing is applied until you accept it.
- **Orbit View.** The phrase as concentric rings, one ring per chop, with a moving playhead.
- **Per-hit effects.** A state-variable filter, tape glide (tape stop and rise) and bit or sample-rate crunch. Each one can be edited and locked like other hit properties.
- **Variation family tree.** Every Generate, Mutate and Zoom is kept as a branch. You can go back to any of them.

## Export

- **WAV**: 16-bit, 24-bit or 32-bit float, up to 192 kHz
- **Producer Kit**: every chop as its own WAV, plus a MIDI file that replays the pattern on those slices

## Plug-in

ChopFractal is a VST3 effect. The alpha builds are unsigned, so your OS may ask you to confirm the first launch. The beta release workflow builds Standalone, VST3 and CLAP for Windows x64 (see `docs/RELEASING.md`).

## Not finished yet

- Live input capture
- Final GUI polish
- Compatibility testing with real hosts
- Installers and signing

See `docs/BUILD_STATUS.md` for the feature-by-feature status.
