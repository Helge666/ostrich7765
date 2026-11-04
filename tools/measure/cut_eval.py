#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which sounds cut each other off: a pair scan (pairscan.py) against single notes.

    cut_eval.py PAIRS.wav NOTES_DIR [--json OUT]

For every pair, first note A then B (--delay later): A's single recording (NOTES_DIR,
noteNNN.wav, from its onset) is lined up with the scan over A's first 25 ms (lag of the
largest cross-correlation, gain by least squares), likewise B's on what is left. Then
over 120 ms from B's onset the scan is fitted as alpha * A + beta * B: alpha near 1 means A
plays on, near 0 that B has cut it off. Prints alpha per pair, the pairs with alpha under
0.3, and the groups they form (connected pairs). Needs numpy.
"""

import argparse
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from response import onset  # noqa: E402

SR = 48000


def rd(path):
    w = wave.open(path)
    return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2) / 32768.0


def align(x, ref, centre, search, length):
    """Lag (sample of ref[0] in x) near centre with the largest correlation over length."""
    r = ref[:length].ravel()
    best = None
    for p in range(centre - search, centre + search + 1):
        seg = x[p:p + length].ravel()
        c = np.dot(seg, r)
        if best is None or c > best[0]:
            best = (c, p)
    p = best[1]
    seg = x[p:p + length].ravel()
    return p, float(np.dot(seg, r) / np.dot(r, r))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pairs")
    ap.add_argument("notes")
    ap.add_argument("--json")
    ap.add_argument("--threshold", type=float, default=0.3)
    a = ap.parse_args()
    x = rd(a.pairs)
    log = json.load(open(os.path.splitext(a.pairs)[0] + ".json"))
    hits, delay = log["hits"], log["delay"]
    env = np.abs(x).max(axis=1)
    first = int(np.argmax(env > 0.02))
    last = len(env) - 1 - int(np.argmax(env[::-1] > 0.02))
    # The last sync note's onset, from the end of its sound backwards: its own onset.
    lseg = x[last - SR // 2:last + 1]
    lon = last - SR // 2 + onset(lseg)
    scale = (lon - first) / (hits[-1][0] * SR)
    print(f"sync: {first / SR:.3f} s; clock {1e6 * (scale - 1):+.0f} ppm")

    def at(t):
        return first + int(round(t * SR * scale))

    singles = {}
    for n in {n for _, n, _ in hits[1:-1]}:
        y = rd(os.path.join(a.notes, f"note{n:03d}.wav"))
        singles[n] = y[max(0, onset(y) - 48):]
    w25, w120 = int(0.025 * SR), int(0.12 * SR)
    res = {}
    k = 1
    for p, q in log["pairs"]:
        (tp, np_, _), (tq, nq, _) = hits[k], hits[k + 1]
        k += 2
        A, B = singles[np_], singles[nq]
        pa, ga = align(x, A, at(tp) - 48, int(0.008 * SR), min(w25, int(delay * SR) - 48))
        n = min(len(A), len(B) + int(delay * SR) + w120, int(0.6 * SR))
        rest = x[pa:pa + n] - ga * A[:n]
        pb, gb = align(rest, B, int(delay * SR) - 48, int(0.008 * SR), w25)
        lo, hi = pb + 96, min(pb + w120, n, len(A))
        mA = A[lo:hi].ravel()
        mB = B[lo - pb:hi - pb].ravel()
        y = x[pa + lo:pa + hi].ravel()
        M = np.stack([mA, mB], axis=1)
        coef, *_ = np.linalg.lstsq(M, y, rcond=None)
        alpha = float(coef[0] / ga) if ga > 0 else float("nan")
        res[(p, q)] = alpha
    print("pairs with alpha < %.2f (B cuts A):" % a.threshold)
    cut = [(p, q) for (p, q), v in res.items() if v < a.threshold]
    for p, q in cut:
        print(f"  {p:3d} {q:3d}  alpha {res[(p, q)]:+.2f}")
    # Groups: connected components.
    parent = {}

    def find(u):
        parent.setdefault(u, u)
        while parent[u] != u:
            u = parent[u]
        return u
    for p, q in cut:
        parent[find(p)] = find(q)
    groups = {}
    for u in list(parent):
        groups.setdefault(find(u), []).append(u)
    print("groups:", sorted(sorted(g) for g in groups.values()))
    notes = sorted({n for pr in res for n in pr})
    print("alpha matrix (row A, column B):")
    print("     " + " ".join(f"{n:4d}" for n in notes))
    for p in notes:
        print(f"{p:4d} " + " ".join(f"{res[(p, q)]:4.1f}" if (p, q) in res else "   ." for q in notes))
    if a.json:
        json.dump({f"{p},{q}": v for (p, q), v in res.items()}, open(a.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
