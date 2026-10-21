# Break engine research: how breaks should sound, and what the engine must do

Branch: docs/break-engine-research. Scope: research only, no engine code changed.
Date: 2026-10-21.

## 0. How to read this document

Evidence grades used throughout:

- [S] stated in a fetched source (URL in section 9).
- [K] producer or drummer consensus from general knowledge (the web sources found were thin on exact grid data). Treat as a strong prior, verify against the real MIDI/audio of the break before hard-coding.
- [E] engineering judgement derived from the repo code.

Honest limits of the sourcing: no fetched page gave a full machine-readable transcription of any classic break. Exact step lists below are [K] and marked approximate. The structural rules (backbeat anchors, ghost behavior, bar 3 and 4 variation, fill at phrase end) are well supported. Step positions use a 16-step bar, steps numbered 1..16, beats on 1, 5, 9, 13. Velocities are MIDI 1..127; linear gain is vel/127 (squared taper optional).

Correction to the brief: BreakTweaker is an iZotope product (not Plasmonic), and Stutter Edit 2 is iZotope as well. Sources below treat them accordingly.

## 1. The engine today (what the research is measured against)

Facts read from the repo at origin/main e12625c:

- modules/pattern_engine/src/generate.cpp
  - Slot chop choice: baseIdx = slot * nChops / slots. The chop played at a step is the chop that sits at the same relative position in the source. So generation is "source order with variation", not role-driven. With variation.event, alternatives are drawn from the same group or any chop (uniform or grammar-weighted).
  - Density: probability of a slot = density * metricWeight / avgWeight. This is a generic metric-weight model with no per-role or per-bar-position knowledge.
  - Velocity: level is only 1.0 on beats and 0.85 elsewhere. No accents, no ghosts, no random range.
  - Swing: odd slots delayed by g * swing / 3 (swing 0..1). That maps to Linn swing 50% + swing/6, so swing 1.0 is 66.7% (triplet). See 4.2 for conversions.
  - Shift: uniform +-maxShiftTicks if dShift < variation.event.
  - Retrigger: if dRet < 0.15*ev, count 2..maxRetrigger, evenly spaced across the event.
  - Reverse 15% * ev, pitch 20% * ev, sub-hit at half grid 15% * ev (level 0.7), filter 30% * ev, glide 20% * ev, crush 20% * ev.
- modules/audio_renderer/src/renderer.cpp
  - kDefaultAttack 32 frames, kDefaultRelease 64 frames (about 0.73 ms and 1.45 ms at 44.1 kHz), kKillFade 64 frames, kRetriggerDecay 0.8 per sub-hit, release shortened to at most half the region.
  - Voice steal fades out (no hard cut). There is no kick/snare choke group, and attack length is not transient-aware.
- composition/src/chaos.cpp: chaos raises variation toward 1.0 (80%), density toward 0.6, and unlocks reverse (0.35), filter (0.40), pitch (0.50), glide (0.60), retrigger (0.65), crunch (0.75). Nothing in it protects backbeat anchors, so high chaos becomes random slice order.
- modules/chop_roles_grammar: built-in roles kick, snare, hat, cymbal, percussion, texture, accent, transition, other. Rules: preserveFirst, requireInFinalBeat, preferAtPhraseBoundary(weight 3.0). Rules can allow/block/weight a chop by role at a scope (event, beat, bar, phrase) but cannot yet express "role X at step N with probability p and velocity range".
- modules/evolve: only schedules steps (everyLoops, amount, ramp, ceiling). Mutation amount is global; no notion of "mutate fills first, keep anchors".
- composition/src/features.cpp holds the glue for Fractal, Evolve, Smart Setup (role suggestion) and export.

Diagnosis: the engine can produce rhythm-shaped output but has no model of what makes a break read as a break: fixed anchors, a velocity hierarchy, a ghost layer, phrase development, and fills. Everything below targets those gaps.

## 2. Anatomy of classic breaks

### 2.1 Universal principles [S+K]

1. Backbeat anchor: snare on beat 2 and 4 (steps 5 and 13) at full velocity in most bars. Loud backbeat against soft ghost notes is the defining contrast of funk breaks (Roland on Funky Drummer: forte on 2 and 4, ghosts soft and steady). [S]
2. Kick is syncopated around beats 1 and 3, usually on step 1 and then one or two of steps 3, 4, 7, 9, 10, 11. Kick almost never lands on 5 or 13 (it would collide with the snare). [K]
3. Ghost notes sit on weak 16ths (the "e" and "a" of the beat, steps 2, 4, 6, 8, 10, 12, 14, 16) between hat strokes, often right after the main snare. Never three or more ghosts in a row on consecutive 16ths. [S MusicRadar, Unison]
4. Hats or ride run straight 8ths or 16ths with accent on the 8th-note positions; for 16th hats the "e" and "a" are 15 to 30 percent quieter. Open hat usually on the "a" of 4 (step 16) or "e" of 4 (step 14) and is followed by a closed hit. [S Funky Drummer lesson, K]
5. Ghost velocity is 15 to 40 percent of the backbeat. Sources give 30 to 40 percent and 15 to 30 percent, start at 15 to 30 for subtle groove (Unison). One source uses vel 79 for a single anticipatory ghost on the last 16th of a 2-bar pattern (MusicRadar), which is a "skip into next phrase" accent, not a background ghost. [S]
6. Phrase development: bars 1 and 2 state the groove, bar 3 varies it, bar 4 turns around (omitted kick, displaced backbeat, extra weak-16th snare, crash). Amen: bar 3 delays the second backbeat to the second half of the bar; bar 4 omits the first kick, keeps the delayed backbeat, adds an unexpected snare on the weakest 16th and a crash on a weak 8th. [S Ethan Hein, MusicRadar]
7. Human feel: inter-onset spacing of 16ths in the Amen performance varies by roughly 120 to 165 BPM-equivalent (a commenter disputes the framing; read it as "variation in 16th spacing, not tempo drift"). Ride hits vary slightly in pitch and velocity; snare hits vary too. [S Ethan Hein]
8. Experts are more bothered by fixed (identical) snare displacement than by the flexible deviations of a real performance. [S Frontiers 2017] So humanization must vary per hit, not apply a constant offset.

### 2.2 Per-break grids

All grids: 16 steps per bar, `K` kick, `S` snare backbeat, `g` ghost snare, `h` closed hat or ride, `o` open hat, `C` crash, `.` empty. Confidence [K], approximate; ghost positions fluctuate between takes.

**Amen, Brother (The Winstons, about 136 to 138 BPM, 4 bars)** [S for structure, K for steps]

```
bar1/2: K . K . | S . . g | . g K K? | S . . g     (kick 1, 3, ~11; snare 5, 13; ghosts ~8, ~10, ~16)
bar3:   K . K . | S . . g | . g K . | . . S .      (second backbeat pushed late, to ~step 15)
bar4:   . . K . | S . . g | . g K . | . . S g C    (first kick dropped, late backbeat, extra weak 16th snare, crash on a weak 8th)
```

Ride: straight 8ths all four bars, full strength, slight pitch/velocity variation. Pedalled hat on beats, audible mostly in bar 4. Kick double hit just before beat 4 in bar 1 (first hit quieter). Tempo 136 to 138, Amen sits 1.47x below breakcore tempo 200. [S]

**Funky Drummer (Clyde Stubblefield, about 100 BPM, 2 bars)** [S Roland, funkydrummer.com; K for steps]

```
h (16ths, right hand): x x x x | x x x x | x x x x | x x x x   accents on 8ths (1,3,5,7,...)
K: 1, 4 (the "a" of 1), 7 or 8-ish, 10/11, with a kick together with the second hat opening on the "e" of 4
S: 5, 13 loud; ghosts g on 2?,4?,8,10,12?,14?,16 under the hats, soft and EVEN
```

Defining traits: one-handed 16th hats with first open hat alone, second open hat on the "e" of 4 together with the kick; ghosts steady and even, backbeat forte; restraint (Brown: "You don't have to do no soloing"). For chopping: long continuous hat bed, so slices of this break should keep hat continuity.

**Apache (Incredible Bongo Band, about 110 BPM)** [S structure, K steps]: tom/conga/bongo-driven, snare on 2 and 4 ghosted by congas; kick on 1 and off-beat; syncopated bongo fills; bongos cut through, so the "ghost layer" is percussion not snare. The audiolabs decomposition separated it into 6 components (snare combined with congas and bongos), which tells you the percussion layer is the part to retain for feel. [S]

**Think (Lyn Collins, about 100 BPM, 4 bars)** [K]: K on 1 and 3 with pickups on "&" of 2 or "a" of 3; S 5 and 13; tight closed hat 8ths; short fill with snare on steps 15, 16 at bar end; light ghost on step 8 or 12 only. Open and dry kit, little ring, so hard to hide edit points.

**Impeach the President (Honey Drippers, about 90 BPM)** [K]: very dry, laid-back, kick 1, 3(+), 10 or 11; S 5 and 13 with the snare slightly late (behind the beat); hats 8ths; almost no ghosts. Good for low-density, heavy backbeat generation; late snare (10 to 20 ms) is part of the feel (Unison also suggests nudging the second kick 10 to 20 ms late [S]).

**Hot Pants (James Brown, about 100 BPM)** [K]: kick on 1, "&" of 2, 3; snare 5 and 13; busy 16th hat; ghost at 8 and 16.

**Soul Pride (James Brown, about 100 BPM)** [K]: funk break similar to Funky Drummer family, strong 16th hats, ghosts on 7/8, 15/16.

**Scorpio (Grandmaster Flash source, Dennis Coffey, about 105 BPM)** [K]: kick 1, 3-ish with pickup 4; S 5 and 13; cymbal-heavy; bar 4 snare roll style fill.

**Periodic Table** [K, low confidence]: no source found; treat as a DnB-style re-sequenced break with the same anchors and shifted kicks.

Common table (aggregate over the above): P(kick on step 1) about 0.95; P(snare on 5) about 0.97; P(snare on 13) about 0.95 in bars 1 to 2 and about 0.6 in bars 3 to 4 (displacement variants).

### 2.3 Bar-end fills [K+S]

- Fill lives in the last 4 steps (13..16) or last beat of bar 4 (and optionally bar 2 of a 4-bar phrase, weaker). Content: snare 16ths with velocity ramp, ghost-then-flam, tom run, or crash plus silence.
- Phrase turn convention in jungle: turnaround on the last bar of an 8-bar loop; remove drums, swap in another break, filter, reverse crash, or upsweep; sections are 8 bars then 16-bar blocks (EDMProd). [S]
- Snare roll: 16th or 32nd notes, velocity ramp 50 to 100 percent, optional 1 to 2 semitone pitch rise, high-pass sweep 200 to 800 Hz, short reverb about 1.2 s (Unison). [S]

## 3. Re-editing breaks (jungle, DnB, breakcore, IDM, glitch)

### 3.1 Jungle and DnB [S+K]

- Tempo 170 to 180, classic 174. Break sped up with detune about +20 cents sometimes used to sit in time with a layered second break. [S Loopmasters, EDMProd]
- Keep kick and snare on the grid; off-grid hits elsewhere ("funkiness") are good. Classic breaks were not recorded to a click, so snares drift later across a loop; tighten by moving the snare start to land on beat 2. [S]
- Chop by transient, auto-slice, trigger by MIDI; hits then re-sequenced. Edit so that a kick or snare lands on most bass notes (no exact ratio). [S EDMProd]
- Snare on 2 and 4 ("2 and 4 rule") is the default; a "rolling" variant puts ghosts right after the main snare on steps 6 and 14 and pushes density up with 16th hats. Kicks on 1 and 3, the second one 10 to 20 ms late; extra off-beat kick every second bar; drop the first kick every second bar. [S Unison]
- Velocity guide (Unison): snare about 90 percent first hit, 100 percent second; ghosts 15 to 30 (or 30 to 40) percent; closed hats 70 to 80 percent with dips; hat variation random between 70 and 90 over 4 or 8 bars; humanize ghosts and hats within 60 to 100 percent. [S]
- Layering: high-pass the second break about 160 Hz so its kick does not fight the first break's kick. [S Loopmasters]
- Transient processing to remove sustain from breaks gives tight, edit-friendly slices. [S EDMProd]

### 3.2 Breakcore and glitch [S+K]

- Tempo 160 to 220 (mostly at or above 200); edit density matters more than tempo. Slices shorter than one hit, working at 1/32 and 1/64 notes (37.5 ms per 32nd at 200 BPM, 75 ms per 16th). [S orphiq]
- Slice on transients, so slice lengths are uneven. Fade each slice edge 1 to 3 ms or snap edits to zero crossings; ringing ride causes clicks otherwise. [S orphiq]
- Label slices by role (kick, snare, ghost snare, open hat, crash, displaced kick) and sequence from the labelled hits instead of looping one pattern. [S orphiq]
- Retrigger one slice across a beat with descending pitch; roll a two-hit figure into a bar change; reverse a snare into a downbeat. [S orphiq]
- Chopping shortens kick decay, so layer a sine sub or clean kick and duck it under loud snares. [S orphiq]
- Distort after chopping, so edit boundaries become part of the character. [S orphiq]
- Repitching raises the break (seven semitones thins the kick, makes snare brittle); time-stretch adds grain; producers often layer a repitched and a stretched copy. [S orphiq]
- Structure: breakcore rewrites the pattern continuously and ignores 16/32 bar phrasing; jungle loops one chopped 2 or 4 bar break per section. [S orphiq] That means the engine needs a "phrase strictness" parameter (jungle high, breakcore low), not one mode.
- Avoid a "fake repeated sample" sound in rolls: alternate hits and apply slight volume, offset and pitch changes, keep pitch variation under about half a semitone (hseiken, Renoise thread). Using two different snares or velocity changes helped but was not enough alone. [S Renoise]
- Snare roll programming: low ticks-per-beat so each hit is editable; plain retrigger sounds wrong. [S Renoise]

### 3.3 Why re-edited breaks sound wrong

| Symptom | Cause | Engine hook |
|---|---|---|
| Sounds random, no groove | Slices in random or source-position order (generate.cpp baseIdx and variation.event drawing from any chop) | Draw chops by role per step (section 5) |
| No pulse | Backbeat anchors lost; chaos replaces snare on 5 or 13 | Anchor protection (P0) |
| Muddy low end, flams | Kick and snare on same step, or long kick tail under snare | Choke group, collision rule |
| Flat, robotic | Equal velocities (1.0/0.85 only) | Velocity model (P0) |
| Cluttered | Ghost density too high, three consecutive ghosts | Ghost cap |
| No sense of bar | No phrase structure, no fill at bar 4 | Phrase template (P0) |
| Clicks, zipper | Slice boundary not faded, edit at non-zero crossing | Fade rules (section 7) |
| Level jumps | Slices from different parts of the source with different loudness (ride swell, decay) | Per-chop gain normalization |
| Transients softened | Attack fade too long (kDefaultAttack 32 frames is ok; longer eats the kick) | Transient-aware attack |
| Pitch/time artifacts | Pitch range too large for drums | Cap pitch to +-0.5 st on rolls, +-12 for effects |
| Rolls sound like machine gun | Fixed gain and pitch per retrigger | Per-repeat randomization |
| Unnatural silence | Repeated hits retriggered with the previous voice tail clipped | Fade-out, not hard cut (already done) |

## 4. Numeric timing and dynamics rules

### 4.1 Velocity hierarchy [S+K]

| Role | Default velocity | Range | Notes |
|---|---|---|---|
| Backbeat snare (5, 13) | 110 | 100..127 | second backbeat 5 to 8 higher than the first (Unison 90 then 100 percent) |
| Kick on 1 | 118 | 105..127 | |
| Other kicks | 100 | 80..115 | pickup kick before 1 or 9 quieter (-10 to -20) |
| Ghost snare | 30 | 19..50 | 15 to 40 percent of backbeat; dip some by -5 |
| Anticipatory ghost (step 16) | 62 | 55..80 | MusicRadar 79 example |
| Closed hat on beat (1,5,9,13) | 100 | 90..110 | |
| Closed hat on 8th (3,7,11,15) | 88 | 76..100 | |
| Closed hat on e/a (even steps) | 68 | 50..85 | 15 to 30 percent quieter |
| Open hat | 95 | 80..110 | followed within 1 step by closed |
| Ride (8ths) | 95 | 85..105 | slight random +-4 |
| Crash | 105 | 90..127 | bar 4 step 8 to 16 |
| Fill snares | ramp 50 to 100 percent | | monotone rising with +-5 noise |

Single-hit variation: add gaussian jitter sigma = 4 velocity units to everything (not 0); clamp to range.

### 4.2 Swing and microtiming [S+K+E]

- Linn convention (MPC, Logic, Reason): 50 percent = straight; Logic 16A..16F = 50, 54, 58, 62, 66, 71 percent; FL/Cubase: 0 percent = straight, 100 percent = triplet (about 66.7 in Linn). [S Attack Magazine]
- Engine mapping: Linn% = 50 + 16.67 * swing. So swing 0.24 = 54, 0.48 = 58, 0.72 = 62, 1.0 = 66.7. [E]
- Recommended: jungle and DnB 16ths 0 to 0.24 (50 to 54), with ghosts/hats swung but kick and snare anchors NOT swung; hip-hop breaks 0.3 to 0.72 (55 to 62); breakcore/IDM 0 to 0.1 for rolls (swing makes 1/32 rolls sloppy).
- Per-role timing offsets (ms, positive = late), [K, flagged as starting values]:
  - Backbeat snare: 0 to +8 (laid-back funk up to +15; Impeach about +10 to +20), sigma 1.5.
  - Kick on 1: -3 to +3, sigma 1.
  - Second kick (Unison): +10 to +20 on the one near beat 3.
  - Ghost: +0 to +12 behind the grid, sigma 3.
  - Hat: sigma 3 (up to 5 for 16ths in funk), fixed offset 0.
  - Rolls: no offset beyond the swing; spacing exact.
- Humanization that stays musical: total shift under 15 ms for anything except intentional laid-back offsets, snare displacement varied per hit rather than fixed (Frontiers: fixed displacement bothered experts more than natural variation) [S]. Current maxShiftTicks uniform shift is fine in principle but is role-blind and applies to anchors too; restrict anchors to sigma 1 to 2 ms.
- Ticks to ms for conversions: ms = tick * 60000 / (bpm * ppq).

### 4.3 Retrigger, roll and stutter conventions [S+K]

| Gesture | Rate | Count | Velocity | Pitch | Gate | Use |
|---|---|---|---|---|---|---|
| Snare roll fill | 1/16 | 4 to 8 | ramp 50 to 100 | 0 to +2 st over roll | 100% | bar 4 end, last 1 to 2 beats |
| Snare roll tight | 1/32 | 8 to 16 | ramp 40 to 100 | +0..+2 st | 100% | last beat of phrase |
| Stutter 1/8 | 1/8 | 2 to 3 | -3 dB per repeat or flat | 0 | 70 to 100% | accent on a beat |
| Stutter 1/16 | 1/16 | 3 to 4 | -1.5 dB per repeat | 0 | 80% | mid-bar glitch |
| Stutter 1/32 | 1/32 | 4 to 8 | flat or ramped | descending 0 to -5 st (breakcore) | 60 to 80% | end of bar |
| Speed ramp (stepping) | 1/8 to 1/16 to 1/32 to 1/64 | 2 each step | rising | none | 70% | build, 1 bar |
| Mini stutter at 1/64 or shorter | 1/64..1/128 | 8+ | | | | reads as tone; use only as pitched effect (Wikipedia: past about 1/128 repeats sound like tones) |

Stutter-edit conventions from the literature: common value 16th, but 32nd, 64th, 128th occur; ramps usually step down through note values starting at 1/32 then 1/64 then 1/128 and can use logarithmic curves not locked to the grid; stutters are placed in an 1/8 or 1/16 span at bar start or toward the bar end for syncopation; they are applied to percussive material whose attack is no longer than an 1/8. [S Wikipedia]

Retrigger decay: the renderer's constant 0.8 per sub-hit (-1.9 dB) is reasonable for stutters (-1 to -3 dB per repeat is the working range) but wrong for roll fills (which should rise). Make decay signed: negative = decay, positive = crescendo. [E]

Probabilities per bar position for glitch gestures (see table 6.3).

### 4.4 Reversal [S+K]

- Reverse a snare or crash into a downbeat: reversed region must END on step 1 (or step 5/9/13), i.e. start at beat minus region length. Currently reverse is just a flag on any event; it lands the reversed transient wherever the event starts, which produces a swell that peaks off the grid.
- Probability: 3 to 8 percent per bar in jungle, 15 to 25 percent per bar in breakcore/IDM, only on snare/cymbal/ghost roles, never on the backbeat-anchor snare of bars 1 to 2.

### 4.5 Pitch and time tricks [S+K]

- Pitching a break up 7 semitones thins the kick and brittles the snare (orphiq); keep per-slice pitch within +-2 st for groove, +-0.5 st for natural variation in rolls, larger only as an explicit effect.
- Descending pitch on a retriggered slice across a beat (-1 to -7 st) is the breakcore signature; a rising +1 to +2 st on snare roll is the DnB signature.
- Layering one repitched and one stretched copy: out of scope for the pattern engine; relevant for time_stretch module.

## 5. Role-driven generation: the model the engine needs

Instead of baseIdx = slot * n / slots, drive each step by role:

1. Pre-pass: label chops with roles (Smart Setup already suggests roles; `acceptRoleSuggestions`). Require at minimum kick, snare, hat; ghost and fill are subsets of snare chops chosen by RMS (ghost = low RMS snare-like chop).
2. For each step i in 1..16 and each role r, sample hit with probability P[r][i] (table 6.1) modified by bar-in-phrase multipliers (6.2).
3. Resolve collisions (6.4): if kick and snare both fire on one step, keep snare on 5 and 13, keep kick elsewhere.
4. Choose the chop within the role (not across roles): round-robin or weighted so consecutive hits of one role differ (avoid same chop twice in a row, except in a stutter).
5. Assign velocity from the hierarchy (4.1) and timing from 4.2.
6. Phrase template (6.2) controls bar-level variation; `variation.*` then perturbs only non-anchor events.

## 6. Machine-usable rule tables

All probabilities are per bar, per step, for a "standard break (funk/jungle)" profile. Steps 1..16. Profiles override with multipliers (6.5).

### 6.1 Per-step, per-role probabilities and velocities

Columns: step, then `p(role)` and velocity (mean, lo, hi). Dash = never.

```
step | beat pos | kick p  vel(mu,lo,hi) | snare p vel          | ghost p vel         | hat p vel           | open hat p | notes
 1   | 1        | 0.95 118,105,127      | -                    | -                   | 0.90 100,90,110     | 0.00 | downbeat kick anchor
 2   | e        | 0.04  85,70,100       | 0.00                 | 0.08 28,20,45       | 0.80  68,50,85      | 0.00 |
 3   | &        | 0.45 102, 85,115      | 0.00                 | 0.06 28,20,45       | 0.92  88,76,100     | 0.02 |
 4   | a        | 0.28  95, 80,110      | 0.00                 | 0.14 30,20,48       | 0.78  68,50,85      | 0.00 |
 5   | 2        | 0.00 -                | 0.97 110,100,127     | -                   | 0.90 100,90,110     | 0.00 | backbeat 1
 6   | e        | 0.05  80,65, 95       | 0.02 70,60,85        | 0.45 30,20,48       | 0.75  66,50,85      | 0.00 | ghost after snare
 7   | &        | 0.18  92,75,108       | 0.00                 | 0.12 28,20,45       | 0.92  88,76,100     | 0.03 |
 8   | a        | 0.10  85,70,100       | 0.00                 | 0.38 30,20,48       | 0.78  68,50,85      | 0.02 | ghost pickup
 9   | 3        | 0.72 112,100,125      | 0.00                 | -                   | 0.90 100,90,110     | 0.00 | second kick anchor
10   | e        | 0.38  98, 80,112      | 0.00                 | 0.40 30,20,48       | 0.75  66,50,85      | 0.00 | classic ghost spot
11   | &        | 0.42 100, 85,115      | 0.00                 | 0.08 28,20,45       | 0.92  88,76,100     | 0.04 |
12   | a        | 0.12  88, 70,105      | 0.00                 | 0.14 30,20,48       | 0.78  68,50,85      | 0.00 |
13   | 4        | 0.00 -                | 0.95 112,100,127     | -                   | 0.90 100,90,110     | 0.00 | backbeat 2 (about 5 to 8 louder than step 5)
14   | e        | 0.04  80,65, 95       | 0.03 72,60,85        | 0.42 30,20,48       | 0.75  66,50,85      | 0.03 | ghost after snare
15   | &        | 0.12  90,75,105       | 0.04 80,65,95        | 0.12 28,20,45       | 0.92  88,76,100     | 0.04 |
16   | a        | 0.10  88,70,105       | 0.05 78,60,95        | 0.35 36,22,60       | 0.78  68,50,85      | 0.10 | anticipation (ghost may reach 62..79 when it leads next bar)
```

Notes: a row's roles may fire simultaneously except the collisions in 6.4. Hat is represented as an independent layer (continuous 8th/16th grid), so p is the "present" probability inside the gesture; for hat-less chop sets (single drum slices) skip the hat column and fold into "percussion".

### 6.2 Phrase (4-bar) modifiers

Multiplicative on 6.1, then clamp to [0,1]. Bar index b in 1..4 within the 4-bar phrase (for 2-bar phrases use bars 1,2 as b=1,4 patterns).

```
bar | kick on step1 | snare 13 | snare 15 (late backbeat) | ghosts | variation of other kicks | fill (6.3) | crash step
 1  | x1.00         | x1.00    | x0.0 (use 6.1)          | x1.0   | x0.6                     | 0.00       | 0.03 on step1
 2  | x1.00         | x1.00    | x0.0                    | x1.0   | x0.8                     | 0.20 (last beat) | 0.00
 3  | x1.00         | x0.60    | +0.40 (push 13 to 15)   | x1.1   | x1.2                     | 0.10       | 0.00
 4  | x0.55 (kick omitted) | x0.50 | +0.45            | x1.3   | x1.4                     | 0.75       | 0.35 on step 8..9, 0.20 on step 1 of next bar
```

Rule: when "snare 13" is moved to 15 (displaced backbeat), delete step 13 snare in that bar (do not keep both).

### 6.3 Fills and glitch gestures

Per phrase, last bar unless noted. `p_ph` = probability of occurring per phrase end (4 bars), `p_bar` = per non-final bar.

```
gesture             | span (steps) | p_ph (bar 4) | p_bar (bars 1-3) | vel shape               | extras
snare roll 1/16     | 13..16       | 0.45         | 0.04             | ramp 50->100 pct        | pitch +0..+2 st, hp sweep optional
snare roll 1/32     | 15..16       | 0.20         | 0.02             | ramp 40->100            | gate 100 pct
double snare 15,16  | 15,16        | 0.30         | 0.05             | 85, 100                 |
tom run / perc fill | 13..16       | 0.15         | 0.00             | descending 110->90      | only if percussion role chops exist
stutter 1/8 on kick or snare | 1 beat | 0.10      | 0.06             | -3 dB/rep               | breakcore x3
stutter 1/16       | 2..4 steps   | 0.15         | 0.08             | -1.5 dB/rep             |
stutter 1/32       | 1..2 steps   | 0.10         | 0.05             | flat                    | pitch 0..-5 st
speed ramp 1/8->1/16->1/32->1/64 | 1 beat  | 0.08 | 0.02            | rising                  | breakcore x3
reverse snare/crash into step 1 | 4..8 steps before bar line | 0.12 | 0.02 | rising by reversal | must end on bar line
silence/gate drop (drums out) | last 2..4 steps | 0.08 | 0.00        | -                       | turnaround
```

Per-profile multiplier on `p_bar`: jungle/DnB x1, hip-hop x0.3, breakcore x3, IDM/glitch x3 with random structure allowed.

### 6.4 Collision and density rules

1. Kick and snare on the same step: keep snare for steps 5 and 13; elsewhere keep kick; allow both only when `kickSnareLayer` flag (flam-free layered hit) is on. Metric: collisions per bar = 0 except for layered mode.
2. Kick adjacent to snare: no kick on step 4 or 6 when kick already on 3 and snare on 5 in the same beat unless ghost is absent (limit 2 low-frequency hits within 3 steps).
3. Ghost cap: at most 2 ghost snares per beat, never 3 consecutive 16th steps ghost/snare, at most 6 ghosts per bar.
4. Minimum events per bar: kick >= 1, snare >= 1 (anchor). Maximum events per bar for non-glitch: 20 (16th hat + hits excluding hats: <= 12).
5. Open hat must be followed by a closed hat or silence within 2 steps; max 1 open hat per bar except in glitch profiles.
6. Cymbal crash only on step 1 or at phrase end; at most one per 4 bars in jungle.

### 6.5 Profiles (multipliers on 6.1/6.3, swing, timing)

```
profile       | density (events/bar excl. hats) | swing (engine 0..1) | ghost x | fill x | glitch x | pitch range st | anchor protect
funk break    | 6..10                           | 0.30..0.60          | 1.0     | 0.8    | 0.3      | 0              | strict (p>=0.95)
hip-hop       | 5..8                            | 0.45..0.70          | 0.8     | 0.5    | 0.2      | 0              | strict
jungle/DnB    | 8..14                           | 0.00..0.24          | 1.3     | 1.0    | 1.0      | +-1            | strict (bars 1-3), soft bar 4
rolling DnB   | 10..14                          | 0.00..0.20          | 1.6     | 1.0    | 0.8      | +-1            | strict
breakcore     | 12..24                          | 0.00..0.10          | 1.0     | 1.5    | 3.0      | +-7 effects    | soft (p>=0.7), 1 anchor per bar min
IDM/glitch    | 6..20                           | 0.00..0.60 free     | 0.7     | 1.2    | 3.0      | +-12 effects   | soft (p>=0.5)
```

## 7. Quality engineering

### 7.1 Slice-boundary fades [S+K+E]

- Breakcore edit practice: fade each slice edge 1 to 3 ms or snap to zero crossings (orphiq). [S]
- Sampler forum evidence: automatic zero-crossing snap does not reliably remove clicks; the click often exists in the sample itself, a short fade-in fixes it; too long a fade makes a dip (attack 10 on Octatrack was "ok", 15 "too much" for one user; a 1 to 5 ms equivalent was not always enough on sliced loops); fades that are too long soften the first kick transient. [S Elektronauts]
- Repo state: attack 32 frames (0.73 ms), release 64 frames (1.45 ms) at 44.1 kHz. That is at the low end. Recommended defaults (frames at source rate, sr/1000 * ms):
  - Attack: kick/snare/tom 0.5 to 1 ms (do not exceed 1.5 ms, protects the transient), hats/cymbals 1 to 2 ms, texture 3 to 5 ms.
  - Release: 3 ms default for hits; 5 ms for cymbal/ride; 1.5 ms inside stutters shorter than 1/32; always <= region/2 (already so).
  - Raised-cosine or equal-power-ish shape, not linear (linear fades create a small spectral splatter of their own; difference small but cheap to fix).
- Transient-aware start: when the chop start sits within 1 ms before a detected onset, move the start to the pre-onset zero crossing within a search window of 0.5 to 2 ms before the onset, never after (moving after the onset eats the attack). When no zero crossing in the window, rely on the fade. Do this in source_chop at marker placement or in renderer at event start; cheaper in source_chop.
- For back-to-back slices (contiguous regions from the same source), where a splice joins non-adjacent audio, apply 1 to 3 ms crossfade overlap (end fade of A under start fade of B) rather than fade to silence then up, to prevent a gap dip. Overlap is only needed for sustained material (ride, pads), not for drum hits that have decayed.
- Reverse slices: after reversal the transient becomes the end; apply the long fade (3 to 5 ms) at the start and a very short (0.3 ms) at the end, or the swell cuts abruptly.

### 7.2 Voice choke and tails [K+E]

- Choke group kick: a new kick-role voice fades out the previous kick-role voice over 2 to 4 ms (prevents 808-style stacking and phase cancel). Snare: a new backbeat snare fades the previous snare tail over 4 to 8 ms unless in a roll (rolls overlap intentionally with the existing 0.8 decay). Hat: closed hat chokes open hat within 1 to 2 ms (existing physical behavior).
- Kick under snare: if a kick tail is still active when the snare fires on 5 or 13, duck the kick by -6 dB over 5 ms (sidechain-style) rather than choking, as breakcore advice also layers kick under snares and ducks it under loud snares. [S orphiq]
- Voice stealing already fades over kKillFade 64 frames (1.45 ms); raise to 3 ms for non-transient content.

### 7.3 Level matching [K+E]

- Compute per-chop integrated loudness (RMS over first 50 ms after onset); normalize role-wise so that all snares are within +-1.5 dB, kicks within +-2 dB, ghosts at (backbeat RMS - 12 to - 20 dB) before the velocity model is applied. Apply as an invisible per-chop trim, not touching user gain. Without it, velocity values are meaningless because slices from different source times (e.g. a roll tail) have very different baselines.
- Limit total output peak: a soft ceiling or at minimum -1 dBFS true peak because summing overlapping stutters with 0.8 decay and layered kicks easily exceeds 0 dBFS.
- Retrigger decays: dynamic (rolling) 0.8 factor default is fine for stutters; rolls need `crescendo` factors 1.1 to 1.25 per hit with cap at 1.0.

### 7.4 Humanization budget [S+K]

- Role-aware gaussian: see 4.2. Timing jitter total (peak) < 12 ms for hits, hats < 6 ms, ghost < 8 ms; velocity sigma 4 for backbeat, 6 for ghosts and hats.
- Never apply both a fixed laid-back offset and jitter that together exceed 20 ms on the backbeat unless profile is "laid-back hip-hop".
- Do not humanize timing in 1/32 or finer rolls (grid error becomes rhythm error). Vary only velocity (sigma 3), pitch (+-0.3 st) as per Renoise advice. [S]

## 8. Stutter Edit and BreakTweaker design (iZotope) [S]

### 8.1 Stutter Edit 2

- Rate: two-point range (e.g. 1/8 bottom to 1/128 top), slides smoothly without Quantize; with Quantize on it locks to discrete values held for Step length. Quantize modes: Free, Closest, Walk (up one per step), Skip (up two, down one), Stagger (up one, up one, down one), Random.
- Gate: Width (1.0 equals stutter length; lower cuts faster) and Tail (release; low is choppy, high extends). This is the "gate" in rolls: gate shape decides if a stutter sounds like a roll or a buzz.
- Buffer Position: L and R nodes; forward if left lower than right, backward otherwise; nodes snap to grid; Buffer Reverse instant. Movement modes: Slider, Grid (locks to last grid point), Random within range. Jitter: pitch (-24..+24 st) or rhythm offset, master/L/R switches.
- Pan: alternates repeats left and right, with Pan Width and Center.
- Gesture length from 1/16 triplet to 2 bars; Grid sets start and end value (1/8, 1/16). Gestures are triggered by MIDI notes; Release modes On Grid, Full Gesture, Latch, Instant, Stick. Freeze repeats a slice of buffer set by Duration; Palindrome reverses or restarts. Only one stutter gesture plays at a time. Effects: 11 processors, all mappable to time-variant curves (TVMs), including low-pass, high-pass, delay band-pass, stereo delay, bit reduction, lo-fi, jump pan.
- What the reviewer found musical: the signature banks, used on master for fills, edits, transitions and breakdowns, best for repetitive electronic styles. Gesture beat-division range 1/2 to 1/1024 with dotted and triplet rows. A key press starts on the next beat or bar division.
- Take-aways for ChopFractal: (a) a stutter event should have a rate *curve* (start rate, end rate, quantized step list) not only a count; (b) gate width and tail per event are separate controls; (c) pan alternation on repeats makes rolls sound wide and non-robotic; (d) gestures start and end on grid positions; (e) one stutter gesture at a time per bus; (f) a small set of named presets (macros) is how users reach good settings.

### 8.2 BreakTweaker (iZotope)

- 32-step sequencer, six tracks, 24 pattern slots per patch; tracks have independent lengths (polyrhythm) and can run at 1/3 to 3x host speed.
- MicroEdit per step: split the sound into many slices; "divisions" stutter playback over time; pitch, time and speed modifiable the same way; slope, tension, rotation, slice-length gating adjustable. Four envelopes and LFOs assignable to pan, gain, waveform shape, effects. Two distortion slots and one filter per generator.
- Take-aways: per-step micro-edit (a step carries its own division/ramp rather than a global stutter flag); per-step envelopes (slope, tension) rather than only linear; independent track lengths to create polymetric breakcore (bar-level loop length per role, e.g. hat 16, kick 12 steps).
- Probability and macros were not described in the reviews fetched; macro design for ChopFractal is therefore [E] (section 10).

### 8.3 Which presets sound musical (synthesized from sources, K)

- Rate ramps that step down in note value across 1 beat to 1 bar and stop on grid.
- Gate width 0.5 to 0.8 with short tail for rolls; width 1.0 with long tail for washy stutter.
- Pitch jitter under +-1 st or stepped to musical intervals; random pitch jitter of +-24 st sounds like noise.
- Filter sweep and bit-crush ramps timed to the same gesture length.
- Bank of gestures starting on bar or beat boundaries only.

## 9. Sources

Fetched or searched (grade S unless noted):

- Ethan Hein, Amen break listening guide: https://www.ethanhein.com/wp/2025/amen-break-listening-guide/
- MusicRadar, How to program an Amen-style break: https://musicradar.com/tuition/tech/how-to-program-an-amen-style-break-637374
- MusicRadar, ghost notes: https://www.musicradar.com/tuition/tech/how-to-add-groove-and-pace-to-a-beat-using-ghost-notes-625526
- Roland, Behind the Beat: Funky Drummer: https://articles.roland.com/behind-the-beat-funky-drummer-by-james-brown/
- Funky Drummer School of Funk lesson 10: https://www.funkydrummer.com/school-of-funk/lesson-10
- Loopmasters, How to make jungle breaks: https://www.loopmasters.com/articles/4377-How-To-Make-Jungle-Breaks-A-Drum-Bass-Jungle-Break-Beat-Tutorial
- EDMProd, How to make jungle music: https://www.edmprod.com/how-to-make-jungle-music/
- Unison, DnB drum patterns: https://unison.audio/dnb-drum-patterns/
- Orphiq, What is breakcore: https://orphiq.com/resources/what-is-breakcore
- Renoise forum, breakcore tutorial: https://forum.renoise.com/t/tutorial-breakcore-mod-med-rns-it-xm/18985
- Attack Magazine, Swing in DAWs: https://attackmagazine.com/technique/passing-notes/daw-drum-machine-swing/2
- Elektronauts, declick tricks: https://www.elektronauts.com/t/declick/10476
- Frontiers in Psychology 2017, microtiming and groove (abstract only): https://www.frontiersin.org/articles/10.3389/fpsyg.2017.01709/text
- iZotope Stutter Edit 2 Stutter and Buffer docs: https://s3.amazonaws.com/izotopedownloads/docs/stutteredit2/en/stutter-and-buffer/index.html
- iZotope, Learn Stutter Edit 2 in 10 minutes: https://www.izotope.com/en/learn/learn-stutter-edit-in-10-minutes
- Sound On Sound, Stutter Edit review: https://www.soundonsound.com/reviews/izotope-stutter-edit
- Wikipedia, Stutter edit: https://en.wikipedia.org/wiki/Stutter_edit
- MusicTech, iZotope BreakTweaker review: https://musictech.com/reviews/izotope-break-tweaker-review/
- AudioLabs Erlangen, Apache decomposition: https://audiolabs-erlangen.com/resources/MIR/2016-IEEE-TASLP-DrumSeparation/BongoBand
- Melodics, Amen Brother lesson (kicks on 1-& and 1-a in bars 1 to 4): https://melodics.com/learn-to-play/amen-brother-by-the-winstons-on-drums

Searched but not readable for detail (a MIDI or notation image was needed): iriss Amen pattern page (MIDI download in author's book Live Drum & Bass), Soul-Sides Apache page, Stereogum Breaks With Tradition. Recommendation: obtain MIDI transcriptions of the 6 target breaks and replace section 2.2 [K] grids with measured onset tables before shipping the profile data.

## 10. Prioritized engine changes

### P0 (largest audible gain, small code)

1. Velocity model. Replace `level = beat ? 1.0 : 0.85` in generate.cpp with the role/step table 6.1 (mean +- gaussian, clamp lo..hi), converted to gain (vel/127)^1.0. Needs role per chop: when a chop has no role, fall back to metric weight: beat 1.0, 8th 0.85, 16th 0.65 (still much better than 0.85 flat).
2. Backbeat anchor protection. In generate.cpp and in evolve/chaos paths: events on steps 5 and 13 with role snare (and step 1 kick) are locked from variation, reversal, pitch, retrigger, shift > sigma 2 ms and replacement by non-snare chops, except where the phrase template (6.2) displaces them. `applyChaos` must not unlock these (breakcore profile uses soft anchors with p >= 0.7).
3. Role-driven chop selection. Replace slot-proportional baseIdx with role selection per step using 6.1; within role choose round-robin or weighted; `variation.event` swaps within the same role first.
4. Phrase template. Apply 6.2 across bars (kick dropped bar 4, displaced backbeat bar 3 and 4, ghosts growing, fill at bar 4). Add `Settings.phraseStrictness` 0..1 to blend profile templates (jungle high, breakcore low).
5. Renderer fades: attack 32 to 48 frames at 44.1 k by role (0.5 to 1.2 ms for kick/snare, 1 to 2 ms others), release 3 ms (132 frames at 44.1k), raised-cosine shape. Replace fixed frame constants with ms scaled by source sample rate.
6. Collision rule 6.4 (kick vs snare same step, ghost cap, open hat follow).

### P1

7. Grammar defaults: ship a built-in rule set: preserveFirst(kick), requireInFinalBeat(snare or fill), preferAtPhraseBoundary(transition/cymbal, weight 3.0 already), plus new rules: `anchorAtStep(role, step, p)`, `maxConsecutive(role, n)`, `forbidTogether(roleA, roleB, step)`, `velocityRange(role, lo, hi)`, `timingOffset(role, mu, sigma ms)`. Extend serialization (state version bump per hot-file rules; golden hashes unchanged only when default profile is off, so profile data must be opt-in or hashed separately).
8. Fills: 6.3 gestures generated as events with retrigger and velocity ramps. Extend retrigger decay to signed (crescendo for rolls, decay for stutters), per-repeat pitch and velocity random (0.3 st, sigma 3 vel).
9. Reverse rule: reversed hits must end at the next grid line; restrict reverse to snare/cymbal/ghost roles.
10. Per-chop loudness trim for role-level matching; ceiling at -1 dBFS.
11. Choke groups: kick 2 to 4 ms fade, snare 4 to 8 ms, closed hat chokes open.
12. Transient-aware marker start (zero crossing search window 0.5 to 2 ms before onset) in source_chop.
13. Swing apply only to ghost and hat roles; anchors unswung. Maintain swing 0..1 mapping (Linn% = 50 + 16.67*swing).

### P2

14. Fractal Rhythm: when subdividing, never subdivide an anchor beat cell into rolls except in bar 4; ensure children inherit role and keep the child with the transient first.
15. Evolve: mutate fills and ghosts first (amount weights per role: ghost 1.0, hats 0.8, non-anchor kick 0.5, fills 1.5, anchors 0.1) and increase amount toward phrase end. Evolve ramp should stop at ceiling before touching anchors.
16. Chaos macro: instead of unlocking transforms at fixed thresholds only, scale per-gesture probabilities (6.3 x chaos), and tie `maxShiftTicks` to <= 6 ms for anchors even at chaos 1.
17. Effects: stutter gesture object with rate curve (start rate, end rate, step mode Walk/Skip/Stagger/Random), gate width 0.5..1, tail 0..1, pan alternation width, and pitch ramp; two or three named macros (section 11).
18. Glitch profile polymeter: role loop lengths independent (kick 12, hat 16).
19. Pitch: cap at +-2 st groove and +-0.5 st in rolls; larger only on effect events.

## 11. Macro design suggestions [E, with S motivation]

- Macro "Break Feel" 0..1: swing (0 to 0.3 in engine units), ghost level (+0 to +10 vel), laid-back snare offset (0 to +10 ms).
- Macro "Edit Density": events per bar excluding hats (6 to 20), fill probability x (0.5 to 3), glitch x (0.3 to 3).
- Macro "Glitch": enables 6.3 gesture classes in the order stutter 1/8, stutter 1/16, reverse, roll 1/32, speed ramp, pitch ramp, bit crush (mirrors chaos thresholds but per gesture not per global flag).
- Macro "Phrase Strictness": 1 = jungle/hip-hop template, 0 = rewrite each bar.
- Gate macro: width 1.0..0.4 and tail 0.3..0.05 for stutter events (more cut = more machine-like).

## 12. Automatically testable acceptance metrics

Run these on N = 200 seeded generations per profile (fixed seeds so tests are deterministic) with a synthetic source of labelled chops (kick, snare, ghost, hat, crash with known onsets) so role is known without ML. Thresholds apply to the funk/jungle profile; breakcore thresholds in parentheses.

Structure and pattern metrics (on the Pattern model, no audio):

1. Backbeat presence: fraction of bars with a snare-role event on step 5 AND 13 >= 0.70 (0.45); snare on step 5 alone >= 0.95 (0.7); bars with no snare at all <= 2 percent (10).
2. Kick anchor: kick on step 1 in >= 0.85 of bars (0.6); bars with no kick <= 3 percent.
3. Kick/snare collision: events with kick and snare on same step excluding layered mode == 0 (<= 1 per 20 bars).
4. Kick on 5 or 13 <= 3 percent of bars (8).
5. Onset-density histogram: per step mean count compared with 6.1 expected probability; require L1 distance between observed per-step probability (kick, snare, ghost separately) and table <= 0.15 per role averaged.
6. Velocity hierarchy: median backbeat snare velocity - median ghost velocity >= 50 (>= 40); ghost / backbeat ratio between 0.15 and 0.5; velocity standard deviation of backbeat snares >= 2 and <= 10 (not constant, not wild); no more than 5 percent of events at identical velocity within a role.
7. Ghost behavior: no three consecutive steps of ghost/snare; ghosts per bar <= 6; ghosts on strong steps (1, 5, 9, 13) == 0.
8. Fill at phrase end: in bar 4, P(event count in steps 13..16 > bar 1's count in 13..16) >= 0.5 over seeds; presence of fill or crash in bar 4 >= 0.7.
9. Phrase variation: bar 4 differs from bar 1 (Hamming distance on the 16-step role grid >= 3) in >= 0.8 of generations; bar 1 equals bar 2 within Hamming <= 3 (funk/jungle strict profiles).
10. Repetition guard: same chop in same role never twice in a row outside stutters, in >= 0.95 of cases; longest run of identical (step, chop) bars <= 2 (jungle) or none.
11. Swing check: measured swing ratio of odd/even 16th hat onsets equals configured within +-1 percentage point (after the timing-jitter is averaged); anchors unswung (mean offset within +-2 ms).
12. Timing budget: absolute timing offset of any event <= 20 ms for anchors-plus-humanize, <= 12 ms for non-laid-back profiles; no jitter on rolls finer than 1/32.
13. Roll shape: all roll events have monotone non-decreasing velocity (fills) or monotone non-increasing (stutters) within +-6 units; count within table 4.3; no roll longer than 1 bar; fine rolls (1/64 or finer) only in glitch profiles.
14. Reverse: reversed regions end within 1 ms of a grid line, only on snare/cymbal/ghost roles.
15. Determinism: same seed produces identical pattern hash (existing golden-hash gates; the new profile logic must keep default-off hashes unchanged).

Audio metrics (offline render at 44.1 and 48 kHz, 4 bars):

16. Click detection: for every slice boundary (event start and end sample positions from the render plan), compute max |x[n+1] - x[n]| in a window of +-8 samples; flag if > 0.25 of full scale for non-transient boundaries (event ends, event starts that are not on a transient) and > 0.5 for event starts on transients; target 0 flagged boundaries per 1000.
17. Edge energy: the RMS of the first 0.5 ms of every event must be <= 0.5 * RMS of the following 3 ms (fade-in is present) unless the source onset is inside the window; same for tails (last 1 ms <= 0.3 of preceding 3 ms).
18. Zero-crossing or fade: for each marker, either |sample at start| <= 0.01 or a fade-in >= 0.5 ms is applied (query render plan).
19. Transient preservation: peak amplitude of the first 5 ms of a kick or snare event must be >= 0.8 of the same slice rendered with a 0 ms fade (guards against attack eating transients); crest factor of the rendered bar within 2 dB of raw source.
20. Level consistency: RMS (first 50 ms) of backbeat snares within +-2 dB of each other; kick within +-3 dB; ghost at -12 to -22 dB re backbeat; output true peak <= -1 dBFS; no sample > 1.0.
21. Spectral clutter: low-frequency (< 150 Hz) energy: kick and snare layers should not exceed +4 dB over the sum of their solo energies (phase/stack check), and double kick within 60 ms must not exceed +3 dB peak over a single kick.
22. Choke: when a new kick starts, previous kick voice amplitude at start + 5 ms <= 0.2 of its previous value.
23. Level jumps: |RMS(100 ms window n+1) - RMS(window n)| in dB for consecutive non-silent windows not exceeding 12 dB except at fills/breakdowns; flag count 0 in the steady-state bars.
24. Retrigger variation: for rolls of >= 4 repeats, velocity standard deviation of repeats excluding the ramp trend >= 1 (not machine-gun), pitch spread <= 0.6 st.
25. Silence audit: no unintended gap longer than 1/16 note in bars 1 to 3 (only gate-drop gesture produces one, and only in bar 4).

Report each as pass/fail with counts in a test named `break_quality_*`; make thresholds a table in a header so profile changes update tests mechanically.

## 13. Test data suggestion

- Add `tests/data/break_profiles.json` with tables 6.1, 6.2, 6.3, 6.5 in machine-readable form; generator and tests read the same file so rules and acceptance stay in sync.
- Add a synthetic labelled source (kick 60 Hz decaying sine with click, snare noise burst plus 200 Hz, ghost = snare at -14 dB, hat band-passed noise) so audio metrics are deterministic, and one real-break regression test (CC0 loop) for click detection.
