# Break analysis: real drum patterns for the engine

Scope: measured patterns from openly licensed drum data, reduced to statistics the engine can embed.
Machine-usable output: `docs/research/break_stats.json`. Scripts: `tools/analysis/`.
No source audio or MIDI is committed. Only derived numbers are.

## Method

Reproduce: `python3 tools/analysis/run_all.py <workdir> docs/research/break_stats.json` (needs numpy, scipy, librosa, soundfile, mido; network to storage.googleapis.com and raw.githubusercontent.com).

- `fetch_data.py` downloads sources into a work dir outside the repo.
- `groove_stats.py` (Groove MIDI Dataset): 4/4 files only. Note numbers map to roles K (35/36), S (37/38/40), H (42/44), O (46), R (51/53/59), C (49/52/55/57), T (toms). Each onset is placed on a 16-step bar grid using tick position; the residual is the microtiming offset in fractions of a 16th (negative = early; ms uses the file bpm). Per step and role: onset probability per bar, mean and sd of velocity (/127). Ghost snare = snare below 0.5 x the file's median backbeat snare velocity and not on step 4 or 12. Bar similarity = Jaccard of the (role x step) grid between a bar and the previous one, grouped by bar index mod 4. IOI = gap in steps between distinct onset steps within a bar. The last partial bar of each file is dropped. Beat-type files and fill-type files are separate (`fills`).
- `audio_stats.py` (loops): resample 22.05 kHz, tempo = whole-bar count x 240 / duration (loops are seamless, bar count chosen closest to librosa's tempo), band-split spectral flux (30-130 Hz, 180-1800 Hz, 6-14 kHz), onset pick on the summed envelope, role from band flux plus spectral centroid. Velocity = RMS of the 50 ms after the onset relative to the loudest onset.
- `run_all.py` merges both into `break_stats.json`.

Data sources reached: Groove MIDI Dataset (yes, direct via proxy), Sonic Pi samples (yes, via raw.githubusercontent.com; the GitHub API for that repo was refused, so file names came from its README). Not attempted: IDMT-SMT-Drums, ENST-drums, MedleyDB, FreePats, Hydrogen (not shown to be reachable from the allowlist, and the two sources above already cover MIDI plus audio). Say so if a further source matters.

## License table

| Source | Files used | License | Committed? |
|---|---|---|---|
| Groove MIDI Dataset v1.0.0, Magenta (Gillick et al. 2019), `groove-v1.0.0-midionly.zip` | 409 beat files + 91 fill files, 4/4, funk/hiphop/soul/dance/afrobeat/rock/jazz/latin | CC BY 4.0 (attribution: Magenta Groove MIDI Dataset) | No, stats only |
| Sonic Pi `etc/samples/loop_amen.flac` (freesound 43389, Dolfeus) | analysed | CC0 per `etc/samples/README.md` | No |
| `loop_amen_full.flac` (freesound 202537, gowers) | analysed | CC0 per README | No |
| `loop_breakbeat.flac` (freesound 160354, Snapper4298) | analysed | CC0 per README | No |
| `loop_compus.flac` (212288, godspine), `loop_garzul.flac` (213524, Garzul) | analysed, low confidence | CC0 per README | No |
| `loop_mika`, `loop_safari`, `loop_tabla`, `loop_industrial` | analysed, rejected (not usable drum kits, or no whole-bar tempo fit) | CC0 per README | No |

Sonic Pi code is MIT; the sample directory README states the CC0 dedication. Attribution is not required for CC0 but is given. The Groove dataset requires attribution if the stats are published or shipped: add it to `THIRD_PARTY_NOTICES.md` when the engine embeds the tables.

## Results (Groove MIDI, beat files, bar = 16 steps)

Corpus sizes: funk 53 files / 2426 bars (median 100 bpm); hiphop 33 / 837 (90); soul 28 / 592; dance 7 / 545; afrobeat 13 / 976; all styles 409 / 17471; all styles at 120 bpm or faster 116 / 5923 (127 bpm), used as the closest proxy for breakbeat/dnb-adjacent tempo. The dataset has no jungle or dnb style; dance (7 files) is the thinnest group. Treat it as indicative.

Onset probability per step, percent (steps 0..15):

| Group, role | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| funk K | 87 | 4 | 31 | 45 | 8 | 22 | 12 | 35 | 34 | 14 | 52 | 22 | 8 | 16 | 13 | 8 |
| funk S | 28 | 43 | 19 | 9 | 82 | 43 | 32 | 67 | 34 | 57 | 21 | 28 | 72 | 40 | 41 | 71 |
| funk H | 53 | 18 | 44 | 28 | 45 | 18 | 47 | 28 | 44 | 21 | 33 | 23 | 63 | 17 | 38 | 15 |
| fast (>=120) K | 73 | 9 | 13 | 16 | 24 | 4 | 33 | 8 | 55 | 7 | 20 | 15 | 26 | 7 | 34 | 9 |
| fast S | 25 | 23 | 28 | 24 | 62 | 24 | 24 | 30 | 33 | 30 | 36 | 23 | 57 | 26 | 30 | 32 |
| fast H | 32 | 5 | 21 | 13 | 50 | 5 | 22 | 9 | 32 | 5 | 21 | 13 | 48 | 5 | 20 | 8 |
| fast R | 35 | 1 | 18 | 6 | 37 | 3 | 24 | 9 | 37 | 2 | 20 | 5 | 32 | 2 | 22 | 8 |
| hiphop K | 80 | 3 | 41 | 33 | 6 | 19 | 38 | 34 | 45 | 23 | 39 | 10 | 18 | 18 | 21 | 30 |
| hiphop S | 11 | 13 | 10 | 9 | 83 | 14 | 17 | 32 | 26 | 25 | 14 | 20 | 70 | 30 | 25 | 20 |
| dance K | 90 | 2 | 8 | 33 | 44 | 2 | 11 | 7 | 86 | 3 | 8 | 33 | 45 | 27 | 9 | 7 |
| dance S | 3 | 20 | 9 | 7 | 91 | 16 | 7 | 32 | 12 | 28 | 22 | 13 | 83 | 24 | 25 | 38 |

Mean velocity (percent of 127), funk S: 25 34 46 46 87 40 39 42 42 43 51 55 85 47 65 38; funk K: 58 26 50 39 50 49 37 41 52 40 51 37 63 40 44 26; hiphop S backbeats 94 and 91 vs 32-62 elsewhere. Full tables for every group, plus sd, O/R/C/T roles and per-step ms offsets, are in the JSON.

Snare accents and ghosts:

| Group | Snare velocity p5 / p25 / p50 / p75 / p95 | Ghost share of snare onsets | Ghosts per bar |
|---|---|---|---|
| funk | .10 / .24 / .39 / .94 / 1.0 | 0.50 | 3.5 |
| hiphop | .09 / .27 / .80 / 1.0 / 1.0 | 0.40 | 1.7 |
| dance | .10 / .26 / .60 / 1.0 / 1.0 | 0.28 | 1.2 |
| fast (>=120) | .09 / .28 / .43 / .82 / 1.0 | 0.25 | 1.3 |

Snare velocity is bimodal: backbeat accents near 0.85 to 1.0, ghosts at 0.1 to 0.35.

Timing (offset in fraction of a 16th, negative = early; ms at the file tempo):

| Group | Timing sd | Offbeat 8th mean offset | Odd-16th mean offset | Hat/ride 8th ratio proxy |
|---|---|---|---|---|
| funk | 0.198 | -0.102 (about -14 ms at 100 bpm) | +0.013 | 0.90 |
| hiphop | 0.183 | -0.088 | -0.010 | 0.92 |
| dance | 0.195 | -0.126 | -0.019 | 0.88 |
| fast | 0.234 | -0.051 | -0.031 | 0.95 |
| all | 0.216 | -0.058 | -0.022 | 0.94 |

Even steps (0, 2, 4, ...) are played 9 to 18 ms early in funk (ms table in JSON); odd steps sit near zero or a few ms late. Caveat: the grid has no reference to the performer's real tempo drift, so the absolute early bias partly reflects the click-recorded setup. Use the relative pattern (even steps early, odd steps on grid or slightly late, sd about 0.2 step = 8 to 12 ms at 100 to 130 bpm). Triplet swing is not captured (the grid snaps to 16ths); no group shows a 16th-level swing (odd mean offset is about 0).

Inter-onset interval, share of gaps between distinct onset steps (steps): funk 1: 75%, 2: 20%, 3: 3%, 4: 2%. Fast: 1: 60%, 2: 30%, 3: 6%, 4: 4%. Hiphop: 64 / 29 / 4 / 3. Gaps beyond 4 steps are under 0.5%.

Kick/snare co-occurrence, P(kick at step | snare at step 4) (and 12 in brackets):

| Kick step | funk | fast | hiphop | all |
|---|---|---|---|---|
| 0 | .90 (.90) | .83 (.79) | .90 (.88) | .86 (.84) |
| 4 | .05 (.10) | .23 (.25) | .04 (.05) | .23 (.25) |
| 8 | .36 (.39) | .67 (.65) | .47 (.43) | .55 (.54) |
| 10 | .58 (.54) | .28 (.30) | .47 (.38) | .41 (.40) |

Funk and hiphop almost never put kick and snare on the same backbeat step (5 to 10%); other genres (rock with the kick doubled on the snare, jazz comping) push the all-style figure to .23.

Bar to bar variation. Similarity of bar n to bar n-1 by bar index mod 4 (index 0 = first bar of a phrase vs the last of the previous one): funk .44 / .53 / .60 / .51; hiphop .41 / .51 / .53 / .42; fast .42 / .49 / .54 / .48; all .48 / .55 / .60 / .53. Onsets per bar by mod 4 (bar 4 of the phrase is index 3): funk 17.5 / 18.7 / 17.7 / 20.2; all 17.4 / 17.8 / 17.7 / 18.7; afrobeat 19.1 / 19.7 / 19.3 / 22.0; dance 14.2 / 13.8 / 14.3 / 16.5. So bar 4 is about 8 to 15% denser than bars 1 to 3, and the break between bar 4 and the next bar 1 is the least similar boundary. Even the "same" bars share only 0.5 to 0.6 of their (role x step) cells, so real players vary about half the cells bar to bar (mostly ghosts and hats).

Fill files (91 files, 204 bars): onsets per bar by mod 4 are 11.8 / 15.1 / 17.5 / 16.8, toms about 10 to 20% at each late step, snare 28 to 61% per step across steps 8 to 15 (JSON `fills`).

## Results (audio loops, heuristic roles)

Role labels are validated only on the Amen break against its known structure; others are low confidence. Measured timing sd is 0.04 to 0.06 step, and offsets read 0.05 to 0.15 step early on every loop, which is an onset-detector bias (peak flux precedes the envelope peak), not groove. Ignore absolute audio offsets; sd is the useful number (these loops are quantised or tight: sd about 0.05 vs 0.2 for live drummers).

Step grids (x = onset, bar of 16 steps):

| Loop | bpm | Bars | Kick | Snare |
|---|---|---|---|---|
| loop_amen | 136.9 | 1 | `x.x.......xx....` | `....x....xx.x..x` |
| loop_amen_full bar 1 | 140 | 4 | `x.x.............` | `....x....xx.x...` |
| bar 2 | | | `x.x....x..xx....` | `....x....x..x..x` |
| bar 3 | | | `..x.............` | `....x..x.x....x.` |
| bar 4 | | | `..xx............` | `.x.....x.x....x.` |

Amen facts: kick on step 0 (all bars but 3 and 4 shift to step 2), snare on step 4 in every bar (rel. velocity .71), second snare on step 12 in bars 1 to 3 (.73), step 10 snare/kick pair (.53 vs .60) recurring in bars 1 to 3, ghost snares at steps 7, 9, 15 (rel. velocity .24 to .45). Bar 4 breaks the backbeat (snare on step 1, 7, 9, 14; kick on 2 and 3), i.e. variation is concentrated in the last bar, matching the Groove MIDI trend. Bar-to-bar similarity of the full loop is in the JSON (`bar_similarity_consecutive`).
`loop_breakbeat` (126 bpm, 1 bar): kick at 0 and 6, eighth-note hats on every even step; snare detection failed, treat as unclassified. `loop_compus` and `loop_garzul` give kick at 0, 6, (7) and 0, 8, (12), snare at 4 and 12 with early pickups, usable only as a sanity check.

## Rules the data supports

1. Step 0 kick ~0.75 to 0.90 per bar in every group; use as the anchor, not as a certainty (10 to 25% of bars omit it).
2. Backbeat: snare at steps 4 and 12 with probability 0.6 to 0.9 (funk .82/.72, hiphop .83/.70, dance .91/.83, fast .62/.57); velocity 0.85 to 1.0 at those steps.
3. When the backbeat snare plays, the kick is off the same step (funk, hiphop: 5 to 10% overlap). Kick on step 0 given snare on 4 or 12 is about 0.9, so the bar opens with a kick.
4. Kick syncopation lives on steps 2, 3, 7, 8, 10 (funk: .31 .45 .35 .34 .52); step 10 kick pairs with the second backbeat (P ~0.55 in funk/hiphop). Kick on odd 16ths other than 3, 7, 11 and 15 is under 15%; steps 1, 4 and 12 kicks are the rarest (4 to 9% in funk).
5. Ghost snares: 25 to 50% of all snare hits, 1.2 to 3.5 per bar, velocity 0.10 to 0.35 (use roughly 0.1 to 0.3 for ghosts, 0.85 to 1.0 for accents; velocity is bimodal, not uniform). Ghosts concentrate on the "e" and "a" 16ths: steps 1, 5, 7, 9, 13, 15 (funk p .43 .43 .67 .57 .40 .71).
6. Hats are the grid holder: on-beat and 8th steps (0, 2, 4, 6, 8, 10, 12, 14) at .3 to .6, odd 16ths at .05 to .28 (fast tempo: odd 16ths at .05 to .13, i.e. mostly 8th hats; ride shares this role). Accent hats on steps 0, 4, 8, 12 (velocity ~0.5 vs ~0.3 for off-steps).
7. Density: median gap between onsets is one step (60 to 75%) or two (20 to 30%); gaps over 4 steps are rare (<0.5%), so avoid holes longer than a beat unless it is a deliberate drop.
8. Tempo dependency: above 120 bpm the pattern thins (fewer odd-16th hat and snare hits, kick moves to 0, 8, 6, 14, 12), while below 105 bpm 16th ghosts and syncopated kicks are plentiful. Scale ghost and odd-16th probabilities by tempo (density about 15.9 onsets/bar at >=120 vs 17.5 to 20 in funk at about 100).
9. Microtiming: live feel = even steps early by roughly 0.1 step (10 to 18 ms at 100 bpm), odd steps on grid or up to +0.03 step late, per-onset random sd about 0.2 step (8 to 12 ms); ghosts are not systematically later. A quantised sample break shows sd around 0.05 step, so the engine's "human" amount can span 0.05 to 0.2 step sd.
10. Bar variation: keep bars 1 to 3 about 55 to 60% similar to the previous bar and the first bar after a phrase about 45%. Bar 4 carries 8 to 15% more onsets (fill pressure at the end of the 4-bar phrase). Variation is mostly ghosts and hats; kick/backbeat skeleton stays.
11. Fills: bar density ramps from about 12 onsets (fill first bar) to about 17.5 by bar 3; toms 10 to 20% per late step, snare 30 to 60% per step over steps 8 to 15; kick drops to 5 to 15% late in the bar.
12. Amen-style break (audio): 1-bar skeleton K 0, 2, 10 (11); S 4, 12 (+10); ghosts 7, 9, 15; bar 4 re-voices the backbeat, so a chopper should treat bar 4 as the variation slot and may move snares to step 1, 7, 9, 14.

## Limits

- Groove MIDI is human performance on an e-kit, not sampled break chopping; dnb/jungle styles are absent. The "fast" group is a tempo proxy, not a genre.
- Steps snap to 16ths; triplet and shuffle feels are mis-assigned.
- Bar origin is tick 0 of each file. The kick at step 0 (87% funk) supports this.
- Audio role detection is a heuristic; only the Amen grids are trustworthy. No claim is made about clean separation of kick and bass.
