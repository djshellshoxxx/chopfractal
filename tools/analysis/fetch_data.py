#!/usr/bin/env python3
"""Download open-licensed source data into a work dir (never into the repo).
usage: fetch_data.py WORKDIR
Sources: Groove MIDI Dataset (CC BY 4.0), Sonic Pi sample loops (CC0)."""
import sys, os, urllib.request, zipfile
G = "https://storage.googleapis.com/magentadata/datasets/groove/groove-v1.0.0-midionly.zip"
SP = "https://raw.githubusercontent.com/sonic-pi-net/sonic-pi/dev/etc/samples/%s.flac"
LOOPS = ["loop_amen", "loop_amen_full", "loop_breakbeat", "loop_compus", "loop_garzul",
         "loop_industrial", "loop_mika", "loop_safari", "loop_tabla"]

def get(url, dst):
    if not os.path.exists(dst):
        urllib.request.urlretrieve(url, dst)

def main(w):
    os.makedirs(os.path.join(w, "sp"), exist_ok=True)
    z = os.path.join(w, "groove.zip")
    get(G, z)
    zipfile.ZipFile(z).extractall(os.path.join(w, "groove"))
    for n in LOOPS:
        get(SP % n, os.path.join(w, "sp", n + ".flac"))

if __name__ == "__main__":
    main(sys.argv[1])
