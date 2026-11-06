# Every multiplexed voice: its envelope in the recording and in the model, each over the ROM.
#   f707p.py RENDER.wav
import json, sys, wave
import numpy as np

FS = 48000
x = np.load("measure/tr707_rec/rec.npy").mean(axis=1).astype(np.float64)
cmp_ = json.load(open("measure/tr707_rec/cmp.json"))
hats = np.array(sorted(p for p, _ in json.load(open("measure/tr707_rec/hat_hits.json"))))
rom_all = np.concatenate([np.frombuffer(open("roms/" + f, "rb").read(), np.uint8).astype(float) - 128
                          for f in ("IC34_TR707_SNDROM.bin", "IC35_TR707_SNDROM.bin")])
w = wave.open(sys.argv[1])
raw = w.readframes(w.getnframes())
if w.getsampwidth() == 2:
    r = np.frombuffer(raw, np.int16).reshape(-1, 2).astype(float) / 32768
else:
    b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
    v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
    r = np.where(v >= 1 << 23, v - (1 << 24), v).reshape(-1, 2) / float(1 << 23)
rm = r.mean(axis=1)


def bp(v, lo, hi):
    n = 1 << int(np.ceil(np.log2(len(v) + 4096)))
    f = np.fft.rfftfreq(n, 1 / FS)
    g = 1 / (1 + (lo / np.maximum(f, 1)) ** 8) / (1 + (f / hi) ** 8)
    return np.fft.irfft(np.fft.rfft(v, n) * g, n)[:len(v)]


# name: voice, var (None: plain counter), rate, t0 in the render, length, window, band
V = {
    "BD1": (0, 0, 25000, .25, .16, 20, (50, 150)),
    "BD2": (0, 1, 25000, 3.05, .16, 20, (50, 150)),
    "SD1": (1, 0, 25000, 5.85, .16, 20, (400, 6000)),
    "SD2": (1, 1, 25000, 8.65, .16, 20, (400, 6000)),
    "LT": (2, None, 12500, 11.45, .6, 40, (100, 1500)),
    "MT": (3, None, 12500, 14.25, .6, 40, (100, 1500)),
    "HT": (4, None, 12500, 17.05, .6, 40, (100, 1500)),
    "RS": (6, 0, 25000, 19.85, .08, 10, (400, 6000)),
    "CB": (6, 1, 25000, 22.65, .16, 20, (400, 6000)),
    "HCP": (7, 0, 25000, 25.45, .16, 20, (500, 6000)),
    "TAMB": (7, 1, 25000, 28.25, .16, 20, (2000, 8000)),
}
for name, (voice, var, rate, t0, Ls, Wms, (lo, hi)) in V.items():
    if name not in cmp_:
        continue
    L = int(Ls * FS)
    W = int(Wms * FS / 1000)
    m = L // W
    n = np.arange(L)
    k = (n / FS * rate).astype(int)
    k = k * 2 + var if var is not None else k
    rom = rom_all[(voice << 13) + np.minimum(k, 8191)]
    s0 = int(t0 * FS)
    tail = rm[s0:s0 + int(.4 * FS)]
    s0 += int(np.argmax(np.abs(tail) > 0.05 * np.abs(tail).max()))

    def env(v):
        return np.sqrt((v[:m * W].reshape(m, W) ** 2).mean(axis=1))

    hits = [p for p in cmp_[name]["hits"] if 3000 < p < len(x) - L - 3000]
    free = [p for p in hits if not ((hats > p + 480) & (hats < p + L)).any()]
    use = free if len(free) >= 6 else hits
    E = []
    for p in use:
        e = env(bp(x[p - 1500:p + L + 1500], lo, hi)[1500:1500 + L])
        E.append(e / e[:2].max())
    E = np.array(E)
    eo = env(bp(rom, lo, hi))
    eo = 20 * np.log10(eo / eo[:2].max() + 1e-6)
    em = env(bp(rm[s0 - 1500:s0 + L + 1500], lo, hi)[1500:1500 + L])
    em = 20 * np.log10(em / em[:2].max() + 1e-6)
    p25 = 20 * np.log10(np.percentile(E, 25, axis=0) + 1e-6)
    med = 20 * np.log10(np.median(E, axis=0) + 1e-6)
    print(f"{name}  band {lo}-{hi} Hz, {len(hits)} hits, {len(use)} used")
    print("  ms       " + " ".join(f"{i*Wms:4d}" for i in range(m)))
    print("  rom      " + " ".join(f"{v:4.0f}" for v in eo))
    print("  real-rom " + " ".join(f"{a-b:4.0f}" for a, b in zip(med, eo)))
    print("  p25-rom  " + " ".join(f"{a-b:4.0f}" for a, b in zip(p25, eo)))
    print("  model-rom" + " ".join(f"{a-b:4.0f}" for a, b in zip(em, eo)), flush=True)
