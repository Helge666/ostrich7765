#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The TR-626's pitch steps from one recording: hits of one sound after another at the
# pitch settings given (default -7 -4 -1 0 +1 +4 +7), sound after sound.
#   python pitch_steps.py SOUNDORDER.bin REC.wav [--steps -7,-4,-1,0,1,4,7] (needs numpy)
# Finds the hits, matches the first sound group's step-0 hit to the ROM sounds, then fits
# every hit's playback rate (resampled ROM sound, best normalised cross-correlation over
# the first 150 ms) and prints each rate against step 0 in semitones.
import argparse
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import tr626_rom  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("rom")
ap.add_argument("rec")
ap.add_argument("--steps", default="-7,-4,-1,0,1,4,7")
ap.add_argument("--win", type=float, default=0.15)
a = ap.parse_args()
steps = [int(s) for s in a.steps.split(",")]

rom = open(a.rom, "rb").read()
sounds = [np.frombuffer(rom[o:o + l], np.uint8).astype(float) - 128 for o, l in tr626_rom.SOUNDS]


def load(p):
    w = wave.open(p)
    n, ch, width, fs = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    if width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(float)
    else:
        x = np.frombuffer(raw, {2: np.int16, 4: np.int32}[width]).astype(float)
    return x.reshape(-1, ch).mean(axis=1), fs


x, FS = load(a.rec)
env = np.convolve(np.abs(x), np.ones(int(0.002 * FS)) / int(0.002 * FS), "same")
thr = 0.08 * env.max()
above = np.flatnonzero(env > thr)
onsets = [above[0]]
for i in above[1:]:
    if i - onsets[-1] > int(0.5 * FS) and env[i - int(0.3 * FS):i].max() < thr:
        onsets.append(i)
# Back to the true start: the first sample above 2 % before the threshold crossing.
onsets = [o - int(np.argmax(env[o - int(0.01 * FS):o + 1][::-1] < 0.02 * env.max())) for o in onsets]
print(f"{len(onsets)} hits at", " ".join(f"{o / FS:.2f}" for o in onsets))
if len(onsets) % len(steps):
    sys.exit(f"expected a multiple of {len(steps)} hits")

N = int(a.win * FS)


def ncc(x, y, maxlag):
    n = len(x)
    X = np.fft.rfft(x, 2 * n)
    Y = np.fft.rfft(y, 2 * n)
    c = np.fft.irfft(X * np.conj(Y))
    c = np.concatenate([c[-maxlag:], c[:maxlag + 1]])
    return np.max(np.abs(c)) / (np.linalg.norm(x) * np.linalg.norm(y) + 1e-9)


def fit(hit, s, rates):
    best = (0, 0)
    for f in rates:
        t = np.arange(N) * f / FS
        t = t[t < len(s) - 1]
        y = np.pad(np.interp(t, np.arange(len(s)), s), (0, N - len(t)))
        best = max(best, (ncc(hit, y, int(0.005 * FS)), f))
    return best


def cut(o):
    h = x[max(0, o - int(0.002 * FS)):][:N]
    h = np.pad(h, (0, N - len(h)))
    return h - h.mean()


for g in range(len(onsets) // len(steps)):
    hits = [cut(o) for o in onsets[g * len(steps):(g + 1) * len(steps)]]
    ref = hits[steps.index(0)] if 0 in steps else hits[len(steps) // 2]
    coarse = np.exp(np.linspace(np.log(15000), np.log(40000), 60))
    sound = max(range(len(sounds)), key=lambda k: fit(ref, sounds[k], coarse)[0])
    print(f"group {g + 1}: ROM sound {sound}")
    rates = []
    for st, h in zip(steps, hits):
        c0, f0 = fit(h, sounds[sound], np.exp(np.linspace(np.log(8000), np.log(80000), 400)))
        c, f = fit(h, sounds[sound], f0 * np.exp(np.linspace(-0.006, 0.006, 61)))
        rates.append((st, f, c))
    r0 = next(f for st, f, c in rates if st == 0)
    for st, f, c in rates:
        semis = 12 * np.log2(f / r0)
        per = semis / st if st else 0.0
        print(f"  step {st:+d}: {f:8.0f} Hz  ncc {c:.3f}  {semis:+6.2f} semitones"
              + (f"  ({per:.3f} per step)" if st else ""))
