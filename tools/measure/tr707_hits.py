# Hats in the real recording: are they the ROM's HH, and with which envelope?
import sys, json
import numpy as np

FS = 48000
x = np.load("measure/tr707_rec/rec.npy")
mono = x.mean(axis=1).astype(np.float64)
ic35 = open("roms/IC35_TR707_SNDROM.bin", "rb").read()
hh = np.frombuffer(ic35[8192:16384], np.uint8).astype(float) - 128


def hold(s, f, n):
    t = np.arange(n) * f / FS
    t = t[t < len(s)]
    return s[t.astype(int)]


def hpf(v, fc):
    # long signal: 2nd-order-ish by difference of box means is too crude, use FFT in blocks
    N = 1 << 22
    out = np.zeros_like(v)
    ov = 4096
    pos = 0
    fr = np.fft.rfftfreq(N, 1 / FS)
    g = 1 / (1 + (fc / np.maximum(fr, 1)) ** 8)
    while pos < len(v):
        seg = v[max(0, pos - ov):pos + N - ov]
        y = np.fft.irfft(np.fft.rfft(seg, N) * g, N)[:len(seg)]
        a = pos - max(0, pos - ov)
        m = min(N - 2 * ov, len(v) - pos)
        out[pos:pos + m] = y[a:a + m]
        pos += m
    return out


h = hpf(mono, 4000.0)
np.save("measure/tr707_rec/hp4k.npy", h.astype(np.float32))
n = int(0.03 * FS)
y = hold(hh, 25000, 4096)
Y = np.zeros(4096)
Y[:] = y
yy = hpf(Y, 4000.0)[:n]
yy /= np.linalg.norm(yy)
N = 1 << 22
c = np.zeros(len(h), np.float32)
Yf = np.fft.rfft(yy[::-1], N)
pos = 0
while pos < len(h):
    seg = h[pos:pos + N - n]
    r = np.fft.irfft(np.fft.rfft(seg, N) * Yf, N)[n - 1:n - 1 + len(seg)]
    c[pos:pos + len(seg)] = r
    pos += N - n
cs = np.concatenate([[0], np.cumsum(h ** 2)])
idx = np.arange(len(h) - n)
e = np.sqrt(np.maximum(cs[idx + n] - cs[idx], 1e-14)).astype(np.float32)
ncc = c[:len(idx)] / e
ncc[e < 0.003] = 0
print("max ncc", ncc.max())
for thr in (0.9, 0.8, 0.7, 0.6, 0.5):
    print(thr, (ncc > thr).sum())
above = np.flatnonzero(ncc > 0.6)
pk = []
i = 0
while i < len(above):
    j = i
    while j + 1 < len(above) and above[j + 1] - above[j] < 2400:
        j += 1
    k = above[i:j + 1]
    b = k[np.argmax(ncc[k])]
    pk.append((int(b), float(ncc[b])))
    i = j + 1
print(len(pk), "hat hits")
json.dump(pk, open("measure/tr707_rec/hat_hits.json", "w"))
# Envelope in 4 kHz+ after each hit, until the next strong HF onset: rms per 5 ms, dB re first 5 ms
W = 240
env = []
for p, q in pk:
    seg = h[p:p + 96000]
    m = len(seg) // W
    r = np.sqrt((seg[:m * W].reshape(m, W) ** 2).mean(axis=1))
    env.append(20 * np.log10(r / r[:2].max() + 1e-6))
env = [e_ for e_ in env if len(e_) >= 100]
env = np.array([e_[:100] for e_ in env])
times = [1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 80]
# classify by level at 60 ms
lvl = env[:, 12]
print("hist level at 60ms", np.histogram(lvl, bins=[-80, -40, -30, -25, -20, -15, -10, -5, 5])[0])
for lo, hi in ((-80, -25), (-25, -15), (-15, 5)):
    s = env[(lvl >= lo) & (lvl < hi)]
    if len(s):
        print(f"60ms in [{lo},{hi}): n={len(s)}  median env at ms:",
              " ".join(f"{t*5}:{np.median(s[:, t]):.0f}" for t in times))
