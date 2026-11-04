#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
The TR-707's sound ROMs: split into its sounds as the voice board plays them, report where
each sound ends, and optionally write each sound as a WAV at its native rate (for
listening -- the WAVs are Roland's data and stay outside the repository).

    tr707_rom.py IC34 IC35 IC19 IC22 [--wav OUTDIR]

Layout (analysis/TR-707.md): IC34/IC35 hold four 8 KiB voice slots each, read by the
MB63H114's counters 0-7; slots 0, 1, 6, 7 interleave two sounds (even/odd bytes). IC19
(crash) and IC22 (ride) are a 32 KiB sound each, 6 bits (D7-D2) used. Counter clocks:
50 kHz (BD, SD, RS/CB, HCP/TAMB: 25 kHz per sound), 25 kHz (HH), 12.5 kHz (toms);
the cymbals 25 kHz.
"""

import argparse
import hashlib
import os
import struct
import sys

# (name, rom index 0=IC34 1=IC35, slot 0..3 in that ROM, interleave phase or None, rate)
MUX_SOUNDS = [
    ("BD1",  0, 0, 0, 25000), ("BD2",  0, 0, 1, 25000),
    ("SD1",  0, 1, 0, 25000), ("SD2",  0, 1, 1, 25000),
    ("LT",   0, 2, None, 12500), ("MT", 0, 3, None, 12500),
    ("HT",   1, 0, None, 12500), ("HH", 1, 1, None, 25000),
    ("RS",   1, 2, 0, 25000), ("CB",   1, 2, 1, 25000),
    ("HCP",  1, 3, 0, 25000), ("TAMB", 1, 3, 1, 25000),
]


def end_of_sound(samples, centre, tol=1):
    """Index after the last sample that differs from the centre by more than tol."""
    for i in range(len(samples) - 1, -1, -1):
        if abs(samples[i] - centre) > tol:
            return i + 1
    return 0


def write_wav(path, samples_u8, rate, bits=8):
    # 8-bit WAV is unsigned, as the ROM data; 6-bit cymbals are scaled up to 8 bits.
    data = bytes(samples_u8)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ic34")
    ap.add_argument("ic35")
    ap.add_argument("ic19")
    ap.add_argument("ic22")
    ap.add_argument("--wav", help="folder for one WAV per sound (outside the repository)")
    a = ap.parse_args()

    roms = []
    for p in (a.ic34, a.ic35, a.ic19, a.ic22):
        b = open(p, "rb").read()
        if len(b) != 0x8000:
            sys.exit(f"{p}: {len(b)} bytes, expected 32768")
        roms.append(b)
        print(f"{os.path.basename(p)}: sha1 {hashlib.sha1(b).hexdigest()}")

    if a.wav:
        os.makedirs(a.wav, exist_ok=True)

    print(f"\n{'sound':6} {'rate':>6} {'bytes':>6} {'used':>6} {'ms':>7} {'min':>4} {'max':>4} {'first':>5}")
    for name, r, slot, phase, rate in MUX_SOUNDS:
        s = roms[r][slot * 0x2000:(slot + 1) * 0x2000]
        if phase is not None:
            s = s[phase::2]
        used = end_of_sound(s, 0x80)
        print(f"{name:6} {rate:6} {len(s):6} {used:6} {1000 * used / rate:7.1f} {min(s):4} {max(s):4} {s[0]:5}")
        if a.wav:
            write_wav(os.path.join(a.wav, f"tr707_{name}.wav"), s, rate)

    for name, r in (("CRASH", 2), ("RIDE", 3)):
        s6 = [b >> 2 for b in roms[r]]                 # D7..D2 to the 6-bit DAC
        used = end_of_sound(s6, 0x20)
        print(f"{name:6} {25000:6} {len(s6):6} {used:6} {1000 * used / 25000:7.1f} {min(s6):4} {max(s6):4} {s6[0]:5}  (6-bit)")
        low = sum(1 for b in roms[r] if b & 3)
        print(f"{'':6} bytes with D1/D0 set: {low}")
        if a.wav:
            write_wav(os.path.join(a.wav, f"tr707_{name}.wav"), [v << 2 for v in s6], 25000)


if __name__ == "__main__":
    main()
