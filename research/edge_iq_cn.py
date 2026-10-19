"""C/N of real edge IQ snapshots (C5VRX by Twotoz and the C5VRX contributors).

Carrier power over the no-VTX noise floor at the same gain (G71: -74.2 dB
per Hz in cells^2, 0.95 dB per table step), in a 10 MHz matched filter and
referred to the 20.7 MHz chip channel. 2026-10-19 field captures where the
picture was "pretty much gone": -1.3..+4.4 dB (one 11 dB fade peak), the
model's picture limit is 1.7-3.5 dB: the chain behind the ADC performs as
modelled.

    python edge_iq_cn.py "data/edge_dc_2026-10-19/cap*.bin"
"""
import glob, json, sys
from pathlib import Path
import numpy as np
from scipy import signal as sg
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sim, situations
FS = 40e6
NOISE_G71 = -74.2       # dB/Hz-ish (welch density units, cells^2/Hz), 20 MHz filter, VTX off
DB_PER_STEP = 10.5 / 11  # G71 -> G82 measured
NBW10 = 10.0e6
LUT = situations.lut('kp0.50')
def load(fn):
    s = np.fromfile(fn, np.uint8)
    v = s.astype(int)
    return s, ((((v >> 4) ^ 8) - 8) + .5) + 1j * ((((v & 15) ^ 8) - 8) + .5)
for fn in sorted(sum((glob.glob(p) for p in sys.argv[1:]), [])):
    raw, z = load(fn); g = int(json.load(open(fn[:-4] + '.json'))['gain'])
    tot = np.mean(abs(z - z.mean()) ** 2)
    n = 10 ** ((NOISE_G71 + (g - 71) * DB_PER_STEP) / 10) * NBW10
    cn10 = 10 * np.log10(max(tot - n, 1e-6) / n)
    cn20 = cn10 - 10 * np.log10(20.7e6 / NBW10)
    # ideal discriminator on the same 4-bit samples
    zc = z - z.mean()
    ideal = np.angle(zc[1:] * np.conj(zc[:-1])) * FS / (2 * np.pi)
    ideal = sg.sosfiltfilt(sim.GOGGLE, ideal)
    codes = sim.u85q_codes(raw, LUT)
    u = sg.sosfiltfilt(sim.GOGGLE, sim.DAC_VOLTS[codes].astype(float))
    m = min(len(u), len(ideal)) - 200
    c = np.corrcoef(u[100:m], ideal[100:m])[0, 1]
    # line sync visibility: fold at NTSC line, compare
    def syncq(y):
        L = int(round(63.556e-6 * FS)); k = len(y) // L
        f = y[:k * L].reshape(k, L); w = int(4.7e-6 * FS)
        sm = np.array([np.convolve(r, np.ones(w) / w, 'same') for r in f])
        mean = sm.mean(0); depth = (np.median(mean) - mean.min()) / (np.std(f - f.mean(0)) + 1e-9)
        return depth
    print(f'{Path(fn).parent.name}/{Path(fn).name:9s} G{g} C/N {cn10:5.1f} dB in 10 MHz = {cn20:5.1f} dB ref 20.7 | '
          f'U85Q vs ideal corr {c:+.2f} | sync depth/noise: ideal {syncq(ideal):4.1f} u85q {syncq(u):4.1f}')
