#!/usr/bin/env python3
"""Onset/role/step statistics from open-licensed break loops.
usage: audio_stats.py SP_DIR OUT.json  (SP_DIR holds loop_*.flac, see fetch_data.py)
Each loop is treated as seamless and bar aligned: tempo = bars*240/duration, bars chosen
so tempo lies in 80..180 and best matches librosa's tempo estimate."""
import sys, os, json, glob
import numpy as np, librosa, soundfile as sf

SR = 22050; HOP = 128
BANDS = {"low": (30, 130), "mid": (180, 1800), "high": (6000, 14000)}

def fit_bars(y, sr, dur):
    t0 = float(np.atleast_1d(librosa.beat.beat_track(y=y, sr=sr)[0])[0])
    best = None
    for nb in (1, 2, 4, 8):
        t = nb * 240.0 / dur
        for m in (0.5, 1, 2):
            d = abs(np.log2(t / (t0 * m)))
            if 80 <= t <= 190 and (best is None or d < best[0]): best = (d, nb, t)
    return None if best is None else (best[1], best[2])

def band_env(S, f, lo, hi):
    m = (f >= lo) & (f < hi)
    e = np.log1p(S[m].sum(0) * 10)
    return np.maximum(0, np.diff(e, prepend=e[0]))

def analyse(path):
    y, sr = sf.read(path)
    if y.ndim > 1: y = y.mean(1)
    y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    dur = len(y) / SR
    fb = fit_bars(y, SR, dur)
    if fb is None: return None
    nb, bpm = fb
    S = np.abs(librosa.stft(y, n_fft=1024, hop_length=HOP)) ** 2
    f = librosa.fft_frequencies(sr=SR, n_fft=1024)
    env = {k: band_env(S, f, *v) for k, v in BANDS.items()}
    mix = sum(e / (e.max() + 1e-9) for e in env.values())
    on = librosa.onset.onset_detect(onset_envelope=mix, sr=SR, hop_length=HOP, units="frames", delta=0.15, wait=int(0.03 * SR / HOP))
    step_s = 60.0 / bpm / 4
    nbk = {k: np.percentile(e, 99) + 1e-9 for k, e in env.items()}
    out = []
    for fr in on:
        t = fr * HOP / SR
        w = slice(max(0, fr - 1), fr + 5)
        fl = {k: float(env[k][w].max() / nbk[k]) for k in env}
        seg = S[:, fr:fr + 6].mean(1)
        cen = float((f * seg).sum() / (seg.sum() + 1e-12))
        lowfrac = float(seg[(f >= 30) & (f < 150)].sum() / (seg.sum() + 1e-12))
        rms = float(np.sqrt(np.mean(y[int(t * SR):int(t * SR) + int(0.05 * SR)] ** 2)))
        roles = []
        if fl["low"] > 0.45 and lowfrac > 0.25: roles.append("K")
        if fl["mid"] > 0.4 and fl["high"] > 0.12 and cen > 1200: roles.append("S")
        if fl["high"] > 0.12 and cen > 1500 and "S" not in roles and "K" not in roles: roles.append("H")
        if not roles: roles.append("X")
        pos = t / step_s; st = int(round(pos)); out.append(dict(pos=pos, step=st, off=pos - st, roles=roles, rms=rms))
    return dict(n_bars=nb, bpm=bpm, dur=dur, onsets=out, step_s=step_s)

def stats(name, a):
    nb = a["n_bars"]; total = 16 * nb
    roles = ["K", "S", "H", "X"]
    P = {r: np.zeros((nb, 16)) for r in roles}; V = {r: np.zeros((nb, 16)) for r in roles}
    offs = [[] for _ in range(16)]
    refrms = max([o["rms"] for o in a["onsets"]] + [1e-9])
    for o in a["onsets"]:
        s = o["step"] % total; b, st = divmod(s, 16)
        offs[st].append(o["off"])
        for r in o["roles"]:
            P[r][b, st] = 1; V[r][b, st] = o["rms"] / refrms
    res = dict(file=name, bpm=round(a["bpm"], 2), bars=nb, onsets=len(a["onsets"]))
    res["p_onset"] = {r: [round(float(x), 3) for x in P[r].mean(0)] for r in roles}
    res["vel_rel"] = {r: [round(float(V[r][:, s][P[r][:, s] > 0].mean()) if P[r][:, s].any() else 0, 3) for s in range(16)] for r in roles}
    res["bars_grid"] = {r: ["".join("x" if v else "." for v in row) for row in P[r]] for r in roles}
    res["offset_steps_mean"] = [round(float(np.mean(o)), 3) if o else 0 for o in offs]
    res["timing_sd_steps"] = round(float(np.std(np.concatenate([np.array(o) for o in offs if o]))), 3)
    if nb > 1:
        g = [np.concatenate([P[r][b] for r in "KSH"]) for b in range(nb)]
        res["bar_similarity_consecutive"] = [round(float((g[i] * g[i + 1]).sum() / max(1, ((g[i] + g[i + 1]) > 0).sum())), 3) for i in range(nb - 1)]
        res["onsets_per_bar"] = [int(sum(P[r][b].sum() for r in roles)) for b in range(nb)]
    return res

if __name__ == "__main__":
    d, out = sys.argv[1], sys.argv[2]
    res = {}
    for p in sorted(glob.glob(os.path.join(d, "loop_*.flac"))):
        n = os.path.basename(p)[:-5]
        an = analyse(p)
        if an is None:
            print(n, "skipped: no whole-bar tempo fit in 80..190 bpm"); continue
        res[n] = stats(n, an)
        r = res[n]; print(n, r["bpm"], r["bars"], r["onsets"])
        for k in "KSH": print(" ", k, r["bars_grid"][k])
    json.dump(res, open(out, "w"), indent=1)
