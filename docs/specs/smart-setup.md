# Smart Setup

**Status:** Creation spec  
**Module ID:** `chop_insight`  
**Purpose:** The usability feature. One click takes a freshly loaded loop from "audio file" to "ready to generate": it finds the chops, **suggests a role for each chop** (kick, snare, hat, cymbal, other) from the audio itself, and **suggests the loop's length and tempo**. Nothing is guessed silently: suggestions are shown, and the user's existing tags and tempo are never overwritten.

## User behavior

1. **Smart Setup** detects chops (merging with any markers the user already placed) and analyzes each chop.
2. Role suggestions appear with a confidence and a plain-language reason ("low energy, short body"). **Accept** applies them to chops that have no role yet; existing roles are never changed.
3. Loop suggestions list the plausible bar counts with the tempo each implies ("4 bars at 96.0 BPM"), best first, favoring the host tempo when the host provides one. **Use tempo** copies a chosen tempo into the manual-tempo fallback and the even-grid length; it is never applied automatically and never changes the host's tempo.
4. Everything remains editable; Smart Setup can be run again.

## Analysis (deterministic, no machine learning, no external model)

Per chop (using the audio inside its range): energy split into low (below about 200 Hz), mid and high (above about 5 kHz) bands using fixed one-pole filters; zero-crossing rate; peak and RMS; and decay time (time for the envelope to fall 30 dB below its peak, capped at the chop length).
Rules, in priority order, with confidence from how far the features sit past the thresholds:
- **kick**: low-band energy dominates and the zero-crossing rate is low.
- **cymbal**: high-band energy dominates and the decay is long (over about 300 ms).
- **hat**: high-band energy dominates or the zero-crossing rate is very high, and the decay is short.
- **snare**: broadband noisy content with a moderate body and decay.
- **other**: everything else (low confidence).
Role names are the built-in role IDs of `chop_roles_grammar`; this module does not depend on it.

Loop suggestion: for a clip of S seconds, candidate lengths of 4, 8, 16 and 32 beats (1, 2, 4, 8 bars of 4/4) imply a tempo of `beats * 60 / S`; candidates between 60 and 200 BPM are kept and ranked by closeness to the host tempo if given, otherwise to a musical sweet spot around 100 to 130 BPM. The top candidate's confidence is lower when another candidate is also plausible.

## Contract

`chop_insight` (depends on `chop_contracts`):
- `analyzeChop(samples, channels, range, sampleRate) -> Result<ChopFeatures>`
- `suggestRoles(SourceView, ChopSnapshot) -> vector<RoleSuggestion{chop, role, confidence, reason}>`
- `suggestLoop(seconds, hostBpm) -> vector<LoopSuggestion{beats, bars, bpm, confidence, note}>`
Pure functions; thread-safe; not real-time safe.

Composition: `ProjectSession::suggestSetup()` (no state change), `acceptRoleSuggestions(chops)`, `smartSetup()`.

## Tests and acceptance

- Synthetic kick, snare, hat and cymbal classify as intended, and mixed loops classify per chop.
- Silence, very short chops and invalid ranges return errors or the low-confidence "other", never a crash.
- Loop suggestions are correct for known lengths (for example 8 s of 4 bars gives 120 BPM) and respect the host tempo preference.
- Accepting never overwrites an existing role; Smart Setup never changes tempo or markers the user placed.
- Results are deterministic.

## Migration to another project

Copy `modules/chop_insight/` and `modules/chop_contracts/`; pass decoded sample pointers and a chop snapshot.
