# Every TR-707 sound: recording against the model's render. Spectrum and envelope.
#   f707d.py RENDER.wav
import sys, wave, json
import numpy as np

FS = 48000
render = sys.argv[1]
w = wave.open(render)
assert w.getframerate() == FS
sw = w.getsampwidth()
raw = w.readframes(w.getnframes())
if sw == 2:
    r = np.frombuffer(raw, np.int16).reshape(-1, 2).astype(float) / 32768
else:
    b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
    v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
    r = np.where(v >= 1 << 23, v - (1 << 24), v).reshape(-1, 2) / float(1 << 23)
rm = r.mean(axis=1)
x = np.load("measure/tr707_rec/rec.npy").mean(axis=1).astype(np.float64)

T = {"BD1": .25, "BD2": 3.05, "SD1": 5.85, "SD2": 8.65, "LT": 11.45, "MT": 14.25, "HT": 17.05,
     "RS": 19.85, "CB": 22.65, "HCP": 25.45, "TAMB": 28.25, "CH": 31.05, "OH": 33.85,
     "CRASH": 37.85, "RIDE": 45.75}
LEN = {"BD1": .1, "BD2": .1, "SD1": .12, "SD2": .12, "LT": .3, "MT": .3, "HT": .3, "RS": .06, "CB": .12,
       "HCP": .14, "TAMB": .14, "CH": .06, "OH": .2, "CRASH": .5, "RIDE": .5}
HP = {"BD1": 0, "BD2": 0, "LT": 0, "MT": 0, "HT": 0}
edges = 125 * 2 ** (np.arange(0, 22) / 3)   # third octaves, 125 Hz .. 20 kHz
centres = np.sqrt(edges[:-1] * edges[1:])


def filt(v, fc, N=1 << 22):
    if not fc:
        return v
    out = np.zeros_like(v)
    ov = 8192
    pos = 0
    n = 1 << int(np.ceil(np.log2(min(N, len(v) + 2 * ov))))
    fr = np.fft.rfftfreq(n, 1 / FS)
    g = 1 / (1 + (fc / np.maximum(fr, 1)) ** 8)
    while pos < len(v):
        a0 = max(0, pos - ov)
        seg = v[a0:pos + n - 2 * ov]
        y = np.fft.irfft(np.fft.rfft(seg, n) * g, n)[:len(seg)]
        a = pos - a0
        m = min(n - 2 * ov, len(v) - pos)
        out[pos:pos + m] = y[a:a + m]
        pos += m
    return out


def bands(seg):
    win = np.hanning(2 * len(seg))[len(seg):]        # weights the start, the part with the sound
    S = np.abs(np.fft.rfft(seg * win, 1 << 16)) ** 2
    fr = np.fft.rfftfreq(1 << 16, 1 / FS)
    return np.array([S[(fr >= a) & (fr < b)].mean() for a, b in zip(edges[:-1], edges[1:])])


hpcache = {}
print("band:   " + " ".join(f"{c/1000:5.1f}" for c in centres))
res = {}
for name, t0 in T.items():
    s0 = int(t0 * FS)
    tail = rm[s0:s0 + int(.4 * FS)]
    on = np.argmax(np.abs(tail) > 0.05 * np.abs(tail).max())
    s0 += on
    n = int(min(LEN[name], .06) * FS)
    fc = HP.get(name, 1500.0)
    if fc not in hpcache:
        hpcache[fc] = filt(x, fc)
    h = hpcache[fc]
    tm = filt(rm[s0 - 4096:s0 + 16384], fc)[4096:4096 + n]
    tm = tm / np.linalg.norm(tm)
    N = 1 << 22
    Yf = np.fft.rfft(tm[::-1], N)
    c = np.zeros(len(h), np.float32)
    pos = 0
    while pos < len(h):
        seg = h[pos:pos + N - n]
        c[pos:pos + len(seg)] = np.fft.irfft(np.fft.rfft(seg, N) * Yf, N)[n - 1:n - 1 + len(seg)]
        pos += N - n
    cs = np.concatenate([[0], np.cumsum(h ** 2)])
    idx = np.arange(len(h) - n)
    e = np.sqrt(np.maximum(cs[idx + n] - cs[idx], 1e-14)).astype(np.float32)
    ncc = c[:len(idx)] / e
    ncc[e < 0.004] = 0
    # peaks, at least 40 ms apart
    order = []
    cand = np.flatnonzero(ncc > max(0.4, ncc.max() * 0.8))
    i = 0
    while i < len(cand):
        j = i
        while j + 1 < len(cand) and cand[j + 1] - cand[j] < 1920:
            j += 1
        k = cand[i:j + 1]
        p = k[np.argmax(ncc[k])]
        order.append((float(ncc[p]), int(p)))
        i = j + 1
    order.sort(reverse=True)
    L = int(LEN[name] * FS)
    hits = [int(p) for _, p in order[:200] if p + L < len(x)]
    if len(hits) < 3:
        print(f"{name:6} few hits ({len(hits)}), best ncc {ncc.max():.2f}")
        continue
    B = np.array([bands(x[p:p + L]) for p in hits])
    real = np.median(B, axis=0)
    model = bands(rm[s0:s0 + L])
    d = 10 * np.log10(real / model)
    ref = d[(centres > 800) & (centres < 2500)].mean()
    d = d - ref
    res[name] = {"hits": hits, "d": d.tolist(), "s0": int(s0)}
    print(f"{name:6} n={len(hits):3} ncc {order[0][0]:.2f}..{order[min(len(order), 60) - 1][0]:.2f}")
    print("  r-m:  " + " ".join(f"{v:5.1f}" for v in d), flush=True)
    # envelope: broad band above fc, rms per 5 ms
    W = 240
    m = L // W
    hh = np.array([h[p:p + m * W].reshape(m, W) for p in hits])
    er = np.median(np.sqrt((hh ** 2).mean(axis=2)), axis=0)
    mm = filt(rm[s0 - 4096:s0 + L + 4096], fc)[4096:4096 + m * W].reshape(m, W)
    em = np.sqrt((mm ** 2).mean(axis=1))
    idxs = [i for i in (0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96) if i < m]
    print("  ms:   " + " ".join(f"{i*5:5d}" for i in idxs))
    print("  real: " + " ".join(f"{20*np.log10(er[i]/er[:4].max()+1e-6):5.0f}" for i in idxs))
    print("  model:" + " ".join(f"{20*np.log10(em[i]/em[:4].max()+1e-6):5.0f}" for i in idxs), flush=True)
json.dump(res, open("measure/tr707_rec/cmp.json", "w"))
