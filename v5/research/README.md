# Where can C5VRX-5 firmware still gain range?

Simulation study, 2026-10-11. `sim.py` models the whole C5VRX-5 chain with
the **bit-exact U85Q LUT** (two LUT16 lookups per 50 ns, `u85q.bsasm`), the
C5 analog channel (5th-order, ~20 MHz), unfiltered 80 -> 40 MS/s decimation,
4-bit ultrafine quantisation at the Direct Gain V5 radius, the resistor DAC,
a 5 MHz goggle input filter and a goggle sync/H-PLL model (ideal and
peak-clamp AGC). `range_study.py` runs the experiments; raw rows and tables
are in `data/`. Synthetic numbers, not measured range.

Each configuration gets two thresholds over a C/N sweep (4 cases per point,
PAL and NTSC, C/N per 20 MHz channel):

- **lock**: the goggle still shows 90 % of the lines correctly;
- **usable**: the luma SINAD reaches 0 dB (below that the picture is noise).

The range limit is the worse of the two. Free-space range scales with
10^(dB/20): 3 dB is 1.41x the distance.

## 1. Sync is not the bottleneck

| configuration (deviation 1.0) | lock | usable |
| --- | ---: | ---: |
| 20 MHz channel | 1.80 dB | 3.55 dB |
| 14 MHz filter | 0.85 dB | 2.77 dB |

The goggle keeps lock 1.5-4 dB below the point where the picture is still
usable. Perfect sync (true timing pasted in) moves lock below -3 dB, but a
realistic regenerator (a slow line PLL on the received video, writing clean
syncs where it predicts them) gains nothing: it sees the same noisy syncs
the goggle already flywheels over. A sync flywheel is not a range lever.

## 2. The picture limit depends on the VTX deviation

Usable threshold, then the combined range limit (worse of lock/usable):

| deviation | v5.0 (14 MHz, kp 0.85) | best | setting | gain |
| --- | ---: | ---: | --- | ---: |
| 0.69 | 4.86 dB | 1.66 dB | 10 MHz filter + kp 0.5 | **3.2 dB** |
| 1.0 | 2.77 dB | 1.10 dB | 10 MHz filter + kp 0.6 | **1.7 dB** |
| 1.4 | 2.31 dB | 2.08 dB | 12 MHz filter + kp 0.85 | 0.2 dB |

A narrower pre-detection filter lowers the noise bandwidth; a lower tracker
loop gain narrows the tracking bandwidth (classic FM threshold extension).
Both suit low-deviation VTXs and hurt wide ones: the low-deviation setting
on a deviation-1.4 VTX loses the picture (0.71 at 4 dB) and sharpness
(SINAD 6.9 vs 14.6 dB at 25 dB C/N). The full grid is in
`data/matched.txt`.

## 3. Strong signal and carrier error (`data/strong.txt`)

- 12 MHz + kp 0.85 (the new default) is at least as good as 14 MHz for
  every deviation at weak signal and loses <= 0.8 dB SINAD close in.
- The matched settings cost 0.9-1.7 dB SINAD at 25 dB C/N (still 10-12 dB,
  a good picture) and win from 12 dB C/N downward.
- A residual carrier error of -0.5 MHz hurts the narrow settings at the
  edge; the AFC holds +-50 kHz, so this is margin, not the operating point.

## 4. Tracker gain alone (`data/tracker.txt`)

kp 0.7 / 0.6 / 0.5 on the 14 MHz filter: up to 2.4 dB better usable
threshold at deviation 0.69, about equal at 1.0, worse at 1.4. Only useful
together with a deviation measurement.

## 5. What C5VRX-5 does with it

- Default filter class >= 12 MHz (was 14 MHz): safe for every VTX.
- **VTX-matched mode**: the AFC already measures the sync tip and the
  burst-free porch of every line; their distance is deviation x 1.914 MHz.
  After 32 tight estimates on a usable carrier the receiver picks
  10 MHz + kp 0.5 (deviation <= 0.85) or 10 MHz + kp 0.6 (<= 1.15), else
  keeps the default. Once per channel session; a retune or reboot starts
  over. The kp variants are the same untrained formula compiled with the
  original generator (kp 0.85 reproduces `u85q.bsasm` bit-exactly).

## Not firmware levers

- The second IQ40 sample of every span (+0.5..1.1 dB in the C5VRX-4 ledger)
  needs four lookups per 50 ns; the TX BitScrambler does two.
- Antenna diversity, LNA and filters in front of the C5: hardware.

## Reproduce

```
python range_study.py filter  data/filter.json
python range_study.py sync    data/sync.json
python range_study.py tracker data/tracker.json
python range_study.py matched data/matched.json
CNRS=0,2,4,8,12,16,20,25 python range_study.py strong data/strong.json
```

Needs numpy, scipy and numba. `data/tracker_luts.json` holds the kp variant
LUTs (generated with the C5VRX-4 untrained generator of PR #190).
