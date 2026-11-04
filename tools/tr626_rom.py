#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
The TR-626's sound ROM (256 KiB): bring the known dump into sound order, split it into its
30 sounds, and optionally write each as a WAV (for listening -- the WAVs are Roland's data
and stay outside the repository).

    tr626_rom.py DUMP [--wav OUTDIR] [--rate HZ] [--canonical OUT.bin]

The dump has address lines A6 and A8 swapped (64-byte blocks out of order within every
512 bytes: the samples jump at exactly those block edges, and nowhere else once swapped).
With that undone it is two 128 KiB banks, each laid out almost as the TR-505's ROM in pin
order (analysis/TR-626.md): slots 0-3 one sound each, 4-6 two interleaved sounds each,
slot 7 one sound (the hi-hats; on the TR-505 two), 8-11 one sound across four slots, 12-13
one across two, 14 and 15 one each: 15 sounds per bank, 30 in all, each matched to one
of the 30 notes the real unit plays.
"""

import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tr505_rom  # noqa: E402

SIZE = 0x40000
BANK = 0x20000
SHA1_DUMP = "d7891edecc68a0652d2cad82a637430ee8568818"


# The sounds of one bank in sound order: (offset, length).
BANK_SOUNDS = (
    [(s * 0x2000, 0x2000) for s in range(4)]
    + [(s * 0x2000 + h * 0x1000, 0x1000) for s in range(4, 7) for h in range(2)]
    + [(0xE000, 0x2000), (0x10000, 0x8000), (0x18000, 0x4000), (0x1C000, 0x2000),
       (0x1E000, 0x2000)]
)
SOUNDS = [(b + o, n) for b in (0, BANK) for o, n in BANK_SOUNDS]


def bank_to_sound(pin):
    out = bytearray(tr505_rom.pin_to_sound(pin, flip_a0=False))
    out[0xE000:0x10000] = pin[0xE000:0x10000]          # slot 7: one sound, not two
    return bytes(out)


def to_sound_order(data):
    if len(data) != SIZE:
        return None, f"{len(data)} bytes, expected {SIZE}"
    h = hashlib.sha1(data).hexdigest()
    if h != SHA1_DUMP:
        return None, f"unknown dump (sha1 {h})"
    data = bytes(data[a & ~0x140 | (a >> 8 & 1) << 6 | (a >> 6 & 1) << 8] for a in range(SIZE))
    out = b"".join(bank_to_sound(data[b:b + BANK]) for b in (0, BANK))
    return out, "dump"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--wav", help="folder for one WAV per sound (outside the repository)")
    ap.add_argument("--rate", type=int, default=25000, help="WAV rate (the real rates: phase 1)")
    ap.add_argument("--canonical", help="write the sound-order image here")
    a = ap.parse_args()

    snd, form = to_sound_order(open(a.dump, "rb").read())
    if snd is None:
        sys.exit(f"{a.dump}: {form}")
    print(f"{a.dump}: {form} -> sound order, sha1 {hashlib.sha1(snd).hexdigest()}")
    if a.canonical:
        open(a.canonical, "wb").write(snd)
    if a.wav:
        os.makedirs(a.wav, exist_ok=True)
    print(f"{'#':>2} {'offset':>7} {'bytes':>6} {'used':>6} {'min':>4} {'max':>4}")
    for n, (off, ln) in enumerate(SOUNDS):
        s = snd[off:off + ln]
        print(f"{n:2} {off:#07x} {ln:6} {tr505_rom.end_of_sound(s):6} {min(s):4} {max(s):4}")
        if a.wav:
            tr505_rom.write_wav(os.path.join(a.wav, f"tr626_{n:02}.wav"), s, a.rate)


if __name__ == "__main__":
    main()
