#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Evaluate a note scan: which notes sounded, the onset latency, and cut each sound out.
#   py -3 -I scan_eval.py SCAN.wav [--cut DIR]
import argparse
import json
import os
import struct
import wave

ap = argparse.ArgumentParser()
ap.add_argument("wav")
ap.add_argument("--cut")
a = ap.parse_args()

w = wave.open(a.wav)
n, r = w.getnframes(), w.getframerate()
s = struct.unpack("<%dh" % (n * 2), w.readframes(n))
L, R = s[0::2], s[1::2]
M = [max(abs(x), abs(y)) for x, y in zip(L, R)]
meta = json.load(open(os.path.splitext(a.wav)[0] + ".json"))
hits = meta["hits"]


def peak(t0, t1):
    i0, i1 = max(0, int(t0 * r)), min(n, int(t1 * r))
    return max(M[i0:i1]) if i1 > i0 else 0


def onset(t0, t1, frac=0.2):
    i0, i1 = max(0, int(t0 * r)), min(n, int(t1 * r))
    p = max(M[i0:i1])
    for i in range(i0, i1):
        if M[i] >= frac * p:
            return i / r
    return None


# first sync: the first loud onset in the recording
first = next(i for i in range(n) if M[i] > 2000) / r
off = first - hits[0][0]
# refine: onset of the last sync against its send time
tl = hits[-1][0] + off
last = onset(tl - 0.05, tl + 0.1)
print(f"offset {off:.4f} s; last sync drift {1000 * (last - tl):.2f} ms over {hits[-1][0]:.1f} s")
drift = (last - tl) / hits[-1][0]

found = []
for t, note, vel in hits[1:-1]:
    ts = t * (1 + drift) + off
    before, after = peak(ts - 0.03, ts - 0.001), peak(ts, ts + 0.04)
    if after > 1500 and after > 2 * before:
        on = onset(ts - 0.002, ts + 0.04)
        found.append((note, 1000 * (on - ts), after))
for note, lat, p in found:
    print(f"note {note:3}  latency {lat:6.2f} ms  peak {p:6}")
print(f"{len(found)} notes sound")
if a.cut:
    os.makedirs(a.cut, exist_ok=True)
    gap = meta["gap"]
    for note, lat, p in found:
        t = [h[0] for h in hits[1:-1] if h[1] == note][0] * (1 + drift) + off + lat / 1000
        i0, i1 = int((t - 0.005) * r), int((t + gap - 0.01) * r)
        ww = wave.open(os.path.join(a.cut, f"note{note:03}.wav"), "wb")
        ww.setnchannels(2); ww.setsampwidth(2); ww.setframerate(r)
        ww.writeframes(struct.pack("<%dh" % (2 * (i1 - i0)), *s[2 * i0:2 * i1]))
        ww.close()
