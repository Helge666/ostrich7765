#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Does a transistor's exponential characteristic make the energy the recordings have over
the ROM data (bass drum, snare: 250-500 Hz)?

    vca_fit.py 505 SOUND_ORDER.bin NOTES_DIR

Model per sound: y = env(t) * (dc + (exp(a * x) - 1) / a), x the ROM sound as the DAC holds it
(-1..1, response.held, in the unit's order), env the engine's envelope (PcmMachine.cpp).
For a = dc = 0 it is the engine as it is (dc: a term following the envelope, the
transistor's quiescent current, through the coupling capacitors). Compared with the recording as the share of each third
octave in the sound's energy (40 Hz-5 kHz, bands where the recording has at least -50 dB of
the total), in dB rms. Fitted: a and dc per sound; then one drive A for all (dc = 0), each channel's a =
A * d / d(47k), d = 2.2k / (R + 2.2k) its documented divider (service notes). Needs numpy.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from response import NOTES, REGIONS, SR, bands, held, onset, read, sound  # noqa: E402

# PcmMachine.cpp: hold, v0, tau per sound.
HOLD, V0 = 0.035, 0.18
TAU = [0.258, 0.264, 0.264, 0.264, 0.112, 0.107, 0.135, 0.100,
       0.123, 0.107, 0.118, 0.107, 0.580, 0.608, 0.636, 0.210]
# Series resistor of each sound's channel (service notes), kOhm.
SERIES = [47, 47, 47, 47, 47, 47, 33, 47, 100, 82, 100, 82, 47, 47, 47, 47]




def shares(sig, nfft, fb):
    p = np.abs(np.fft.rfft(sig, nfft)) ** 2
    f = np.fft.rfftfreq(nfft, 1.0 / SR)
    out = np.array([p[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].sum() for c in fb])
    return out


def main():
    rom = np.frombuffer(open(sys.argv[2], "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    fb = bands()
    fb = fb[(fb >= 40) & (fb <= 5100)]
    cases = []
    for note, s in sorted(NOTES["505"].items()):
        y = read(os.path.join(sys.argv[3], f"note{note:03d}.wav"))
        on = onset(y)
        _, ln = REGIONS["505"][s]
        n = min(int(ln / 24955 * SR) + 480, len(y) - on - int(0.2 * SR))
        x = held(sound(rom, "505", s), 24955.0, n) / 128.0
        t = np.arange(n) / SR
        e = np.clip((np.exp(-np.maximum(t - HOLD, 0) / TAU[s]) - V0) / (1 - V0), 0, None)
        ys = y[max(0, on - 48):max(0, on - 48) + n].sum(axis=1)
        tail = y[-int(0.15 * SR):].sum(axis=1)
        nfft = 1 << int(np.ceil(np.log2(4 * n)))
        pr = shares(ys, nfft, fb)
        f = np.fft.rfftfreq(nfft, 1.0 / SR)
        pn = np.mean(tail ** 2) * n / (SR / 2) * (SR / nfft) * nfft / 2
        pr = np.maximum(pr - pn * np.array([((f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))).sum()
                                             for c in fb]), 1e-30)
        pr /= pr.sum()
        ok = pr > 1e-5
        cases.append((note, s, x, e, pr, ok, nfft))

    def err(case, a, dc=0.0):
        note, s, x, e, pr, ok, nfft = case
        m = e * (dc + (np.expm1(a * x) / a if a != 0 else x))
        m -= np.mean(m)                               # coupling capacitors
        pm = shares(m, nfft, fb)
        pm /= pm.sum()
        d = 10 * np.log10(np.maximum(pm[ok], 1e-30) / pr[ok])
        return float(np.sqrt(np.mean(d ** 2))), d

    # Per sound: drive a and a term dc that follows the envelope (the transistor's
    # quiescent current), in units of the sound's full scale.
    ag = np.round(np.arange(-2.0, 2.01, 0.2), 2)
    cg = np.round(np.arange(-0.6, 0.601, 0.03), 3)
    print("per sound: rms of the band shares (dB): engine now; envelope term only; both")
    for c in cases:
        tab = np.array([[err(c, a, k)[0] for k in cg] for a in ag])
        i0 = list(ag).index(0.0)
        j0 = int(np.argmin(tab[i0]))
        i, j = np.unravel_index(np.argmin(tab), tab.shape)
        print(f"  note {c[0]:3d} s{c[1]:<2d} ({SERIES[c[1]]:3d}k): {tab[i0, list(cg).index(0.0)]:5.2f};"
              f"  dc {cg[j0]:+.2f}: {tab[i0, j0]:5.2f};  a {ag[i]:+.1f} dc {cg[j]:+.2f}: {tab[i, j]:5.2f}")
    grid = np.concatenate([-np.linspace(0, 6, 61)[::-1], np.linspace(0, 6, 61)[1:]])
    d47 = 2.2 / (47 + 2.2)
    print("one drive A for all (a = A * d / d47):")
    best = None
    for A in grid:
        tot = [err(c, A * (2.2 / (SERIES[c[1]] + 2.2)) / d47)[0] for c in cases]
        if best is None or np.mean(tot) < best[0]:
            best = (np.mean(tot), A, tot)
    print(f"  A = {best[1]:+.1f}: mean {best[0]:.2f} dB (a = 0: "
          f"{np.mean([err(c, 0.0)[0] for c in cases]):.2f} dB)")
    for c, v in zip(cases, best[2]):
        print(f"    note {c[0]:3d} s{c[1]:<2d}: {err(c, 0.0)[0]:5.2f} -> {v:5.2f} dB")
    for note in (35, 38):
        c = [c for c in cases if c[0] == note][0]
        a = best[1] * (2.2 / (SERIES[c[1]] + 2.2)) / d47
        _, d0 = err(c, 0.0)
        _, d1 = err(c, a)
        fbk = fb[c[5]]
        print(f"  note {note}, model / real per band (dB), a=0 then a={a:+.1f}:")
        print("    Hz   " + " ".join(f"{v:5.0f}" for v in fbk))
        print("    a=0  " + " ".join(f"{v:5.1f}" for v in d0))
        print("    fit  " + " ".join(f"{v:5.1f}" for v in d1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
