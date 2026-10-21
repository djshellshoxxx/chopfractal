# Input capture

**Status:** implemented (module `input_capture`, shell wiring in `plugin/src/PluginProcessor.*` and `PluginEditor.*`).
**Purpose:** record a loop straight from the plugin's audio input (a DAW track, or the stand-alone app's input device), then chop and generate from it exactly like an imported file.

## User flow

1. Choose a **Capture length**: *Free* (until Rec Stop, at most 60 s) or 1, 2, 4 or 8 bars.
2. **Rec Arm.** A bar-length capture waits for the next bar line while the DAW (or local Play) is running, and starts at once when no transport is running. It stops by itself after exactly that many bars at the current tempo and meter.
3. **Rec Stop** ends a free capture, or disarms one that has not started.
4. **Rec Keep** loads the finished audio as the project's source and embeds it in the project (there is no file to reload it from). Detect Chops and Generate then work as usual. Re-arming a finished capture discards it.

In the stand-alone app the audio input is muted by default to prevent feedback; unmute it in Options > Settings before capturing.

## Contract (`modules/input_capture`)

- `Capture::arm(Options)`, `stop()`, `cancel()`, `take()` run on one control thread; `process()` runs on the audio thread. They share only atomics; `process()` never allocates, locks or waits.
- Sample-accurate: the capture starts on the exact frame of the bar line inside a block and ends on the exact target frame. The captured audio does not depend on the host block size.
- Limits: 60 seconds, 100 MB, mono or stereo (a mono input fills both channels of a stereo capture; missing buffers become silence; non-finite samples become 0).
- A Stop or Cancel of a Recording capture takes effect on the next `process()` call; stopping an Armed capture is immediate.

## Tests and acceptance

- Module: immediate and bar-aligned start (including exactly on a bar line and a bar line in a later block), exact target length, block-size independence (1 to 4096), channel mapping and bad input, state machine and limits, the 60 s cap, no allocation in `process()`, a control-thread/audio-thread stress test (also run under ThreadSanitizer in CI).
- Shell: a one-bar capture through the real processor and host transport, kept, chopped, generated, played, saved and restored into a fresh instance with no file on disk; the capture buttons follow the capture state.

## Not covered yet

Count-in or pre-roll, monitoring the input while recording, trimming silence from the start, and a visual level meter (planned with the meters work).
