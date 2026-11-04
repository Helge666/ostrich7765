#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a TR-505 / TR-626 render of ostrich_engine_render against the sound ROM.

    ostrich_engine_render 505 out.wav DUMP > out.txt
    check_pcm_render.py 505|626 out.wav SOUND_ORDER.bin out.txt

SOUND_ORDER.bin is the ROM in sound order (tools/tr505_rom.py or tr626_rom.py
--canonical); out.txt is the tool's printout, which gives each hit's time and sound. For
the hits at velocity 127 the script finds the rate at which the ROM sound best matches the
render (normalised cross correlation over the first 100 ms, swept +-2 % around 25 kHz).
Needs numpy.
"""

import re
import sys
import wave

import numpy as np

BANK = [(s * 0x2000, 0x2000) for s in range(4)] + \
       [(s * 0x2000 + h * 0x1000, 0x1000) for s in range(4, 7) for h in range(2)]
TR505 = BANK + [(0xE000, 0x1000), (0xF000, 0x1000), (0x10000, 0x8000), (0x18000, 0x4000),
                (0x1C000, 0x2000), (0x1E000, 0x2000)]
TR626_BANK = BANK + [(0xE000, 0x2000), (0x10000, 0x8000), (0x18000, 0x4000),
                     (0x1C000, 0x2000), (0x1E000, 0x2000)]
TR626 = [(b + o, n) for b in (0, 0x20000) for o, n in TR626_BANK]
# The unit reads sample n of a sound at n ^ FLIP (PcmMachine.cpp, analysis/TR-505.md).
FLIP = {"505": {12: 7, 13: 3, 14: 1, 15: 1}, "626": {}}


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    sounds = TR505 if sys.argv[1] == "505" else TR626
    w = wave.open(sys.argv[2])
    sr = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2)[:, 0]
    x = x.astype(np.float64)
    rom = np.frombuffer(open(sys.argv[3], "rb").read(), dtype=np.uint8).astype(np.float64) - 128
    hits = re.findall(r"^\s*([\d.]+) s\s+(note \d+) \(sound (\d+)\)\s+vel 127", open(sys.argv[4]).read(),
                      re.M)
    worst = 1.0
    for t, note, s in hits:
        off, ln = sounds[int(s)]
        snd = rom[off + (np.arange(ln) ^ FLIP[sys.argv[1]].get(int(s), 0))]
        n = int(0.1 * sr)
        a = int(float(t) * sr)
        seg = x[a:a + n + 200]
        best = (0.0, 0, 0)
        for rel in np.linspace(-0.02, 0.02, 81):
            r = 25000 * (1 + rel)
            ref = np.interp(np.arange(n) * r / sr, np.arange(len(snd)), snd, right=0.0)
            cc = [np.dot(seg[k:k + n], ref) for k in range(0, 60)]
            k = int(np.argmax(np.abs(cc)))
            sa = seg[k:k + n]
            den = np.sqrt(np.dot(sa, sa) * np.dot(ref, ref))
            c = abs(cc[k]) / den if den > 0 else 0.0
            if c > best[0]:
                best = (c, r, k)
        worst = min(worst, best[0])
        print(f"{note:8s} sound {int(s):2d}  best {best[1]:8.1f} Hz ({100 * (best[1] / 25000 - 1):+.2f} %)"
              f"  corr {best[0]:.4f}  lag {1000 * best[2] / sr:.2f} ms")
    print(f"lowest correlation {worst:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
