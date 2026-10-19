"""C5VRX by Twotoz and the C5VRX contributors.

Our chain vs an analog limiter-discriminator (RX5808 class) on identical
signals: picture (goggle lock share) and luma SINAD per C/N (20 MHz ref).

Result 2026-10-19 (deviation 0.69): the matched C5 chain holds the goggle
~1 dB lower than an analog 14 MHz IF and is 4-7 dB cleaner; there is no
cliff after the ADC. Range lost against an RX5808 is lost before it.

    python analog_compare.py [deviation]
"""
import sys, multiprocessing as mp
from pathlib import Path
import numpy as np
from scipy import signal as sg
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sim, situations

def analog_iq(seed, cnr, std, dev, filter_hz):
    rng = np.random.default_rng(seed)
    ire, region = sim.raster(std, seed + 7, 2)
    source = sg.lfilter(sg.firwin(81, 6e6, fs=sim.F80), 1, np.repeat(ire, 2))
    freq = sim.PORCH_HZ + dev * source / 140 * sim.DEV_PP
    z = sg.sosfilt(sim.CHAN, np.exp(2j * np.pi * np.cumsum(freq) / sim.F80))[::2]; z /= np.sqrt(np.mean(abs(z) ** 2))
    n = sg.sosfilt(sim.CHAN, (rng.normal(size=2 * len(z)) + 1j * rng.normal(size=2 * len(z))) / np.sqrt(2))[::2]
    n /= np.sqrt(np.mean(abs(n) ** 2))
    pre = sg.butter(4, filter_hz / 2, fs=sim.FS, output='sos') if filter_hz else None
    r = 10 ** (cnr / 10)
    f = (lambda y: sg.sosfilt(pre, y)) if pre is not None else (lambda y: y)
    return f(z * np.sqrt(r) + n), f(z * np.sqrt(r)), sg.sosfilt(sim.GOGGLE, ire), region

def disc(y):
    d = np.angle(y[1:] * np.conj(y[:-1])); return sg.sosfilt(sim.GOGGLE, np.r_[d, d[-1]])

def score(y, yc, truth, region, std):
    yi = sim.apply_cal(y, sim.calibrate(yc, truth, region))
    t, rg = truth[3000:-3000], region[3000:-3000]; m = rg == 1
    e = yi[3000:-3000][m] - t[m]
    sinad = 10 * np.log10(np.var(t[m]) / max(np.mean(e ** 2), 1e-12))
    pic = min(sim.goggle(yi, truth, std, 'ideal'), sim.goggle(yi, truth, std, 'peak'))
    return pic, sinad

def job(spec):
    cnr, seed, dev = spec
    std = ('PAL', 'NTSC')[seed % 2]
    out = {}
    # ours: default (chan 20 + kp0.85) and matched (10 MHz + kp0.5)
    for name, fhz, lut in (('C5 default', None, None), ('C5 matched', 10e6, 'kp0.50')):
        c = sim.make_case(1000 * seed + int(10 * cnr) + 7, cnr, std, dev, filter_hz=fhz)
        L = situations.lut(lut) if lut else None
        r = sim.measure(c, sim.u85q_codes(c['raw'], L), sim.u85q_codes(c['clean'], L))
        out[name] = (r['picture'], r['sinad'])
    for name, fhz in (('analog 20MHz IF', None), ('analog 14MHz IF', 14e6)):
        y, yc, truth, region = analog_iq(1000 * seed + int(10 * cnr) + 7, cnr, std, dev, fhz)
        out[name] = score(disc(y), disc(yc), truth, region, std)
    return cnr, seed, dev, out

if __name__ == '__main__':
    dev = float(sys.argv[1]) if len(sys.argv) > 1 else 0.69
    specs = [(c, s, dev) for c in (-4, -2, 0, 1, 2, 3, 4, 6, 8, 10) for s in (11, 12)]
    with mp.Pool(6) as p: rows = p.map(job, specs)
    names = list(rows[0][3])
    print(f'deviation {dev}: picture share / luma SINAD dB (mean of PAL+NTSC)')
    print('C/N  ' + ''.join(f'{n:>22s}' for n in names))
    for c in sorted({r[0] for r in rows}):
        v = [r[3] for r in rows if r[0] == c]
        print(f'{c:4.0f} ' + ''.join(f'{np.mean([x[n][0] for x in v]):12.2f} {np.mean([x[n][1] for x in v]):7.1f}dB  ' for n in names))
