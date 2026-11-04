#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Match recorded hits to ROM sounds and find each one's playback rate.
#   python match_rate.py SOUNDORDER.bin NOTESDIR [--machine 505|626]   (needs numpy)
# For every recording and ROM sound, resample the sound at a grid of rates and take the
# best normalised cross-correlation (over small lags) of the first 200 ms.
import argparse
import glob
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import tr505_rom  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("rom")
ap.add_argument("notes")
ap.add_argument("--machine", default="505")
ap.add_argument("--win", type=float, default=0.2)
a = ap.parse_args()

rom = open(a.rom, "rb").read()
layout = tr505_rom.SOUNDS if a.machine == "505" else \
    [(b + o, l) for b in (0, 0x20000) for o, l in tr505_rom.SOUNDS]
sounds = [np.frombuffer(rom[o:o + l], np.uint8).astype(float) - 128 for o, l in layout]

FS = 48000
rates = np.exp(np.linspace(np.log(8000), np.log(60000), 260))


def load(p):
    w = wave.open(p)
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(float)
    return x.mean(axis=1)


def ncc(x, y, maxlag):
    n = len(x)
    X = np.fft.rfft(x, 2 * n)
    Y = np.fft.rfft(y, 2 * n)
    c = np.fft.irfft(X * np.conj(Y))
    c = np.concatenate([c[-maxlag:], c[:maxlag + 1]])
    return np.max(np.abs(c)) / (np.linalg.norm(x) * np.linalg.norm(y) + 1e-9)


N = int(a.win * FS)
for p in sorted(glob.glob(os.path.join(a.notes, "*.wav"))):
    x = load(p)[:N]
    x = x - x.mean()
    best = []
    for k, s in enumerate(sounds):
        for f in rates:
            t = np.arange(N) * f / FS
            if t[-1] >= len(s):
                t = t[t < len(s) - 1]
            y = np.interp(t, np.arange(len(s)), s)
            y = np.pad(y, (0, N - len(y)))
            best.append((ncc(x, y, 240), k, f))
    best.sort(reverse=True)
    top = best[0]
    second = next(b for b in best if b[1] != top[1])
    print(f"{os.path.basename(p)}: sound {top[1]:2} at {top[2]:7.0f} Hz  ncc {top[0]:.3f}   "
          f"(next: sound {second[1]:2} at {second[2]:7.0f} Hz, {second[0]:.3f})", flush=True)
