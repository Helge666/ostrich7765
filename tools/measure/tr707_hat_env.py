# The open hat over its whole length: hits followed by no hat for 450 ms or more.
import json
import numpy as np

FS = 48000
h = np.load("measure/tr707_rec/hp4k.npy").astype(np.float64)
pk = sorted(p for p, _ in json.load(open("measure/tr707_rec/hat_hits.json")))
ic35 = open("roms/IC35_TR707_SNDROM.bin", "rb").read()
hh = np.frombuffer(ic35[8192:16384], np.uint8).astype(float) - 128
t = (np.arange(int(8192 / 25000 * FS)) * 25000 / FS).astype(int)
Y = np.fft.rfft(hh[t], 1 << 15)
fr = np.fft.rfftfreq(1 << 15, 1 / FS)
Y *= 1 / (1 + (4000 / np.maximum(fr, 1)) ** 8)
y = np.fft.irfft(Y, 1 << 15)[:len(t)]
W = 480
m = 44
rom = np.zeros(m)
n = len(y) // W
rom[:n] = np.sqrt((y[:n * W].reshape(n, W) ** 2).mean(axis=1))
rom_db = 20 * np.log10(np.maximum(rom, 1e-9) / rom[0])
gaps = np.diff(pk)
sel = [pk[i] for i in range(len(gaps)) if gaps[i] > 0.45 * FS]
E = np.array([np.sqrt((h[p:p + m * W].reshape(m, W) ** 2).mean(axis=1)) for p in sel if p + m * W < len(h)])
E = E / E[:, :1]
db = 20 * np.log10(E + 1e-6)
a = db[:, 12]
print(len(db), "hits, hist at 120 ms", np.histogram(a, bins=[-90, -40, -30, -20, -12, 10])[0])
ts = list(range(0, m, 2))
print("ms     " + " ".join(f"{i*10:4d}" for i in ts))
print("rom    " + " ".join(f"{rom_db[i]:4.0f}" for i in ts))
s = db[a > -12]
print("open n", len(s))
for q in (25, 50, 75):
    e = np.percentile(s, q, axis=0)
    print(f" p{q}   " + " ".join(f"{e[i]:4.0f}" for i in ts))
e = np.median(s, axis=0)
print(" eg    " + " ".join(f"{e[i]-rom_db[i]:4.0f}" for i in ts))
s = db[a < -30]
print("closed n", len(s))
e = np.median(s, axis=0)
print(" med   " + " ".join(f"{e[i]:4.0f}" for i in ts))
print(" eg    " + " ".join(f"{e[i]-rom_db[i]:4.0f}" for i in ts))
# fine closed curve, 5 ms steps
W2 = 240
m2 = 40
rom2 = np.sqrt((y[:m2 * W2].reshape(m2, W2) ** 2).mean(axis=1))
E2 = np.array([np.sqrt((h[p:p + m2 * W2].reshape(m2, W2) ** 2).mean(axis=1)) / rom2 for p in sel if p + m2 * W2 < len(h)])
E2 = E2 / E2[:, :2].mean(axis=1, keepdims=True)
c = E2[a < -30]
med = np.median(c, axis=0)
print("closed eg 5 ms: " + " ".join(f"{20*np.log10(v):.1f}" for v in med[:30]))
