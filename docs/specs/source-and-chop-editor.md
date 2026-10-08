# Source and chop editor

**Status:** Draft for review

## Purpose

The source subsystem imports or captures a loop, stores it safely for project recall, analyzes likely chop boundaries, and gives the user final control over those boundaries. Automatic detection is a starting point; user markers always take precedence.

## Source input

### File import

- Initial supported formats: WAV and AIFF. Additional formats can be added after dependency and licensing review.
- Accept mono or stereo PCM; resample to the current processing rate during loading or through a prepared background conversion.
- Reject corrupt, unsupported, zero-length, or unreasonably large files with a clear error and no partial state change.
- Recommended first-release limit: 60 seconds and 100 MB decoded audio, whichever is reached first. Expose these as implementation constants, not user-facing controls.
- Normalize neither the stored source nor its loudness automatically.
- Preserve channel count and original sample rate metadata.

### Input capture

- Provide explicit Arm, Capture, Stop, and Keep/Discard actions.
- Capture into preallocated memory; never allocate or wait in the audio callback.
- Recommended initial capture ceiling: 30 seconds. Stop capture at the ceiling and display that it reached the limit.
- Capture only when the user arms it. Until a captured clip is accepted, normal pass-through behavior remains.
- A capture should become an immutable source snapshot before analysis or pattern generation begins.

### Source management

- Show source name, duration, sample rate, channels, and tempo status.
- Support Replace and Clear with undo where practical.
- If the source changes, retain chop positions only when the user explicitly chooses to keep them; otherwise reset markers and ask before losing edits.
- Persist an external file reference and optionally embed a bounded copy in plugin state for portability. The exact default and maximum state size are open decisions for spec review.

## Transient analysis

- Run analysis off the audio thread.
- Provide sensitivity, minimum spacing, and analysis range controls.
- Detect candidate onset points from a combination of amplitude change and local energy; do not promise semantic drum classification.
- Enforce a configurable minimum spacing to avoid pathological clusters.
- Display confidence or candidate strength so users can distinguish strong and weak detections.
- Offer modes: Transients, Even Grid, and Manual. Even Grid offers subdivisions including straight, triplet, and dotted divisions.
- Re-running detection creates a proposed marker set. It must not silently overwrite manual markers; show preview and confirm Replace, Merge, or Cancel.

## Marker model and editing

Each chop marker has a stable ID, source sample position, enabled state, and optional label/color. Regions are the intervals between adjacent enabled markers, plus the clip end.

Required editing:
- Add, move, delete, and disable markers.
- Snap to transient candidates, grid, zero crossing, or no snap.
- Zoom and horizontal scroll; waveform overview and current view remain linked.
- Undo/redo marker edits.
- Select and audition a chop without starting the pattern.
- Set a chop’s playback trim with a short fade-in/out to reduce clicks.
- Protect minimum region length and prevent invalid overlap.

Markers are stored as integer sample positions relative to the canonical source buffer. Conversion to/from display time must not accumulate rounding drift.

## Audition

- Preview can be triggered from the marker editor or a pad-like chop list.
- Preview uses the same source read and fade behavior as pattern playback.
- Preview must not steal or corrupt the main pattern playhead.
- A Preview Through Output option is allowed if host routing makes it reliable; otherwise preview locally through the plugin output and document its transport behavior.

## Acceptance criteria

- Importing a supported file updates source state only after successful decode.
- Detection can be previewed and undone without losing manually edited markers.
- Marker positions remain stable after save/reload and sample-rate changes.
- Capture at the ceiling ends cleanly without buffer overrun or audio-thread allocation.
- At least 128 chops can be represented and edited; product UI may recommend fewer for legibility.
- Empty, invalid, and missing-file cases have explicit UI feedback and safe audio behavior.
