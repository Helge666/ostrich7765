#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The frequency response of a unit's output stage, from recordings of single notes.

    response.py 505|626 SOUND_ORDER.bin NOTES_DIR [--rate HZ] [--sounds note,note,...]

NOTES_DIR holds noteNNN.wav (notescan.py / scan_eval.py: one note each, 48 kHz stereo).
For every note the ROM sound is played as the DAC does (each sample held, at --rate, the
unit's rate as measured against the recorder's clock), band-limited to the recording's
rate, and its power spectrum compared with the recording's (Welch, 2048-point Hann, from
the onset over the sound's length, at most 0.4 s; the recording's noise, from its quiet
tail, subtracted). Prints |H| in dB per third octave, relative to 1 kHz, per sound and as
the median over all sounds. The DAC's hold (sin x / x) is in both, so |H| is what comes
after the DAC: the analogue filters. Needs numpy.
"""

import argparse
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

NOTES = {
    "505": {35: 4, 37: 10, 38: 6, 39: 8, 41: 1, 42: 15, 45: 3, 46: 14, 48: 2, 49: 12, 51: 13,
            62: 7, 63: 5, 65: 0, 67: 11, 68: 9},
    "626": {35: 9, 36: 24, 37: 8, 38: 13, 39: 4, 40: 14, 41: 1, 42: 25, 43: 16, 45: 3, 46: 10,
            47: 18, 48: 2, 49: 11, 50: 17, 51: 12, 52: 26, 53: 27, 54: 7, 56: 5, 58: 23,
            62: 19, 63: 0, 64: 15, 65: 29, 66: 28, 67: 22, 68: 20, 69: 21, 75: 6},
}
BANK = [(s * 0x2000, 0x2000) for s in range(4)] + \
       [(s * 0x2000 + h * 0x1000, 0x1000) for s in range(4, 7) for h in range(2)]
REGIONS = {
    "505": BANK + [(0xE000, 0x1000), (0xF000, 0x1000), (0x10000, 0x8000), (0x18000, 0x4000),
                   (0x1C000, 0x2000), (0x1E000, 0x2000)],
    "626": [(b + o, n) for b in (0, 0x20000) for o, n in
            BANK + [(0xE000, 0x2000), (0x10000, 0x8000), (0x18000, 0x4000), (0x1C000, 0x2000),
                    (0x1E000, 0x2000)]],
}
# The unit reads sample n of a sound at n ^ FLIP (PcmMachine.cpp, analysis/TR-505.md).
FLIP = {"505": {12: 7, 13: 3, 14: 1, 15: 1}, "626": {}}
SR = 48000
NFFT = 2048


def sound(rom, machine, s):
    """Sound s of the ROM in sound order, its samples in the order the unit plays them."""
    off, ln = REGIONS[machine][s]
    return rom[off + (np.arange(ln) ^ FLIP[machine].get(s, 0))]


def read(path):
    w = wave.open(path)
    assert w.getframerate() == SR and w.getsampwidth() == 2
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels())
    return x.astype(np.float64) / 32768.0


def held(snd, rate, n_out):
    """The DAC's output (each sample held at `rate`), band-limited to SR, n_out samples."""
    up = 25                                       # 1.2 MHz grid
    fine = SR * up
    idx = np.floor(np.arange(n_out * up) * rate / fine).astype(np.int64)
    x = np.where(idx < len(snd), snd[np.minimum(idx, len(snd) - 1)], 0.0)
    spec = np.fft.rfft(x)
    spec[int(len(x) * 0.5 / up):] = 0.0           # brick wall at SR / 2
    return np.fft.irfft(spec, len(x))[::up]


def welch(x):
    win = np.hanning(NFFT)
    hop = NFFT // 4
    frames = [x[i:i + NFFT] * win for i in range(0, len(x) - NFFT + 1, hop)]
    return np.mean([np.abs(np.fft.rfft(f)) ** 2 for f in frames], axis=0)


def onset(y):
    a = np.abs(y).max(axis=1)
    return int(np.argmax(a > 0.05 * a.max()))


def bands():
    f = 1000.0 * 2.0 ** (np.arange(-15, 14) / 3.0)   # 31.5 Hz .. 20 kHz
    return f[(f > 30) & (f < 23000)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("machine", choices=("505", "626"))
    ap.add_argument("rom")
    ap.add_argument("notes")
    ap.add_argument("--rate", type=float, default=None)
    ap.add_argument("--sounds", default=None)
    ap.add_argument("--json", default=None, help="write the per-sound band gains here")
    a = ap.parse_args()
    rate = a.rate or (24955.0 if a.machine == "505" else 25125.0)
    rom = np.frombuffer(open(a.rom, "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    notes = NOTES[a.machine]
    if a.sounds:
        notes = {int(n): notes[int(n)] for n in a.sounds.split(",")}
    fb = bands()
    rows = []
    for note, s in sorted(notes.items()):
        path = os.path.join(a.notes, f"note{note:03d}.wav")
        if not os.path.exists(path):
            continue
        y = read(path)
        on = onset(y)
        off, ln = REGIONS[a.machine][s]
        snd = sound(rom, a.machine, s)
        n = min(int(ln / rate * SR) + 480, len(y) - on - 48 - int(0.2 * SR))
        x = held(snd, rate, n)
        start = max(0, on - 48)
        ys = y[start:start + n].sum(axis=1)
        tail = y[-int(0.15 * SR):].sum(axis=1)
        # One window over the whole sound, tapered only at its end (the attack stays).
        nfft = 1 << int(np.ceil(np.log2(4 * n)))
        taper = np.ones(n)
        m = n // 8
        taper[-m:] = 0.5 * (1 + np.cos(np.pi * np.arange(m) / m))
        freqs = np.fft.rfftfreq(nfft, 1.0 / SR)
        pxx = np.abs(np.fft.rfft(x * taper, nfft)) ** 2
        pyy = np.abs(np.fft.rfft(ys * taper, nfft)) ** 2
        # The noise's power over as many samples, from the quiet tail.
        pnn = np.mean(tail ** 2) * np.sum(taper ** 2) / (SR / 2) * (SR / nfft) * nfft / 2
        hb = []
        for f in fb:
            sel = (freqs >= f / 2 ** (1 / 6)) & (freqs < f * 2 ** (1 / 6))
            sx, sy, sn = pxx[sel].sum(), pyy[sel].sum(), pnn * sel.sum()
            ok = sx > pxx.max() * 1e-5 * sel.sum() and sy > 4 * sn
            hb.append(10 * np.log10((sy - sn) / sx) if ok else np.nan)
        rows.append((note, s, np.array(hb)))
    # One scale for all sounds: the median over the sounds at 1 kHz is 0 dB, so the levels
    # of the sounds relative to each other stay.
    k = np.argmin(np.abs(fb - 1000))
    ref = np.nanmedian([r[2][k] for r in rows])
    print("Hz       " + " ".join(f"{f:6.0f}" for f in fb))
    for note, s, hb in rows:
        print(f"n{note:<3d}s{s:<2d}   " + " ".join("     ." if np.isnan(v) else f"{v - ref:6.1f}" for v in hb))
    med = np.nanmedian(np.array([r[2] - r[2][k] for r in rows]), axis=0)
    print("shape    " + " ".join(f"{v:6.1f}" for v in med) + "   (median, each sound 0 dB at 1 kHz)")
    if a.json:
        import json
        json.dump({"machine": a.machine, "rate": rate, "bands": list(fb),
                   "sounds": {str(s): [None if np.isnan(v) else round(float(v - ref), 2) for v in hb]
                              for note, s, hb in rows}}, open(a.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
