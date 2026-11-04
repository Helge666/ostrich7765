#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Is the DAC linear? Compares a recording's envelope with the ROM sound's along its decay.

    linearity.py 505|626 SOUND_ORDER.bin NOTES_DIR [--rate HZ]

For every note: the ROM sound played as the DAC holds it (response.py) and the recording,
both cut into 10 ms frames from the onset; per frame the RMS of each. With a linear DAC
(and linear stages after it) recording / ROM is the same in every frame, whatever the
level. Prints, per sound, the ratio in dB relative to the loudest frame, against the ROM
frame's level in dB below its loudest; then the pooled ratio per 6 dB of ROM level.
"""

import argparse
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
    ap.add_argument("--rate", type=float, default=None)
    a = ap.parse_args()
    rate = a.rate or (24955.0 if a.machine == "505" else 25125.0)
    rom = np.frombuffer(open(a.rom, "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    frame = int(0.01 * SR)
    pooled = []
    for note, s in sorted(NOTES[a.machine].items()):
        path = os.path.join(a.notes, f"note{note:03d}.wav")
        if not os.path.exists(path):
            continue
        y = read(path)
        on = onset(y)
        off, ln = REGIONS[a.machine][s]
        snd = sound(rom, a.machine, s)
        n = min(int(ln / rate * SR), len(y) - on - 10)
        x = held(snd, rate, n)
        yy = y[on:on + n].sum(axis=1)
        # The noise floor of the recording, from its quiet tail.
        noise = np.sqrt(np.mean(y[-int(0.15 * SR):].sum(axis=1) ** 2))
        k = n // frame
        rx = np.sqrt(np.mean(x[:k * frame].reshape(k, frame) ** 2, axis=1))
        ry = np.sqrt(np.mean(yy[:k * frame].reshape(k, frame) ** 2, axis=1))
        ok = (rx > rx.max() * 1e-3) & (ry > 4 * noise)
        lx = 20 * np.log10(np.maximum(rx, 1e-12) / rx.max())
        ratio = 20 * np.log10(np.maximum(ry, 1e-12) / np.maximum(rx, 1e-12))
        ratio -= ratio[np.argmax(rx)]
        line = []
        for lvl in range(0, -48, -6):
            sel = ok & (lx <= lvl) & (lx > lvl - 6)
            line.append(f"{np.median(ratio[sel]):6.1f}" if sel.sum() else "     .")
            if sel.sum():
                pooled.append((lvl, np.median(ratio[sel])))
        print(f"note {note:3d} sound {s:2d}: " + " ".join(line))
    print("ROM level (dB):     " + " ".join(f"{lvl:6d}" for lvl in range(0, -48, -6)))
    print("pooled median:      " + " ".join(
        f"{np.median([r for l, r in pooled if l == lvl]):6.1f}" if any(l == lvl for l, _ in pooled)
        else "     ." for lvl in range(0, -48, -6)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
