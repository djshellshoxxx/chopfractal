# WAV export and Producer Kit export

**Status:** Creation spec  
**Module IDs:** `wav_export`, `midi_export` (portable), composition export actions  
**Purpose:** Get the generated groove out of the plugin: a rendered WAV of the pattern, and a Producer Kit (every chop as its own WAV plus a MIDI file that replays the pattern on those slices) so the result can be rebuilt in any sampler or drum rack.

## Decisions

- WAV export is **in the first release** (decided). It uses the same `Renderer::process` path as live playback, so exported audio matches what is heard.
- Producer Kit export is the paid-value feature: it makes ChopFractal a slicing and groove-design tool whose output leaves the plugin.
- Direct MIDI *output* from the plugin stays out of scope (host-dependent). Exporting a MIDI **file** is independent of host routing and is in scope.

## WAV export behavior

- Render the whole pattern for N loops (1 to 16) at a chosen sample rate (44.1, 48, 88.2, 96, 176.4 or 192 kHz), at a chosen tempo (host tempo when available, else the manual tempo), plus a configurable tail (default 0.5 s, 0 to 30 s) during which sounding voices ring out and no new events start.
- Bit depth: 16-bit PCM, 24-bit PCM (default), or 32-bit float. 16-bit uses deterministic TPDF dither (seeded); 24-bit and float are not dithered.
- Peak normalization is **off by default**; when on it scales to a ceiling (default -1 dBFS). Values that clip when converted to PCM are clamped and counted; the export reports the peak and the number of clipped samples.
- Stereo output. Output is deterministic: the same project, tempo, sample rate and options produce byte-identical files.
- Files are written to a temporary name and atomically renamed on success. An existing file is **never** overwritten unless the caller sets `overwrite` after the user confirmed.

## Producer Kit behavior

- One WAV per enabled chop at the source's own sample rate and channel count, with a short fade-in (32 frames) and fade-out (64 frames, or the chop's own fades) so slices do not click. Files are named `slice_NN_<role or label>.wav` (names sanitized to `[a-z0-9_-]`).
- `pattern.mid`: a Standard MIDI File (format 0, 960 ticks per quarter, matching the pattern's tick grid exactly) with a tempo event, a time signature event, and one note per played hit. Chop *n* maps to MIDI note `baseNote + n` (default base 36, so the kit lands on the usual drum-pad range). Velocity comes from the event level (1 to 127). Note length is the event duration, shortened where the same note would overlap. Retriggered events export as their individual sub-hits.
- `kit.txt`: plain text mapping note number to file name, role, and source range.
- MIDI cannot express reverse, pitch, glide, filter or crunch; those are documented as audio-only and are present in the WAV export.
- The export refuses (before writing anything) if any target file already exists and overwrite was not confirmed.

## Contracts

`wav_export` (depends on `chop_contracts`):
- `encodeWav(planarSamples, sampleRate, WavOptions) -> bytes` (mono or stereo; reports peak and clipped count), `decodeWav(bytes)` (used for verification and tests), `writeFileAtomic(path, bytes, overwrite)`, `sanitizeFileName(text)`.

`midi_export` (depends on `chop_contracts`):
- `encodeMidi(MidiFileSpec) -> bytes` and `decodeMidi(bytes)` (tests/verification). Notes are validated (0 to 127, velocity 1 to 127, positive length); output is deterministic.

Composition: `ProjectSession::exportWav(path, ExportWavOptions)`, `exportKit(directory, KitOptions)`.

## Limits

Render length at most one hour; WAV size at most 4 GiB (RIFF limit); at most 256 slices; MIDI at most 512 prepared events per pattern times the loop count.

## Tests and acceptance

- WAV: header correctness, 16/24/float round trips within quantization, dither determinism and bounds, normalization, clipping count, mono and stereo, malformed input rejected by the decoder.
- Atomic write: temp file never left behind on failure, overwrite refused unless confirmed, existing target untouched on failure.
- MIDI: decode(encode(x)) returns the notes, tempo and meter; note-off ordering at equal ticks; overlap clipping; invalid notes rejected.
- Kit: pre-flight collision refusal writes nothing; slice audio equals the source range; MIDI note count equals the number of played hits; note numbers follow the chop order.
- Exported WAV equals `renderOffline` output within the quantization step.

## Migration to another project

Copy `modules/wav_export/` or `modules/midi_export/` plus `modules/chop_contracts/`. Both are pure byte encoders with a small filesystem helper; no audio engine or host types are involved. See each module's `MIGRATION.md`.
