#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A/B listening file: the real unit, the plain ROM sound, the ROM sound with the response
measured by response.py -- note by note.

    ab_render.py 505|626 SOUND_ORDER.bin NOTES_DIR RESPONSE.json OUT.wav [--notes 35,38,...]

For every note: the recording (noteNNN.wav), then the ROM sound as the DAC holds it (as the
engine plays it now), then the same with the measured band gains applied (smoothed over
the bands, zero phase). Both ROM versions are scaled so that their 1 kHz band matches the
recording's. Bands below 125 Hz, where the ROM sounds have little energy, take the gain of
the lowest band above. OUT.wav is the owner's unit's sound and Roland's data: keep it out
of the repository. Needs numpy.
"""

import argparse
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from response import NOTES, REGIONS, SR, held, onset, read, sound  # noqa: E402


def curve(bands, gains, freqs):
    """Band gains (dB, None where unknown) as a gain per FFT bin, interpolated over log f."""
    fb = np.array(bands)
    g = np.array([np.nan if v is None else v for v in gains], dtype=float)
    g[fb < 125] = np.nan
    ok = ~np.isnan(g)
    if ok.sum() < 2:
        return np.ones_like(freqs)
    lf = np.log(np.maximum(freqs, 1.0))
    db = np.interp(lf, np.log(fb[ok]), g[ok])          # holds the end values outside
    return 10 ** (db / 20)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("machine", choices=("505", "626"))
    ap.add_argument("rom")
    ap.add_argument("notes")
    ap.add_argument("response")
    ap.add_argument("out")
    ap.add_argument("--notes-only", dest="only", default=None)
    a = ap.parse_args()
    resp = json.load(open(a.response))
    rate = resp["rate"]
    bands = resp["bands"]
    rom = np.frombuffer(open(a.rom, "rb").read(), dtype=np.uint8).astype(np.float64) - 128.0
    notes = NOTES[a.machine]
    if a.only:
        notes = {int(n): notes[int(n)] for n in a.only.split(",")}
    k1 = int(np.argmin(np.abs(np.array(bands) - 1000)))
    gap = np.zeros(int(0.25 * SR))
    out = []
    for note, s in sorted(notes.items()):
        path = os.path.join(a.notes, f"note{note:03d}.wav")
        if not os.path.exists(path) or str(s) not in resp["sounds"]:
            continue
        gains = resp["sounds"][str(s)]
        y = read(path).mean(axis=1)
        on = onset(read(path))
        off, ln = REGIONS[a.machine][s]
        n = int(ln / rate * SR) + int(0.1 * SR)
        rec = y[max(0, on - 48):max(0, on - 48) + n]
        x = held(sound(rom, a.machine, s), rate, n)
        nfft = 1 << int(np.ceil(np.log2(2 * n)))
        freqs = np.fft.rfftfreq(nfft, 1.0 / SR)
        # Scale: the recording's 1 kHz band over the plain sound's.
        g1 = gains[k1] if gains[k1] is not None else np.nanmedian([v for v in gains if v is not None])
        plain = x * 10 ** (g1 / 20)
        eq = np.fft.irfft(np.fft.rfft(x, nfft) * curve(bands, gains, freqs), nfft)[:n]
        # Each version's level: the 1 kHz band's absolute gain is in the curve (from the
        # recording against the ROM, one scale for all sounds); match the recording's RMS so
        # that the comparison is of sound, not of loudness.
        def match(v):
            return v * np.sqrt(np.mean(rec ** 2) / max(np.mean(v ** 2), 1e-20))
        out += [rec, gap, match(plain), gap, match(eq), gap, gap, gap]
        print(f"note {note}: real, plain, measured response")
    sig = np.concatenate(out)
    sig *= 0.89 / np.abs(sig).max()
    pcm = (np.clip(sig, -1, 1) * 32767).astype("<i2")
    w = wave.open(a.out, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(pcm.tobytes())
    w.close()
    print(f"wrote {a.out}, {len(sig) / SR:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
