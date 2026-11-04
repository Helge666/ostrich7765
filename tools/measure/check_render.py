#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""An engine render against the recordings of a unit: spectrum and level per sound.

    ostrich_engine_render 505 out.wav DUMP > out.txt
    check_render.py 505 out.wav out.txt NOTES_DIR [--r-offset DB]

For every hit at velocity 127 in the render and the recording of the same note
(response.py): the share of each third octave in the sound's energy (40 Hz-5 kHz, both
signals over the same length from 1 ms before the onset, the recording's noise
subtracted, bands under -50 dB of the recording's total left out) as the rms of the
differences in dB; and the level of each output over the first 30 ms, relative to the
loudest output of all sounds, render minus recording. --r-offset (dB, default 1.23) is
taken off the recording's right output (analysis/TR-505.md, "Levels and pan").
Needs numpy.
"""

import argparse
import os
import re
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from response import REGIONS, SR, bands, onset, read  # noqa: E402


def shares(sig, nfft, fb, f):
    p = np.abs(np.fft.rfft(sig, nfft)) ** 2
    return np.array([p[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].sum() for c in fb])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("machine", choices=("505", "626"))
    ap.add_argument("render")
    ap.add_argument("printout")
    ap.add_argument("notes")
    ap.add_argument("--r-offset", type=float, default=1.23)
    a = ap.parse_args()
    rate = 24955.0 if a.machine == "505" else 25125.0
    w = wave.open(a.render)
    assert w.getframerate() == SR
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2) / 32768.0
    hits = re.findall(r"^\s*([\d.]+) s\s+note (\d+) \(sound (\d+)\)\s+vel 127",
                      open(a.printout).read(), re.M)
    fb = bands()
    fb = fb[(fb >= 40) & (fb <= 5100)]
    win = int(0.03 * SR)
    rows, lev_e, lev_r = [], {}, {}
    for t, note, s in hits:
        note, s = int(note), int(s)
        path = os.path.join(a.notes, f"note{note:03d}.wav")
        if not os.path.exists(path):
            continue
        y = read(path)
        on = onset(y)
        tail = y[-int(0.15 * SR):]
        _, ln = REGIONS[a.machine][s]
        n = min(int(ln / rate * SR) + 480, len(y) - on - int(0.2 * SR))
        seg = x[int(float(t) * SR):int(float(t) * SR) + SR]
        o = int(np.argmax(np.abs(seg).max(axis=1) > 0.05 * np.abs(seg).max()))
        # Spectrum.
        nfft = 1 << int(np.ceil(np.log2(4 * n)))
        f = np.fft.rfftfreq(nfft, 1.0 / SR)
        ys = y[max(0, on - 48):max(0, on - 48) + n].sum(axis=1)
        pr = shares(ys, nfft, fb, f)
        pn = np.mean(tail.sum(axis=1) ** 2) * n / (SR / 2) * (SR / nfft) * nfft / 2
        pr = np.maximum(pr - pn * np.array([((f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))).sum()
                                             for c in fb]), 1e-30)
        pr /= pr.sum()
        ok = pr > 1e-5
        m = seg[max(0, o - 48):max(0, o - 48) + n].sum(axis=1)
        m = m - m.mean()
        pm = shares(m, nfft, fb, f)
        pm /= pm.sum()
        d = 10 * np.log10(np.maximum(pm[ok], 1e-30) / pr[ok])
        rows.append((note, s, float(np.sqrt(np.mean(d ** 2)))))
        # Level.
        lev_e[note] = 10 * np.log10(np.mean(seg[o:o + win] ** 2, axis=0))
        lev_r[note] = 10 * np.log10(np.mean(y[on:on + win] ** 2, axis=0) -
                                    np.mean(tail ** 2, axis=0)) - np.array([0.0, a.r_offset])
    te = max(max(v) for v in lev_e.values())
    tr = max(max(v) for v in lev_r.values())
    print("note sound  spectrum dB rms   level L, R (render - recording, dB)")
    for note, s, e in rows:
        dl = (lev_e[note] - te) - (lev_r[note] - tr)
        print(f"{note:4d} {s:5d}  {e:8.2f}          {dl[0]:+5.2f} {dl[1]:+5.2f}")
    print(f"mean spectrum {np.mean([r[2] for r in rows]):.2f} dB rms; largest level difference "
          f"{max(np.abs((lev_e[k] - te) - (lev_r[k] - tr)).max() for k in lev_e):.2f} dB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
