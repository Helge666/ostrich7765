#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit the engine's envelope model to the recordings of a unit.

    env_fit.py 505|626 SOUND_ORDER.bin NOTES_DIR [--frame MS] [--hold MS] [--v0 V]

Model (PcmMachine.cpp): flat for `hold`, then g = (exp(-(t - hold) / tau) - v0) / (1 - v0),
silent from g = 0. hold and v0 are shared by all sounds, tau is per sound. The gain per
frame comes from envelope.gains (recording RMS over ROM RMS, noise taken off). Fitted on
the dB values, weighted by the ROM's level, with a free level per sound; frames after the
model has reached silence count as an error of their distance above the noise. Prints the
best shared hold and v0 (or uses the given ones), tau per sound and the rms error.
Needs numpy.
"""

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from envelope import gains  # noqa: E402
from response import NOTES  # noqa: E402


def model_db(t, hold, v0, tau):
    e = np.where(t < hold, 1.0, (np.exp(-np.maximum(t - hold, 0) / tau) - v0) / (1 - v0))
    return 20 * np.log10(np.maximum(e, 1e-3))


TAUS = np.exp(np.linspace(np.log(0.03), np.log(3.0), 161))


def fit_one(t, g, w, hold, v0):
    best = None
    for tau in TAUS:
        m = model_db(t, hold, v0, tau)
        c = np.sum(w * (g - m)) / np.sum(w)
        err = np.sum(w * (g - m - c) ** 2) / np.sum(w)
        if best is None or err < best[0]:
            best = (err, tau, c)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("machine", choices=("505", "626"))
    ap.add_argument("rom")
    ap.add_argument("notes")
    ap.add_argument("--frame", type=float, default=10.0)
    ap.add_argument("--hold", type=float, default=None, help="ms")
    ap.add_argument("--v0", type=float, default=None)
    ap.add_argument("--skip", type=float, default=10.0, help="ms of attack left out")
    a = ap.parse_args()
    rate = 24955.0 if a.machine == "505" else 25125.0
    frame = int(a.frame * 48000 / 1000)
    rom = np.frombuffer(open(a.rom, "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    cases = []
    for note, s in sorted(NOTES[a.machine].items()):
        if not os.path.exists(os.path.join(a.notes, f"note{note:03d}.wav")):
            continue
        t, g, ok, px = gains(rom, a.machine, note, s, a.notes, rate, frame)
        ok &= t > a.skip / 1000
        if ok.sum() < 4:
            continue
        cases.append((note, s, t[ok], g[ok], np.sqrt(px[ok])))
    holds = [a.hold / 1000] if a.hold is not None else np.arange(0.0, 0.0701, 0.005)
    v0s = [a.v0] if a.v0 is not None else np.round(np.arange(0.0, 0.401, 0.02), 3)
    best = None
    for hold in holds:
        for v0 in v0s:
            tot = sum(fit_one(t, g, w, hold, v0)[0] for _, _, t, g, w in cases)
            if best is None or tot < best[0]:
                best = (tot, hold, v0)
    _, hold, v0 = best
    print(f"hold {1000 * hold:.0f} ms, v0 {v0:.2f}; mean rms "
          f"{np.sqrt(best[0] / len(cases)):.2f} dB")
    for note, s, t, g, w in cases:
        err, tau, _ = fit_one(t, g, w, hold, v0)
        print(f"  note {note:3d} s{s:<2d}  tau {1000 * tau:5.0f} ms  rms {np.sqrt(err):4.2f} dB"
              f"  over {1000 * t[-1]:4.0f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
