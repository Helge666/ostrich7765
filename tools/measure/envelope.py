#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The envelope a unit puts on each sound: recording over ROM sound, along the sound.

    envelope.py 505|626 SOUND_ORDER.bin NOTES_DIR [--sounds note,...] [--frame MS]

For every note the ROM sound is played as the DAC holds it (response.py, in the order the
unit plays it) and both it and the recording are cut into frames from the onset. Per
frame the gain is the recording's RMS over the ROM's (the recording's noise power, from
its quiet tail, subtracted); frames where either is too quiet are left out. Prints the
gain in dB relative to the first frames, every 50 ms, and the time constant of an
exponential fitted to it (least squares on the dB values, weighted by the ROM's level) as a
first look; the engine's model (hold, threshold) is in PcmMachine.cpp.
Needs numpy.
"""

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from response import NOTES, REGIONS, SR, held, onset, read, sound  # noqa: E402


def gains(rom, machine, note, s, notes_dir, rate, frame):
    yy = read(os.path.join(notes_dir, f"note{note:03d}.wav"))
    y = yy.sum(axis=1)
    on = onset(yy)
    _, ln = REGIONS[machine][s]
    n = min(int(ln / rate * SR), len(y) - on - int(0.16 * SR))
    x = held(sound(rom, machine, s), rate, n)
    # The onset is where the recording passes 5 % of its peak; the ROM sound's own onset
    # likewise, so that both frames start together.
    start = max(0, on - int(np.argmax(np.abs(x) > 0.05 * np.abs(x).max())))
    ys = y[start:start + n]
    noise = np.mean(y[-int(0.15 * SR):] ** 2)
    k = n // frame
    px = np.mean(x[:k * frame].reshape(k, frame) ** 2, axis=1)
    py = np.mean(ys[:k * frame].reshape(k, frame) ** 2, axis=1) - noise
    ok = (px > px.max() * 1e-4) & (py > 3 * noise)
    g = 10 * np.log10(np.maximum(py, 1e-30) / np.maximum(px, 1e-30))
    t = (np.arange(k) + 0.5) * frame / SR
    return t, g, ok, px


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("machine", choices=("505", "626"))
    ap.add_argument("rom")
    ap.add_argument("notes")
    ap.add_argument("--sounds", default=None)
    ap.add_argument("--frame", type=float, default=10.0)
    ap.add_argument("--rate", type=float, default=None)
    a = ap.parse_args()
    rate = a.rate or (24955.0 if a.machine == "505" else 25125.0)
    frame = int(a.frame * SR / 1000)
    rom = np.frombuffer(open(a.rom, "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    notes = NOTES[a.machine]
    if a.sounds:
        notes = {int(k): notes[int(k)] for k in a.sounds.split(",")}
    marks = np.arange(0.0, 1.2, 0.05)
    print("ms           " + " ".join(f"{1000 * m:5.0f}" for m in marks))
    for note, s in sorted(notes.items()):
        if not os.path.exists(os.path.join(a.notes, f"note{note:03d}.wav")):
            continue
        t, g, ok, px = gains(rom, a.machine, note, s, a.notes, rate, frame)
        if ok.sum() < 3:
            continue
        ref = np.median(g[ok][:3])
        row = []
        for m in marks:
            sel = ok & (np.abs(t - m) < 0.025)
            row.append(f"{np.median(g[sel]) - ref:5.1f}" if sel.any() else "    .")
        w = np.sqrt(px[ok])
        slope, icpt = np.polyfit(t[ok], g[ok], 1, w=w)
        tau = -20 / (np.log(10) * slope) if slope < 0 else np.inf   # amplitude e^(-t/tau)
        print(f"n{note:<3d}s{s:<2d}      " + " ".join(row) +
              f"   tau {1000 * tau:6.0f} ms over {1000 * t[ok][-1]:4.0f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
