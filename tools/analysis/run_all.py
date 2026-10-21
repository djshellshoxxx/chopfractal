#!/usr/bin/env python3
"""usage: run_all.py WORKDIR OUT_JSON  -> fetch data, run both analyses, merge into break_stats.json"""
import sys, os, json, subprocess
here = os.path.dirname(os.path.abspath(__file__)); w, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, here)
import fetch_data; fetch_data.main(w)
g, a = os.path.join(w, "g.json"), os.path.join(w, "a.json")
subprocess.check_call([sys.executable, "-I", os.path.join(here, "groove_stats.py"), os.path.join(w, "groove", "groove"), g])
subprocess.check_call([sys.executable, "-I", os.path.join(here, "audio_stats.py"), os.path.join(w, "sp"), a])
G, A = json.load(open(g)), json.load(open(a))
keep = ["funk", "hiphop", "soul", "dance", "afrobeat", "bpm_ge_120", "all", "fills"]
doc = dict(schema=1, note="Derived statistics only. No source audio or MIDI is included.",
           step_convention="16 steps per 4/4 bar, index 0 = downbeat. offsets: fraction of a 16th, negative = early",
           groove_midi={k: G["groups"][k] for k in keep if k in G["groups"]}, groove_meta={k: G[k] for k in ("source", "roles")},
           audio_loops={k: A[k] for k in ("loop_amen", "loop_amen_full", "loop_breakbeat", "loop_compus", "loop_garzul") if k in A},
           audio_note="Role labels are heuristic (band-flux). Reliable only for loop_amen and loop_amen_full; others are low confidence.")
json.dump(doc, open(out, "w"), indent=1)
