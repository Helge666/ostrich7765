#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pair scan: record the box while sending every pair of the given notes, the second note
`--delay` after the first; write the send times (as notescan.py, same sync notes).

    py -3 -I pairscan.py OUT.wav --notes 35,36,... [--ch 10] [--delay 0.03] [--gap 0.7]

Which sounds cut each other off (one channel of the address counter): cut_eval.py.
"""
import argparse
import itertools
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import winmidi  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--notes", required=True)
ap.add_argument("--ch", type=int, default=10)
ap.add_argument("--vel", type=int, default=127)
ap.add_argument("--delay", type=float, default=0.03)
ap.add_argument("--gap", type=float, default=0.7)
ap.add_argument("--sync", type=int, default=38)
ap.add_argument("--audio", default="IN 7-8 (BEHRINGER X-AIR)")
ap.add_argument("--midi", default="XR18 MIDI OUT")
a = ap.parse_args()

pairs = list(itertools.combinations([int(n) for n in a.notes.split(",")], 2))
dur = (len(pairs) + 6) * a.gap + 4
rec = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "dshow",
                        "-sample_rate", "48000", "-sample_size", "16", "-channels", "2",
                        "-i", f"audio={a.audio}", "-t", f"{dur:.1f}", "-c:a", "pcm_s16le", a.out])
time.sleep(2.0)
o = winmidi.Out(a.midi)
ch = a.ch - 1
log = []
t0 = time.perf_counter()


def wait_until(t):
    while time.perf_counter() - t0 < t:
        time.sleep(0.0005)


def hit(n):
    log.append((round(time.perf_counter() - t0, 6), n, a.vel))
    o.send(0x90 | ch, n, a.vel)


def off(n):
    o.send(0x80 | ch, n, 0)


hit(a.sync)
wait_until(0.05)
off(a.sync)
t = 2 * a.gap
for i, (p, q) in enumerate(pairs):
    wait_until(t)
    hit(p)
    wait_until(t + a.delay)
    hit(q)
    wait_until(t + a.delay + 0.05)
    off(p)
    off(q)
    t += a.gap
    if i % 20 == 0:
        print(f"{i}/{len(pairs)}", end=" ", flush=True)
t += a.gap * 2
wait_until(t)
hit(a.sync)
wait_until(t + 0.05)
off(a.sync)
o.close()
print()
rec.wait()
json.dump({"ch": a.ch, "gap": a.gap, "delay": a.delay, "pairs": pairs, "hits": log},
          open(os.path.splitext(a.out)[0] + ".json", "w"))
print("done", a.out)
