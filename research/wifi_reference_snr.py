"""C5VRX by Twotoz and the C5VRX contributors: Wi-Fi packets in our own IQ
at G83 against the vendor AGC RSSI of the same moments (data/wcal_2026-10-19).
A packet of RSSI R dBm should sit R + 95.8 dB over our noise if our receive
state is as sensitive as Wi-Fi mode (-168 dBm/Hz); measured: bulk at -89.5
dBm -> 6-7 dB (expected 6.3). Snapshots: C5VRX tuned to 5180 MHz, G = MAX.
"""
import glob
import numpy as np
from scipy import signal as sg
W = 80
pw = []; psd_n = []; psd_p = []
for fn in sorted(glob.glob('wcal/MAX_*.bin')):
    s = np.fromfile(fn, np.uint8).astype(int)
    z = ((((s >> 4) ^ 8) - 8) + .5) + 1j * ((((s & 15) ^ 8) - 8) + .5)
    n = len(z) // W
    Z = z[:n * W].reshape(n, W)
    p = np.var(Z.real, 1) + np.var(Z.imag, 1)
    pw.append(p)
p = np.concatenate(pw)
nf = np.percentile(p, 20)
snr = 10 * np.log10(np.maximum(p / nf - 1, 1e-3))
h, e = np.histogram(snr, bins=np.arange(-3, 16, 1))
print(f'G83 noise {nf:.2f} cells^2, windows {len(p)}')
print('SNR dB ' + ' '.join(f'{int(x):>5d}' for x in e[:-1]))
print('count  ' + ' '.join(f'{int(x):>5d}' for x in h))
# noise spectrum shape: occupied bandwidth of our noise (NBW)
allz = []
for fn in sorted(glob.glob('wcal/MAX_*.bin'))[:40]:
    s = np.fromfile(fn, np.uint8).astype(int)
    z = ((((s >> 4) ^ 8) - 8) + .5) + 1j * ((((s & 15) ^ 8) - 8) + .5)
    allz.append(z - z.mean())
f, P = sg.welch(np.concatenate(allz), fs=40e6, nperseg=256, return_onesided=False)
f = np.fft.fftshift(f); P = np.fft.fftshift(P)
nbw = P.sum() * (f[1] - f[0]) / P[abs(f) < 3e6].mean()
print(f'noise NBW (incl. packets, approx) {nbw/1e6:.1f} MHz')
