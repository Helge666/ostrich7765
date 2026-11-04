#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a render of ostrich_engine_render against the TR-707 sound ROMs.

    check_tr707_render.py channels.wav IC34 IC35 IC19 IC22

channels.wav is the tool's optional ten-channel 100 kHz output. For every hit the script
finds the playback rate at which the ROM sound best matches the channel (normalised cross
correlation over the first 120 ms, rate swept +-3 % around the nominal one) and prints
rate, deviation from nominal and correlation. Needs numpy.
"""

import struct
import sys

import numpy as np

RATE = 100000
NAMES = ["bass drum 1", "bass drum 2", "snare drum 1", "snare drum 2", "low tom", "mid tom",
         "high tom", "rim shot", "cowbell", "hand clap", "tambourine", "closed hi-hat",
         "open hi-hat", "crash", "ride"]
CHANNEL = [0, 0, 1, 1, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 9]


def read_wav_float(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    pos, fmt = 12, None
    while pos < len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            assert fmt and fmt[0] == 3 and fmt[5] == 32
            return fmt[2], np.frombuffer(body, dtype="<f4").reshape(-1, fmt[1])
        pos += 8 + size + (size & 1)
    raise ValueError("no data chunk")


def schedule():
    hits, t = [], 0.25
    for s in range(15):
        gap = 2.5 if s >= 13 else 1.2 if s == 12 else 0.8
        for v in (127, 64, 24):
            hits.append((t, s, v))
            t += gap
        t += 0.4
    return hits


def rom_sound(sound, voices, crash, ride):
    """The sound's samples (centred, as floats) and its nominal rate."""
    def mux(v):
        return voices[v * 0x2000:(v + 1) * 0x2000]
    table = {
        0: (mux(0)[0::2], 25000), 1: (mux(0)[1::2], 25000),
        2: (mux(1)[0::2], 25000), 3: (mux(1)[1::2], 25000),
        4: (mux(2), 12500), 5: (mux(3), 12500), 6: (mux(4), 12500),
        7: (mux(6)[0::2], 25000), 8: (mux(6)[1::2], 25000),
        9: (mux(7)[0::2], 25000), 10: (mux(7)[1::2], 25000),
        11: (mux(5), 25000), 12: (mux(5), 25000),
        13: (crash, 25000), 14: (ride, 25000),
    }
    data, rate = table[sound]
    return np.frombuffer(bytes(data), dtype=np.uint8).astype(np.float64) - 128.0, rate


def best_match(seg, snd, nominal):
    """(|correlation|, rate, lag in samples) of the best fit; the channel may be inverted."""
    best = (-1.0, nominal, 0)
    n = len(seg)
    size = 1 << int(np.ceil(np.log2(2 * n)))
    x = np.fft.rfft(seg, size)
    for rel in np.linspace(-0.03, 0.03, 121):
        r = nominal * (1.0 + rel)
        ref = np.interp(np.arange(n) * r / RATE, np.arange(len(snd)), snd, right=0.0)
        # cc[k] = sum seg[i + k] * ref[i]: the channel lags the ROM by k (0 to 4 ms).
        cc = np.fft.irfft(x * np.conj(np.fft.rfft(ref, size)), size)[:400]
        k = int(np.argmax(np.abs(cc)))
        a, b = seg[k:], ref[:n - k]
        den = np.sqrt(np.dot(a, a) * np.dot(b, b))
        corr = abs(float(np.dot(a, b) / den)) if den > 0 else 0.0
        if corr > best[0]:
            best = (corr, r, k)
    return best


def main():
    if len(sys.argv) != 6:
        print(__doc__)
        return 2
    rate, ch = read_wav_float(sys.argv[1])
    assert rate == RATE
    roms = [open(p, "rb").read() for p in sys.argv[2:6]]
    voices, crash, ride = roms[0] + roms[1], roms[2], roms[3]
    for t, s, v in schedule():
        if v != 127:
            continue
        snd, nominal = rom_sound(s, voices, crash, ride)
        a = int(t * RATE)
        n = int(0.12 * RATE)
        seg = ch[a:a + n, CHANNEL[s]].astype(np.float64)
        corr, r, lag = best_match(seg, snd, nominal)
        print(f"{NAMES[s]:14s} nominal {nominal:6d} Hz  best {r:8.1f} Hz "
              f"({100 * (r / nominal - 1):+.2f} %)  corr {corr:.3f}  lag {lag / 100:.2f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
