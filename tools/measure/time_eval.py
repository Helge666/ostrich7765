#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Evaluate a timing scan: onset of every hit (template match, sub-sample), latency
# residuals after a linear clock fit, their spread, and a search for a service period.
#   python time_eval.py SCAN.wav   (needs numpy)
import json
import os
import sys
import wave

import numpy as np

p = sys.argv[1]
w = wave.open(p)
r = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(float).mean(1)
sends = np.array(json.load(open(os.path.splitext(p)[0] + ".json"))["sends"])

i0 = int(np.argmax(np.abs(x) > 2000))
T = x[i0 - 48:i0 + int(0.02 * r)]                      # template: first hit, 1 ms before onset
off = i0 / r - sends[0]
on = []
for s in sends:
    c = int((s + off) * r)
    lo, hi = c - int(0.012 * r), c + int(0.012 * r)
    seg = x[lo:hi + len(T)]
    cc = np.correlate(seg, T, "valid")
    k = int(np.argmax(cc))
    if 0 < k < len(cc) - 1:                            # parabolic sub-sample peak
        a_, b_, c_ = cc[k - 1], cc[k], cc[k + 1]
        k = k + 0.5 * (a_ - c_) / (a_ - 2 * b_ + c_)
    on.append((lo + k + 48) / r)
on = np.array(on)
A = np.vstack([sends, np.ones_like(sends)]).T
(slope, icpt), *_ = np.linalg.lstsq(A, on, rcond=None)
d = 1000 * (on - (slope * sends + icpt))
print(f"clock ratio {slope:.6f} ({1e6 * (slope - 1):.0f} ppm); residuals ms: "
      f"min {d.min():.2f} max {d.max():.2f} std {d.std():.3f}")
h, e = np.histogram(d, bins=np.arange(np.floor(d.min()), d.max() + 0.25, 0.25))
for c, lo in zip(h, e):
    print(f"{lo:6.2f} {'#' * c}")
# service period: residual should rise linearly with (send phase mod P) -- a sawtooth
best = []
for P in np.arange(0.3, 12.0, 0.005):
    ph = (sends * 1000 * slope) % P
    # model: d = const - ph + (P if ph late) ... fit circular: score = corr of d with -phase
    z = np.exp(2j * np.pi * (1000 * sends * slope / P))
    best.append((abs(np.mean(z * np.exp(2j * np.pi * d / P))), P))
best.sort(reverse=True)
print("period candidates (score, ms):", [(round(s, 3), round(P, 3)) for s, P in best[:8]])
