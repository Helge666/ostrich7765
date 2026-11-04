#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Note scan: record the box while sending every note once; write the send times.
#   py -3 -I notescan.py OUT.wav [--ch 10] [--vel 127] [--gap 1.2] [--lo 0] [--hi 127]
# A sync note (38) is sent first and last so the send times can be put on the recording's
# time line (send times are perf_counter seconds, relative to the first sync note).
import argparse
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import winmidi  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--ch", type=int, default=10)
ap.add_argument("--vel", type=int, default=127)
ap.add_argument("--gap", type=float, default=1.2)
ap.add_argument("--lo", type=int, default=0)
ap.add_argument("--hi", type=int, default=127)
ap.add_argument("--sync", type=int, default=38)
ap.add_argument("--vels", help="comma list of notes: sweep velocity 1..127 on each instead of a note scan")
ap.add_argument("--allvel", action="store_true")
ap.add_argument("--audio", default="IN 7-8 (BEHRINGER X-AIR)")
ap.add_argument("--midi", default="XR18 MIDI OUT")
a = ap.parse_args()

if a.vels:
    VELS = list(range(1, 128)) if a.allvel else [1, 8, 16, 24, 32, 40, 48, 56, 63, 64, 65, 72, 80, 88, 96, 100, 104, 112, 120, 127]
    seq = [(n, v) for n in map(int, a.vels.split(",")) for v in VELS]
else:
    seq = [(n, a.vel) for n in range(a.lo, a.hi + 1)]
notes = seq
dur = (len(notes) + 4) * a.gap + 4
rec = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "dshow",
                        "-sample_rate", "48000", "-sample_size", "16", "-channels", "2",
                        "-i", f"audio={a.audio}", "-t", f"{dur:.1f}", "-c:a", "pcm_s16le", a.out])
time.sleep(2.0)
o = winmidi.Out(a.midi)
ch = a.ch - 1
log = []
t0 = None


def hit(n, vel):
    global t0
    t = time.perf_counter()
    if t0 is None:
        t0 = t
    o.send(0x90 | ch, n, vel)
    log.append((round(t - t0, 6), n, vel))
    time.sleep(0.05)
    o.send(0x80 | ch, n, 0)


def wait_until(t):
    while time.perf_counter() - t0 < t:
        time.sleep(0.001)


hit(a.sync, a.vel)
t = 0.0
for n, v in notes:
    t += a.gap
    wait_until(t)
    hit(n, v)
    print(f"{n}/{v}", end=" ", flush=True)
t += a.gap * 2
wait_until(t)
hit(a.sync, a.vel)
o.close()
print()
rec.wait()
json.dump({"ch": a.ch, "gap": a.gap, "hits": log}, open(os.path.splitext(a.out)[0] + ".json", "w"))
print("done", a.out)
