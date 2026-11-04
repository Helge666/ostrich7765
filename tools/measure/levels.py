#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Level and pan of every sound at the unit's two outputs, against its ROM data.

    levels.py 505|626 SOUND_ORDER.bin NOTES_DIR [--window MS] [--json OUT]

For every note (recordings as for response.py: one note each, 48 kHz stereo) the ROM
sound is played as the DAC holds it, in the order the unit plays it, and lined up with the
recording (onset at 5 % of the peak in both). Over the first --window ms (default 30,
inside the envelope's flat start, envelope.py) the gain of each output is the
recording's RMS over the ROM sound's, the recording's noise (from its quiet tail)
subtracted. Prints per sound the gains of both outputs in dB, relative to the loudest
output of all sounds, and the balance (R - L, dB); --json writes them. The recording's
first channel is taken as the left output. Broadband RMS: what the analogue stages add to
a sound (response.py) counts into its level. Needs numpy.
"""

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from response import NOTES, REGIONS, SR, held, onset, read, sound  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("machine", choices=("505", "626"))
    ap.add_argument("rom")
    ap.add_argument("notes")
    ap.add_argument("--window", type=float, default=30.0)
    ap.add_argument("--rate", type=float, default=None)
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    rate = a.rate or (24955.0 if a.machine == "505" else 25125.0)
    rom = np.frombuffer(open(a.rom, "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    win = int(a.window * SR / 1000)
    rows = []
    for note, s in sorted(NOTES[a.machine].items()):
        path = os.path.join(a.notes, f"note{note:03d}.wav")
        if not os.path.exists(path):
            continue
        y = read(path)
        on = onset(y)
        _, ln = REGIONS[a.machine][s]
        x = held(sound(rom, a.machine, s), rate, int(ln / rate * SR))
        xo = int(np.argmax(np.abs(x) > 0.05 * np.abs(x).max()))
        px = np.mean(x[xo:xo + win] ** 2)
        tail = y[-int(0.15 * SR):]
        g = []
        for ch in range(2):
            py = np.mean(y[on:on + win, ch] ** 2) - np.mean(tail[:, ch] ** 2)
            g.append(10 * np.log10(max(py, 1e-20) / px))
        rows.append((note, s, g))
    top = max(max(g) for _, _, g in rows)
    print("note sound      L dB    R dB   R-L dB")
    out = {}
    for note, s, g in rows:
        print(f"{note:4d} {s:5d}  {g[0] - top:7.1f} {g[1] - top:7.1f}  {g[1] - g[0]:+6.1f}")
        out[str(s)] = [round(g[0] - top, 2), round(g[1] - top, 2)]
    if a.json:
        json.dump({"machine": a.machine, "window_ms": a.window, "sounds": out}, open(a.json, "w"),
                  indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
