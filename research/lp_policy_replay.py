#!/usr/bin/env python3
"""LP policy A/B on recorded walks (C5VRX by Twotoz and the C5VRX
contributors; range audit PR #230).

Replays the per-second flight-log records of a decoded session through two
LP policies and counts what each would have done:

  gain  the policy until PR #230: LP from G70, U85Q again under G65
        (2 s release hold, 3 s after the last switch);
  cn    LP below the bit-exact "+1 dB benefit" C/N of the VTX class, U85Q
        above its "0 dB" crossover for 2 s, an unknown C/N holds
        (thresholds from research/data/best_combo*.json, PR #230 review).

Per policy: switches, seconds in LP where the C/N is above the exit point
(unnecessary: LP costs 2-3.5 dB SINAD on a good link, and each switch ~5.5 ms
of video), seconds without LP where the C/N is below the entry point
(missed rescue). Per-second records only: 50 ms fades are not resolved.

    python tools/flight_log.py decode flight.c5log out
    python research/lp_policy_replay.py out/session_0053_seconds.csv [--enter 4.4 --exit 7.5]
"""
import argparse
import csv


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def replay(rows, policy, enter, exit_, hold_s=2, gap_s=3):
    lp, since, last_switch, switches = False, None, -99, 0
    unnecessary = missed = known = 0
    for r in rows:
        t = int(r['t_s'])
        gain = num(r['gain_mean']) or 0
        cn = num(r['cn_db'])
        if cn is not None and cn < -50:
            cn = None
        if policy == 'gain':
            want = gain >= 70 if not lp else gain >= 65
        else:
            want = lp if cn is None else (cn < enter if not lp else cn <= exit_)
        if want != lp:
            if since is None:
                since = t
            need = 0 if want else hold_s
            if t - since >= need and t - last_switch >= (0 if want else gap_s):
                lp, last_switch, since = want, t, None
                switches += 1
        else:
            since = None
        if cn is not None:
            known += 1
            unnecessary += lp and cn > exit_
            missed += (not lp) and cn < enter
    return switches, unnecessary, missed, known


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('csv', nargs='+')
    ap.add_argument('--enter', type=float, default=4.4, help='C/N for LP (20.7 MHz scale; deviation 1.0, 12 MHz)')
    ap.add_argument('--exit', dest='exit_', type=float, default=7.5)
    a = ap.parse_args()
    for path in a.csv:
        rows = list(csv.DictReader(open(path)))
        print(f'== {path}: {len(rows)} s')
        for policy in ('gain', 'cn'):
            sw, un, mi, kn = replay(rows, policy, a.enter, a.exit_)
            print(f'   {policy:4s}: switches {sw:3d}  LP above the exit C/N {un:4d} s  '
                  f'no LP below the entry C/N {mi:4d} s  (of {kn} s with a C/N)')


if __name__ == '__main__':
    main()
