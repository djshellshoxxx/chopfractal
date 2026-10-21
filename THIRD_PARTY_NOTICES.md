# Third-party notices

Binary builds of ChopFractal (VST3, CLAP) include or link the following. ChopFractal's own code is all rights
reserved (see `LICENSE`); the components below keep their own licenses.

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 8.0.15 | Plugin framework, GUI, audio file decoding | AGPLv3 or the JUCE commercial license. Distributing a binary that links JUCE under the AGPL requires offering its complete corresponding source; a closed-source release needs a JUCE commercial license. |
| VST3 SDK (bundled with JUCE) | VST3 plugin format | Steinberg VST3 license (the SDK is dual-licensed with GPLv3) |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | CLAP plugin format wrapper | MIT |
| [CLAP](https://github.com/free-audio/clap) headers | CLAP plugin format | MIT |

## Data

The rhythm tables of the break engine (`modules/break_engine/src/tables.cpp`) are derived statistics (per-step onset
probabilities and velocity means for kick, snare and ghost notes, per genre) computed from the
[Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) by Gillick, Roberts, Engel, Eck and Bamman
(2019), "Learning to Groove with Inverse Sequence Transformations", ICML. The dataset is licensed under
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/). Only aggregate
numbers are embedded; no recording or MIDI file from the dataset is included. The derivation is documented in
`docs/research/BREAK_ANALYSIS.md`.

Alpha builds are for internal testing only. Resolve the licensing route in `docs/LICENSING.md` before sharing
any binary outside the project.
