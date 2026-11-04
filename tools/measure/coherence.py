#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""How far up in frequency does a recording follow the ROM sound, sample for sample?

    coherence.py 505|626 SOUND_ORDER.bin NOTES_DIR [--sounds note,...] [--variants]

For every note the ROM sound is played as the DAC holds it (response.py), its rate fitted
to the recording (the rate whose high-passed version correlates best with the recording's
over the first 150 ms) and its lag fitted to a fraction of a sample. Then the magnitude
squared coherence of recording and ROM sound per third octave (Welch, 1024 points, over
the sound, at most 0.3 s). A linear analogue path, however it filters, keeps the
coherence near 1 wherever the recording is above its noise; where the coherence falls
while the recording still has energy, the recording is not the ROM data as we read it.

--variants also tries the sound with the low bits of the sample index permuted (or
inverted) inside the sound and prints the variants that fit the recording above 2 kHz
better than the plain one. Needs numpy.
"""

import argparse
import itertools
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from response import NOTES, REGIONS, SR, bands, onset, read, sound  # noqa: E402


def held_fast(snd, rate, n_out, t0=0.0):
    """The held DAC output band-limited to SR / 2, sampled at SR from time t0 (in output
    samples). Exact: the hold's spectrum per sample is computed in the frequency domain."""
    # Upsample by an integer factor, hold, brick-wall, decimate (as response.held).
    up = 25
    fine = SR * up
    tt = (np.arange(n_out * up) + t0 * up) * rate / fine
    idx = np.floor(tt).astype(np.int64)
    x = np.where((idx >= 0) & (idx < len(snd)), snd[np.clip(idx, 0, len(snd) - 1)], 0.0)
    spec = np.fft.rfft(x)
    spec[int(len(x) * 0.5 / up):] = 0.0
    return np.fft.irfft(spec, len(x))[::up]


def highpass(x, f=2000.0):
    spec = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(len(x), 1.0 / SR)
    spec[freqs < f] = 0.0
    return np.fft.irfft(spec, len(x))


def ncorr(a, b):
    return float(np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-30))


def fit(snd, y, on, n, rate0, span=40):
    """Best rate and lag (in output samples, fractional) for snd against y[on:]."""
    m = min(n, int(0.15 * SR))
    seg = y[on - 96:on - 96 + m + 192]
    hseg = highpass(seg)
    best = (-2.0, rate0, 0.0)
    # Integer lag first, at the nominal rate.
    x = highpass(held_fast(snd, rate0, m))
    c = np.correlate(hseg, x, mode="valid")
    lag = int(np.argmax(np.abs(c)))
    for rate in rate0 * (1 + np.arange(-span, span + 1) * 0.0005):
        x = highpass(held_fast(snd, rate, m))
        c = np.correlate(hseg, x, mode="valid")
        k = int(np.argmax(np.abs(c)))
        v = abs(c[k]) / np.sqrt(np.dot(x, x) * np.dot(hseg[k:k + m], hseg[k:k + m]) + 1e-30)
        if v > best[0]:
            best, lag = (v, rate, 0.0), k
    # Refine rate and fractional lag.
    v0, rate, _ = best
    for step in (0.0001, 0.00002, 0.000005):
        for frac in np.arange(-0.5, 0.51, 0.1):
            for r in rate * (1 + np.arange(-5, 6) * step):
                x = highpass(held_fast(snd, r, m, -frac))
                v = abs(ncorr(hseg[lag:lag + m], x))
                if v > best[0]:
                    best = (v, r, frac)
        rate = best[1]
    return best[0], best[1], on - 96 + lag, best[2]


def coherence(x, y, nfft=1024):
    win = np.hanning(nfft)
    hop = nfft // 4
    sxy = sxx = syy = 0.0
    for i in range(0, len(x) - nfft + 1, hop):
        fx = np.fft.rfft(x[i:i + nfft] * win)
        fy = np.fft.rfft(y[i:i + nfft] * win)
        sxy = sxy + fx * np.conj(fy)
        sxx = sxx + np.abs(fx) ** 2
        syy = syy + np.abs(fy) ** 2
    freqs = np.fft.rfftfreq(nfft, 1.0 / SR)
    return freqs, sxy, sxx, syy


def band_coh(freqs, sxy, sxx, syy):
    out = []
    for f in bands():
        sel = (freqs >= f / 2 ** (1 / 6)) & (freqs < f * 2 ** (1 / 6))
        if not sel.any():
            out.append(np.nan)
            continue
        out.append(np.abs(sxy[sel].sum()) ** 2 / (sxx[sel].sum() * syy[sel].sum() + 1e-30))
    return np.array(out)


def permute(snd, perm, inv):
    """The sound with sample index bits b -> perm[b] (b < len(perm)) and bits in inv
    inverted, over its whole length (a power of two)."""
    n = len(snd)
    i = np.arange(n)
    j = i & ~((1 << len(perm)) - 1)
    for b, p in enumerate(perm):
        j |= ((i >> b) & 1) << p
    j ^= inv
    return snd[j]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("machine", choices=("505", "626"))
    ap.add_argument("rom")
    ap.add_argument("notes")
    ap.add_argument("--sounds", default=None)
    ap.add_argument("--variants", action="store_true")
    a = ap.parse_args()
    rate0 = 24955.0 if a.machine == "505" else 25125.0
    rom = np.frombuffer(open(a.rom, "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    notes = NOTES[a.machine]
    if a.sounds:
        notes = {int(k): notes[int(k)] for k in a.sounds.split(",")}
    fb = bands()
    print("Hz             " + " ".join(f"{f:5.0f}" for f in fb))
    for note, s in sorted(notes.items()):
        path = os.path.join(a.notes, f"note{note:03d}.wav")
        if not os.path.exists(path):
            continue
        yy = read(path)
        y = yy.sum(axis=1)
        # Room before the onset for the lag search (a recording may start right at it).
        y = np.concatenate([np.zeros(400), y])
        on = onset(yy) + 400
        off, ln = REGIONS[a.machine][s]
        snd = sound(rom, a.machine, s)
        n = min(int(ln / rate0 * SR), int(0.3 * SR), len(y) - on - 200)
        v, rate, start, frac = fit(snd, y, on, n, rate0)
        x = held_fast(snd, rate, n, -frac)
        ys = y[start:start + n]
        c = band_coh(*coherence(x, ys))
        print(f"n{note:<3d}s{s:<2d} {rate:7.0f} " + " ".join(
            "    ." if np.isnan(q) else f"{q:5.2f}" for q in c) + f"   r={v:.2f}")
        if not a.variants:
            continue
        hi = fb >= 2000
        base = np.nanmean(c[hi])
        found = []
        for k in (2, 3):
            for perm in itertools.permutations(range(k)):
                for inv in range(1 << k):
                    if perm == tuple(range(k)) and inv == 0:
                        continue
                    sv = permute(snd, perm, inv)
                    vv, rr, st, fr = fit(sv, y, on, n, rate, span=1)
                    cv = band_coh(*coherence(held_fast(sv, rr, n, -fr), y[st:st + n]))
                    found.append((np.nanmean(cv[hi]), perm, inv, vv))
        found.sort(key=lambda t: -t[0])
        print(f"   plain: coherence above 2 kHz {base:.2f}; best variants:")
        for q, perm, inv, vv in found[:5]:
            print(f"   bits {perm} inverted {inv:#x}: {q:.2f}  r={vv:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
