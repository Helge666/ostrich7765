# Coherent transfer real / model per third octave: other sounds in the recording do not
# correlate with the model's sound, so they average out (unlike power spectra).
#   f707l.py RENDER.wav
import sys, json, wave
import numpy as np

FS = 48000
w = wave.open(sys.argv[1])
raw = w.readframes(w.getnframes())
if w.getsampwidth() == 2:
    r = np.frombuffer(raw, np.int16).reshape(-1, 2).astype(float) / 32768
else:
    b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
    v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
    r = np.where(v >= 1 << 23, v - (1 << 24), v).reshape(-1, 2) / float(1 << 23)
rm = r.mean(axis=1)
x = np.load("measure/tr707_rec/rec.npy").mean(axis=1).astype(np.float64)
cmp_ = json.load(open("measure/tr707_rec/cmp.json"))
edges = 125 * 2 ** (np.arange(0, 22) / 3)
centres = np.sqrt(edges[:-1] * edges[1:])
T = {"BD1": .25, "BD2": 3.05, "SD1": 5.85, "SD2": 8.65, "LT": 11.45, "MT": 14.25, "HT": 17.05,
     "RS": 19.85, "CB": 22.65, "HCP": 25.45, "TAMB": 28.25, "CH": 31.05, "OH": 33.85, "CRASH": 37.85}
LEN = {"BD1": .1, "BD2": .1, "SD1": .1, "SD2": .1, "LT": .15, "MT": .15, "HT": .15, "RS": .05, "CB": .1,
       "HCP": .1, "TAMB": .1, "CH": .03, "OH": .1, "CRASH": .3}
print("band:  " + " ".join(f"{c/1000:5.1f}" for c in centres))
out = {}
for name, t0 in T.items():
    if name not in cmp_:
        continue
    s0 = int(t0 * FS)
    tail = rm[s0:s0 + int(.4 * FS)]
    s0 += int(np.argmax(np.abs(tail) > 0.05 * np.abs(tail).max()))
    L = int(LEN[name] * FS)
    win = np.hanning(2 * L)[L:]
    N = 1 << 14
    m = rm[s0:s0 + L]
    M = np.fft.rfft(m * win, N)
    fr = np.fft.rfftfreq(N, 1 / FS)
    num = np.zeros(len(M), complex)
    cnt = 0
    for p in cmp_[name]["hits"]:
        if p < 2000 or p + L + 2000 > len(x):
            continue
        # align: best lag of the raw signals near the hit
        seg = x[p - 200:p + L + 200]
        c = np.correlate(seg, m, "valid")
        lag = int(np.argmax(c)) - 200
        s = x[p + lag:p + lag + L]
        S = np.fft.rfft(s * win, N)
        # per-hit gain (accent): normalise by its projection on the model
        g = np.dot(s, m) / np.dot(m, m)
        if g <= 0:
            continue
        num += S * np.conj(M) / g
        cnt += 1
    den = (np.abs(M) ** 2) * cnt
    d = []
    for a, b2 in zip(edges[:-1], edges[1:]):
        k = (fr >= a) & (fr < b2)
        d.append(10 * np.log10(np.abs(num[k]).sum() ** 2 / (den[k].sum() ** 2) + 1e-12) / 1)
    d = np.array(d)
    d = d - d[(centres > 800) & (centres < 2500)].mean()
    out[name] = d.tolist()
    print(f"{name:6} " + " ".join(f"{v:5.1f}" for v in d) + f"  n={cnt}", flush=True)
json.dump({"centres": centres.tolist(), "d": out}, open("measure/tr707_rec/coh.json", "w"))
