#!/usr/bin/env python3
"""C5VRX-5 range audit from a decoded blackbox (C5VRX by Twotoz and the
C5VRX contributors).

Where does the link fail at the edge? Two views of the same walk:

  seconds   every second binned by C/N: video share, picture SNR, gain, DC,
            AFC residual, kp/LP, dropouts and the received level;
  snapshots the raw edge IQ (FLOG snapshots): carrier and noise from the
            4-bit samples themselves (M2M4 for a constant-envelope FM
            carrier), DC and rail use, next to that second's record.

    python tools/flight_log.py decode flight.c5log out_dir
    python tools/edge_audit.py out_dir 34            # session 34
"""
import argparse
import csv
import glob
import json
import math
import os
import statistics as st


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def mean(vals):
    vals = [v for v in vals if v is not None]
    return st.mean(vals) if vals else float('nan')


def median(vals):
    vals = [v for v in vals if v is not None]
    return st.median(vals) if vals else float('nan')


def seconds_view(rows):
    bins = {}
    for r in rows:
        cn = num(r.get('cn_db'))
        bins.setdefault('unk' if cn is None or cn < -20 else int(cn // 2 * 2), []).append(r)
    print(' C/N bin    n  video%  vq_snr   gain  |dc|cells  afc_khz    kp   lp  dropouts    dBm')
    for key in sorted(bins, key=lambda k: -999 if k == 'unk' else k):
        rs = bins[key]
        dc = [math.hypot(num(r['dc_i_cells']) or 0, num(r['dc_q_cells']) or 0) for r in rs]
        afc = [abs(num(r['afc_residual_khz'])) if num(r['afc_residual_khz']) is not None else None for r in rs]
        level = [num(r['level_dbm']) for r in rs if r.get('level_above_noise') in ('1', 'True')]
        snr = [num(r['vq_snr_db']) for r in rs if (num(r['vq_snr_db']) or -99) > -50]
        drops = sum(1 for r in rs if r.get('dropout') not in ('0', '', 'False', None))
        print(f' {str(key):>7} {len(rs):4d}  {mean(num(r["video_pct"]) for r in rs):5.0f}  {mean(snr):6.1f}  '
              f'{mean(num(r["gain_mean"]) for r in rs):5.1f}  {mean(dc):8.2f}  {median(afc):7.0f}  '
              f'{mean(num(r["kp"]) for r in rs):4.2f}  {mean(num(r["lp"]) for r in rs):3.1f}  {drops:8d}  '
              f'{median(level):6.1f}')


def iq_cells(raw):
    xs, ys = [], []
    for v in raw:
        i, q = v >> 4, v & 15
        xs.append((i - 16 if i > 7 else i) + .5)
        ys.append((q - 16 if q > 7 else q) + .5)
    return xs, ys


def snapshot_view(folder, session, rows):
    by_t = {int(r['t_s']): r for r in rows}
    print(' t_s  trigger   gain  carrier_r  C/N_iq  C/N_rec    dBm  video%  dc_cells  rail%  noise_cells2')
    for jp in sorted(glob.glob(os.path.join(folder, f'snap_s{session:04d}_*.json'))):
        j = json.load(open(jp))
        xs, ys = iq_cells(open(jp[:-5] + '.bin', 'rb').read())
        n = len(xs)
        if not n:
            continue
        mx, my = sum(xs) / n, sum(ys) / n
        p2 = [(x - mx) ** 2 + (y - my) ** 2 for x, y in zip(xs, ys)]
        m2, m4 = sum(p2) / n, sum(v * v for v in p2) / n
        c = math.sqrt(max(2 * m2 * m2 - m4, 0.0))      # carrier power (M2M4)
        noise = m2 - c
        cn = 10 * math.log10(c / noise) if c > 0 and noise > 0 else float('nan')
        rail = sum(1 for x, y in zip(xs, ys) if abs(x) == 7.5 or abs(y) == 7.5) * 100 / n
        t = j['t_ms'] // 1000
        r = by_t.get(t) or by_t.get(t + 1) or {}
        print(f' {t:4d}  {j["trigger"][:8]:8s}  {j["gain"]:4d}  {math.sqrt(c):9.2f}  {cn:6.1f}  '
              f'{r.get("cn_db", ""):>7}  {r.get("level_dbm", ""):>6}  {r.get("video_pct", ""):>6}  '
              f'{math.hypot(mx, my):8.2f}  {rail:5.1f}  {noise:12.2f}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('folder', help='output folder of flight_log.py decode')
    ap.add_argument('sessions', type=int, nargs='+')
    a = ap.parse_args()
    for s in a.sessions:
        path = os.path.join(a.folder, f'session_{s:04d}_seconds.csv')
        rows = list(csv.DictReader(open(path)))
        print(f'== session {s}: {len(rows)} s')
        seconds_view(rows)
        snapshot_view(a.folder, s, rows)


if __name__ == '__main__':
    main()
