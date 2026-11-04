#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Timing scan: COUNT hits of one note at random intervals; record and log send times.
#   py -3 -I timescan.py OUT.wav [--note 35] [--count 300] [--base 0.25] [--rand 0.02]
import argparse
import json
import os
import random
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import winmidi  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--ch", type=int, default=10)
ap.add_argument("--note", type=int, default=35)
ap.add_argument("--count", type=int, default=300)
ap.add_argument("--base", type=float, default=0.25)
ap.add_argument("--rand", type=float, default=0.02)
ap.add_argument("--audio", default="IN 7-8 (BEHRINGER X-AIR)")
ap.add_argument("--midi", default="XR18 MIDI OUT")
a = ap.parse_args()

dur = a.count * (a.base + a.rand) + 5
rec = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "dshow",
                        "-sample_rate", "48000", "-sample_size", "16", "-channels", "2",
                        "-i", f"audio={a.audio}", "-t", f"{dur:.1f}", "-c:a", "pcm_s16le", a.out])
time.sleep(2.0)
o = winmidi.Out(a.midi)
ch = a.ch - 1
random.seed(7765)
log = []
t0 = time.perf_counter()
t = 0.0
for k in range(a.count):
    while time.perf_counter() - t0 < t:
        pass                                          # busy wait: sub-ms send times
    ts = time.perf_counter() - t0
    o.send(0x90 | ch, a.note, 127)
    log.append(ts)
    o.send(0x80 | ch, a.note, 0)
    t += a.base + random.uniform(0, a.rand)
o.close()
rec.wait()
json.dump({"note": a.note, "sends": log}, open(os.path.splitext(a.out)[0] + ".json", "w"))
print("done", a.out, len(log))
