#!/usr/bin/env python3
"""Per-step role statistics from the Groove MIDI Dataset (CC BY 4.0).
usage: groove_stats.py GROOVE_DIR OUT.json   (GROOVE_DIR contains info.csv)"""
import sys, os, csv, json, collections
import numpy as np, mido

ROLE = {36: "K", 35: "K", 38: "S", 40: "S", 37: "S", 42: "H", 44: "H", 46: "O",
        51: "R", 59: "R", 53: "R", 49: "C", 57: "C", 55: "C", 52: "C",
        41: "T", 43: "T", 45: "T", 47: "T", 48: "T", 50: "T"}
ROLES = ["K", "S", "H", "O", "R", "C", "T"]
GROUPS = {"funk": ["funk"], "hiphop": ["hiphop"], "soul": ["soul"], "dance": ["dance"],
          "afrobeat": ["afrobeat"], "rock": ["rock"], "jazz": ["jazz"], "latin": ["latin"]}

def load(path):
    m = mido.MidiFile(path); tpb = m.ticks_per_beat
    t = 0; ev = []
    for msg in mido.merge_tracks(m.tracks):
        t += msg.time
        if msg.type == "note_on" and msg.velocity > 0 and msg.note in ROLE:
            ev.append((t / tpb, ROLE[msg.note], msg.velocity))
    return ev

def analyse_file(ev, bpm):
    """returns list of bars; each bar: list of (step, offset_steps, role, vel)."""
    nb = int(max(e[0] for e in ev) // 4) + 1
    bars = [[] for _ in range(nb)]
    for b, r, v in ev:
        p = b * 4  # in 16th steps
        s = int(round(p)); off = p - s
        bar, st = divmod(s, 16)
        if bar < nb:
            bars[bar].append((st, off, r, v))
    return [x for x in bars if x]

def main(d, out):
    rows = list(csv.DictReader(open(os.path.join(d, "info.csv"))))
    acc = {}
    def A(g):
        if g not in acc:
            acc[g] = dict(files=0, bars=0,
                          cnt=np.zeros((7, 16)), vs=np.zeros((7, 16)), vq=np.zeros((7, 16)),
                          offs=[[] for _ in range(16)], offms=[[] for _ in range(16)],
                          snare_vel=[], ghost=0, snare_n=0, ioi=collections.Counter(),
                          ks=np.zeros((16, 16)), sim=[[], [], [], []], eighth_pos=[],
                          hatvel=[[] for _ in range(16)], bpm=[], fillcnt=np.zeros((7, 16)), fillbars=0,
                          dens=[[], [], [], []])
        return acc[g]
    for r in rows:
        if r["time_signature"] != "4-4":
            continue
        style = r["style"].split("/")[0]
        if style not in GROUPS: continue
        isfill = r["beat_type"] != "beat"
        try: ev = load(os.path.join(d, r["midi_filename"]))
        except Exception: continue
        if len(ev) < 16: continue
        bpm = float(r["bpm"]); bars = analyse_file(ev, bpm)
        if len(bars) < 2: continue
        # drop trailing partial bar
        bars = bars[:-1] if len(bars) > 2 else bars
        gl = ["fills"] if isfill else [style, "all"] + (["bpm_ge_120"] if bpm >= 120 else [])
        for g in gl:
            a = A(g); a["files"] += 1; a["bpm"].append(bpm)
            sv = [v for bar in bars for (_, _, ro, v) in bar if ro == "S"]
            med = np.median([v for bar in bars for (st, _, ro, v) in bar if ro == "S" and st in (4, 12)] or [100])
            grids = []
            for bi, bar in enumerate(bars):
                a["bars"] += 1
                grid = np.zeros((7, 16))
                allst = []
                for st, off, ro, v in bar:
                    i = ROLES.index(ro)
                    a["cnt"][i, st] += 1; a["vs"][i, st] += v / 127; a["vq"][i, st] += (v / 127) ** 2
                    grid[i, st] = 1
                    a["offs"][st].append(off)
                    a["offms"][st].append(off * 60000 / bpm / 4)
                    allst.append(st)
                    if ro == "S":
                        a["snare_n"] += 1; a["snare_vel"].append(v)
                        if v < 0.5 * med and st not in (4, 12): a["ghost"] += 1
                    if ro in ("H", "R") and st % 2 == 1 and False: pass
                for k in [x[0] for x in bar if x[2] == "K"]:
                    for s_ in [x[0] for x in bar if x[2] == "S"]:
                        a["ks"][k, s_] += 1
                u = sorted(set(allst))
                for x, y in zip(u, u[1:]): a["ioi"][y - x] += 1
                grids.append(grid)
                a["dens"][bi % 4].append(len(allst))
            for bi in range(1, len(grids)):
                x, y = grids[bi - 1].ravel(), grids[bi].ravel()
                j = (x * y).sum() / max(1, ((x + y) > 0).sum())
                a["sim"][bi % 4].append(j)
    res = dict(source="Groove MIDI Dataset v1.0.0 (Magenta), CC BY 4.0", step_unit="16th note, 16 steps per 4/4 bar",
               roles=dict(K="kick 35/36", S="snare 37/38/40", H="closed+pedal hat 42/44", O="open hat 46",
                          R="ride/bell 51/53/59", C="crash/splash 49/52/55/57", T="toms 41-50"), groups={})
    for g, a in acc.items():
        if a["bars"] < 30: continue
        n = a["bars"]; cnt = a["cnt"]
        p = cnt / n
        mv = np.divide(a["vs"], cnt, out=np.zeros_like(cnt), where=cnt > 0)
        sd = np.sqrt(np.maximum(0, np.divide(a["vq"], cnt, out=np.zeros_like(cnt), where=cnt > 0) - mv ** 2))
        offs = a["offs"]
        sw = {}
        for st in range(16):
            o = np.array(offs[st]) if offs[st] else np.zeros(1)
            sw[st] = dict(mean=round(float(o.mean()), 4), sd=round(float(o.std()), 4))
        ms = [round(float(np.mean(a["offms"][s])) if a["offms"][s] else 0, 2) for s in range(16)]
        odd = np.concatenate([np.array(offs[s]) for s in range(1, 16, 2) if offs[s]])
        e8 = np.concatenate([np.array(offs[s]) for s in range(2, 16, 4) if offs[s]])
        sv = np.array(a["snare_vel"])
        tot = sum(a["ioi"].values())
        res["groups"][g] = dict(
            files=a["files"], bars=n, bpm_median=float(np.median(a["bpm"])),
            p_onset={ROLES[i]: [round(float(x), 3) for x in p[i]] for i in range(7)},
            vel_mean={ROLES[i]: [round(float(x), 3) for x in mv[i]] for i in range(7)},
            vel_sd={ROLES[i]: [round(float(x), 3) for x in sd[i]] for i in range(7)},
            offset_steps_by_step=sw, offset_ms_by_step=ms,
            swing=dict(odd16_mean_offset_steps=round(float(odd.mean()), 4),
                       eighth_offbeat_mean_offset_steps=round(float(e8.mean()), 4),
                       eighth_ratio=round(float((2 + e8.mean()) / (2 - e8.mean())), 3),
                       timing_sd_steps=round(float(np.concatenate([np.array(o) for o in offs if o]).std()), 4)),
            snare=dict(ghost_rate_of_snare_onsets=round(a["ghost"] / max(1, a["snare_n"]), 3),
                       ghost_per_bar=round(a["ghost"] / n, 3),
                       vel_pctl={str(q): float(np.percentile(sv, q)) / 127 for q in (5, 25, 50, 75, 95)}),
            ioi_steps={str(k): round(v / tot, 4) for k, v in sorted(a["ioi"].items()) if k <= 8},
            bar_similarity_by_bar_mod4={str(i): round(float(np.mean(a["sim"][i])), 3) for i in range(4) if a["sim"][i]},
            onsets_per_bar_by_bar_mod4={str(i): round(float(np.mean(a["dens"][i])), 2) for i in range(4) if a["dens"][i]},
        )
        ks = a["ks"]
        snare_bars_per_step = np.maximum(1, cnt[1])
        res["groups"][g]["p_kick_step_given_snare_step"] = {
            "kick_steps_rows_snare_steps_cols_for_steps_0_4_8_12": {
                str(k): {str(s_): round(float(ks[k, s_] / snare_bars_per_step[s_]), 3) for s_ in (4, 12)} for k in range(16)}}
    json.dump(res, open(out, "w"), indent=1)
    print({g: (v["files"], v["bars"]) for g, v in res["groups"].items()})

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
