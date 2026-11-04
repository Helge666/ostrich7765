#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
The TR-505's sound ROM (TC531000P, 128 KiB): recognise any of the three known forms of the
dump, bring it into sound order, split it into its 16 sounds, and optionally write each as
a WAV (for listening -- the WAVs are Roland's data and stay outside the repository).

    tr505_rom.py DUMP [--wav OUTDIR] [--rate HZ] [--canonical OUT.bin]

The three forms (analysis/TR-505.md):

  raw         the chip as read (MAME's tr-505_rawromdump.bin), address and data lines in
              the chip's own order
  pin order   "TR-505 Firmware.bin" (despite its name, not firmware): the raw dump with its
              address and data lines put in order
  sound order "TR-505_RawRomDump_Decoded.BIN": every sound contiguous -- the canonical form
              Ostrich 7765 works with
"""

import argparse
import hashlib
import os
import struct
import sys

SIZE = 0x20000

# sha1 of each known form
SHA1_RAW = "6441d3e7b53aff4511b23021dc854b7a5cc57689"
SHA1_PIN = "14e80b926aed1e78cb027752dfbbe51d9b3dcf4d"
SHA1_SOUND = "70f622f07fd94bcbfdb731f303ab4e8df845469f"

# raw -> pin order.  Data: pin-order bit i = raw bit DATA_BITS[i].  Address: pin-order
# address bit i = raw address bit ADDR_BITS[i].
DATA_BITS = (7, 6, 5, 0, 1, 2, 4, 3)
ADDR_BITS = (0, 12, 7, 6, 9, 11, 10, 2, 3, 4, 8, 5, 1, 13, 14, 15, 16)

SLOT = 0x2000

# The 16 sounds in sound order: (offset, length).  Names follow once identified by ear and
# against the real unit (ROADMAP.md, phase 1).
SOUNDS = (
    [(s * SLOT, SLOT) for s in range(4)]                                  # slots 0-3
    + [(s * SLOT + h * 0x1000, 0x1000) for s in range(4, 8) for h in range(2)]  # slots 4-7, two each
    + [(0x10000, 0x8000), (0x18000, 0x4000), (0x1C000, SLOT), (0x1E000, SLOT)]
)


def permute_bits(v, bits):
    return sum(1 << i for i, b in enumerate(bits) if v >> b & 1)


def raw_to_pin(raw):
    lut = [permute_bits(v, DATA_BITS) for v in range(256)]
    # pin-order address bit i is raw address bit ADDR_BITS[i]
    raw_addr = lambda a: sum(1 << b for i, b in enumerate(ADDR_BITS) if a >> i & 1)
    return bytes(lut[raw[raw_addr(a)]] for a in range(SIZE))


def pin_to_sound(pin, flip_a0=True):
    """The TR-505's slot layout.  The TR-626 has it twice (tr626_rom.py), once its dump's
    A6/A8 swap is undone, and needs no A0 flip in slots 0-3."""
    out = bytearray(SIZE)
    for a in range(SIZE):
        if a < 0x8000:
            # slots 0-3: one sound each; in the TR-505's dump A0 the other way round
            src = a ^ 1 if flip_a0 else a
        elif a < 0x10000:
            # slots 4-7: two sounds each, interleaved (even and odd bytes)
            base, k = a & ~(SLOT - 1), a & (SLOT - 1)
            src = base + (2 * k if k < 0x1000 else 2 * (k - 0x1000) + 1)
        elif a < 0x18000:
            # slots 8-11: one sound across four slots, the slot bits fastest
            k = a - 0x10000
            src = 0x10000 + ((k & 1) << 13 | (k >> 1 & 1) << 14 | k >> 2)
        elif a < 0x1C000:
            # slots 12-13: one sound across two slots, the slot bit fastest
            k = a - 0x18000
            src = 0x18000 + ((k & 1) << 13 | k >> 1)
        else:
            src = a                                    # slots 14, 15: one sound each
        out[a] = pin[src]
    return bytes(out)


def to_sound_order(data):
    """(sound-order bytes, form name) for any known form, or (None, reason)."""
    if len(data) != SIZE:
        return None, f"{len(data)} bytes, expected {SIZE}"
    h = hashlib.sha1(data).hexdigest()
    if h == SHA1_SOUND:
        return data, "sound order"
    if h == SHA1_PIN:
        return pin_to_sound(data), "pin order"
    if h == SHA1_RAW:
        return pin_to_sound(raw_to_pin(data)), "raw"
    return None, f"unknown dump (sha1 {h})"


def end_of_sound(s, tol=1):
    for i in range(len(s) - 1, -1, -1):
        if abs(s[i] - 0x80) > tol:
            return i + 1
    return 0


def write_wav(path, data, rate):
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--wav", help="folder for one WAV per sound (outside the repository)")
    ap.add_argument("--rate", type=int, default=25000, help="WAV rate (the real rates: phase 1)")
    ap.add_argument("--canonical", help="write the sound-order image here")
    a = ap.parse_args()

    data = open(a.dump, "rb").read()
    snd, form = to_sound_order(data)
    if snd is None:
        sys.exit(f"{a.dump}: {form}")
    ok = hashlib.sha1(snd).hexdigest() == SHA1_SOUND
    print(f"{a.dump}: {form} -> sound order, sha1 {'matches' if ok else 'DIFFERS'}")
    if a.canonical:
        open(a.canonical, "wb").write(snd)
    if a.wav:
        os.makedirs(a.wav, exist_ok=True)
    print(f"{'#':>2} {'offset':>7} {'bytes':>6} {'used':>6} {'min':>4} {'max':>4}")
    for n, (off, ln) in enumerate(SOUNDS):
        s = snd[off:off + ln]
        print(f"{n:2} {off:#07x} {ln:6} {end_of_sound(s):6} {min(s):4} {max(s):4}")
        if a.wav:
            write_wav(os.path.join(a.wav, f"tr505_{n:02}.wav"), s, a.rate)


if __name__ == "__main__":
    main()
