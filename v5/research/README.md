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

## 6. Open field: AFC re-acquisition at weak signal (`afc_bias.py`)

Operator report (2026-10-11): good video indoors through walls, poor range
in an open field. One firmware mechanism acts exactly at long range. After
4 windows without confirmed sync (200 ms) the AFC drops its lock and gets a
fresh budget of 4 retunes of up to 250 kHz. Losing the lock alone changes
nothing: the offset stays and U85Q keeps demodulating. The risk is the
retunes made after the lock is lost.

The porch estimate is a mean of wrapped phase steps. Below the FM
threshold, clicks pull it toward the receiver centre, and the porch target
is -1.0 MHz. Model results (true error 0, median of 16 windows, mean of 40
decisions):

| C/N (20 MHz) | 20 MHz chan | 12 MHz class | 10 MHz class | MAD gate passes (12 MHz) |
| ---: | ---: | ---: | ---: | ---: |
| 0 dB | +254 kHz | +177 kHz | +138 kHz | 52 % |
| 2 dB | +120 kHz | +77 kHz | +65 kHz | 100 % |
| 4 dB | +58 kHz | +27 kHz | +14 kHz | 100 % |
| 6 dB | +20 kHz | +5 kHz | +2 kHz | 100 % |

The deadband is 50 kHz. Around the usable-picture threshold (~1-5 dB) the
AFC can therefore step the wrong way: up to 4 x 250 kHz per lost lock, in
a fade-heavy field. Section 3 shows that a -0.5 MHz error already hurts the
narrow settings at the edge.

The firmware now retunes only when the C/N estimate is >= 6 dB
(`main/cn_estimate.h`). A weak window still feeds the estimate history. The
VTX carrier does not move during a fade, so the offset found on a strong
signal stays. `[AFC]` logs every retune; `afc_weak_skips` in `T` counts the
retunes that were blocked.

This model omits burst detection and sync search at that C/N. Whether such
steps happen on the board shows up in the logs (`[AFC]` lines,
`afc_step=1` in FIELD_DIAG).

### Coherence is not C/N

The observer coherence of receiver noise alone depends on the analog
filter: about 42 % through the ~20 MHz chip channel, 58 % through the 12 MHz
class and 61 % through the 10 MHz class. Fixed coherence thresholds
therefore mean a different C/N per filter class. `afc_bias.py --coherence`
gives the tables behind `cn_estimate.h`. Profile gating, the diagnostics
and the AFC gate use this estimate.

## 7. Open field: propagation, the larger effect

These are textbook two-ray numbers (ground reflection coefficient -1), not
C5VRX measurements. They show why a test with both antennas low can lose
range in an open field, while short indoor paths through walls work.

| heights TX / RX | breakpoint | extra loss vs free space at 300 / 500 / 1000 m |
| --- | ---: | --- |
| 1 m / 1 m (walking test, VTX in hand or on the ground) | 77 m | +2 / +6 / +12 dB |
| 1.5 m / 1.5 m | 174 m | -4 / 0 / +5 dB |
| 30 m / 1.5 m (flying) | 3.5 km | within +-6 dB nulls and peaks |

The first Fresnel zone has a radius of 2.0 m at the middle of a 300 m path,
and 3.6 m at 1 km. With antennas at 1 m, the ground fills part of it.

Free-space budget, for scale: 25 mW, 2 dBi each side, 3 dB polarisation
loss (linear XIAO antenna vs RHCP VTX), 3 dB C/N per 20 MHz at the
threshold. That gives ~1.0 / 0.7 / 0.46 km for a receiver noise figure of
5 / 8 / 12 dB. Indoors, distances are ~10x shorter (20 dB less path loss),
which covers a few walls. A good indoor picture and a short open-field
range are therefore not contradictory. Neither proves anything about the
demodulator.

`docs/FIELD_TEST.md` separates propagation (height and position A/B),
antenna/polarisation and receiver sensitivity (conducted attenuation).

## 8. Open field: Direct Gain through fast nulls (`test/test_gain_fade.c`)

A moving drone passes multipath and ground-reflection nulls lasting about
λ/2 ÷ speed (2.6 cm, ~3 ms at 10 m/s), after which the carrier is back at
its old level. Direct Gain V5 jumped to the table maximum on the first
empty window. From G60 that is about +22 dB, but the ultrafine window has
only ~4 dB of headroom above the healthy radius, so the returning carrier
folded. The 1 ms burst gate plus a staged recovery added time on top.

The real `dg_tick()` was run on a rough receiver model (gain from the
decoded tuples, noise at P50 ~9 at the maximum, fold above 7.5 cells):

| event (carrier healthy at G60) | before | fade hold 10 ms |
| --- | --- | --- |
| null 1-3 ms, -40 dB | +1.8 ms after the carrier returns, 4 writes | 0 ms, 0 writes |
| null 1-3 ms, -12 dB | 0-1 ms, 4-9 writes | 0 ms, 0 writes |
| null >= 10 ms | +1.8 ms, 4 writes | unchanged |
| carrier stays 12 / 22 dB weaker | 1.2 / 10.6 ms | 11 / 20 ms |
| VTX off | table maximum | table maximum (after <= 10 ms) |
| +20 dB jump | cut at once | unchanged |

The price is up to ~10 ms longer recovery when a carrier stays weaker, for
example behind an obstacle. A 100 ms hold cost 81 ms there, so it was
rejected. These are model numbers; every gain write is also a short PHY
transient on the board. `fade_holds` in `T` counts the windows held.

## 9. Open field: what gives the most range (`field_antenna.py`)

This section uses textbook physics and data from datasheets and listings,
not C5VRX measurements.

**The receiver chip.** The ESP32-C5 802.11a 6 Mbps sensitivity is
-92.5 dBm (module datasheet). That puts its noise figure at about 5 dB,
the same class as an RX5808 (-90 dBm datasheet). At the threshold, Direct
Gain holds a radius of ~4 cells, so the 4-bit capture costs ~0.1 dB there.
The chip is not what limits open-field range.

**The antenna.** Retail listings give the FPC antenna bundled with the XIAO
ESP32-C5 as a 2.4 GHz part (2.9 dBi). Its 5.8 GHz performance is unknown
and could be poor; this needs an A/B test.

**Polarisation against the ground bounce.** At FPV grazing angles
(0.4-4 degrees), the ground reflection of the RHCP VTX signal returns
almost entirely left-handed. Over average ground it is -0.2..-2 dB in the
opposite hand and -16..-34 dB in the same hand. An RHCP receive antenna
therefore rejects the bounce, limited by its axial ratio. A linear antenna
loses 3 dB on the direct wave and still gets the full bounce, which causes
the two-ray nulls.

Model ranges (25 mW, 2 dBi RHCP VTX, -93 dBm, 95 % of positions):

| receive antenna | VTX 1 m / RX 1 m | VTX 30 m / RX 1 m | VTX 30 m / RX 3 m |
| --- | ---: | ---: | ---: |
| linear, 0 dBi | 400 m | 550 m | 400 m |
| linear, 6 dB worse (mismatched antenna) | 300 m | 250 m | 250 m |
| RHCP omni 3 dBi (13 dB cross-pol) | 1250 m | 1150 m | 1150 m |
| RHCP patch 8 dBi (aimed) | 2600 m | 3150 m | 2850 m |

**Other levers.**

- VTX power: 25 to 200 mW is +9 dB, about 2.8x range.
- A low-noise LNA right at the antenna (NF ~1 dB, e.g. ADI CN0534: 23 dB
  gain, 2 dB NF) adds ~3-4 dB over a ~5 dB chip NF, minus coax loss. A
  ~5 dB NF LNA adds little (VK4GHZ build).
- Spatial or polarisation diversity needs a second receive path (a second
  C5 plus a video switch).
- Firmware cannot turn a linear antenna into a circular one.

## 10. Wide VTXs: the default 12 MHz class clips them (`range_study.py wide`)

On the board's own VTX the sync tip sat at -5.0 MHz and white at
+6.25 MHz (docs/FINDINGS.md). By the firmware's formula, that 11.25 MHz
swing is a deviation of ~1.7, wider than the 0.69-1.4 studied above.

Range limit (worse of lock and usable), bit-exact U85Q, kp 0.85, C/N per
20 MHz (`data/wide.txt`):

| deviation | 12 MHz (default) | 14 MHz | 16 MHz | 20 MHz chan |
| ---: | ---: | ---: | ---: | ---: |
| 1.4 | **2.08** | 2.31 | 2.54 | 3.06 |
| 1.7 | 3.64 | 3.28 | **3.26** | 3.63 |
| 2.0 | 8.03 | 5.98 | 5.27 | **5.18** |

Picture quality at 8 dB C/N (SINAD) for deviation 1.7 is 5.9 dB at 12 MHz
and 7.9 dB at 16 MHz. A calmer tracker (kp 0.70) is worse for every wide
VTX. The firmware adds a WIDE profile: the same tracker (kp 0.85) with a
filter class >= 16 MHz from the same sweep, chosen at a measured deviation
>= 1.6. The break-even is ~1.55, so a wrong WIDE on a narrow VTX costs only
~0.5-0.8 dB. The host test sends no VTX <= 1.45 into WIDE.

The filter blob gets a new version, so the receiver runs one new filter
sweep after the update (startup, as before).

## 11. Where it is good and where it is bad (`situations.py`)

This stress matrix runs the bit-exact U85Q with the profile the firmware
picks per deviation, at 2 seeds and C/N 4 / 8 / 20 dB (`data/situations.txt`;
the focused comparisons used 4 seeds). Picture is the share of lines the
goggle shows correctly.

**Good** (picture ~1.0 from 4 dB): clean signal, residual carrier error of
+-0.5 MHz, motion fades (3 ms nulls, picture kept, SINAD lower), DC 0.5
cell, I/Q imbalance 1.5 dB / 10 degrees, a CW spur at -10 dBc, and a strong
signal at the top of the gain band.

**Bad, and what was done about it:**

- **Negative carrier error at weak signal.** At 4 dB C/N, -0.75 MHz drops
  the picture to 0.86-0.89 and -1.0 MHz to 0.5-0.75 (dev 1.7), and
  -1.5 MHz loses it (0.03-0.58, all profiles). The sync tip then runs into
  the low edge of the filter and the U85Q window. Positive errors keep the
  picture but cost SINAD.
  - Firmware flaw found in the AFC (`test/test_afc.c`): the correction
    budget (4 x 250 kHz) only came back after a lost lock. A VTX more than
    1 MHz off therefore never got centred and never locked. It kept
    0.2-0.45 MHz residual for the whole session.
  - Fix: while still unlocked, a spent budget re-arms after 3 s. The
    offset clamp is widened from +-1.5 to +-2.5 MHz (channels are >= 19 MHz
    apart). VTXs up to 2.4 MHz off are now centred within 20 s; a centred
    carrier with 80 kHz estimate noise is never retuned. The +-2.5 MHz
    range of the vendor offset helper is not yet hardware-tested.
- **Long multipath echoes** (-6 dB at 0.3 us, -3 dB at 1 us) hurt every
  profile alike. A wide VTX loses the picture with either echo, and every
  VTX loses it with the 1 us echo up to 8 dB. This is frequency-selective
  fading. The BitScrambler cannot run an equaliser, so this is not a
  profile or tuning choice. Open-field ground reflections have nanosecond
  delays (flat fading, sections 7 and 9), so these echoes come from
  buildings and trees.
- **DC of 1 cell** hurts wide VTXs at 4 dB (picture 0.36). The measured
  uncorrected DC is ~0.13 cell, and the per-gain DC hold covers it.
- **WIDE vs DEFAULT on a deviation-1.7 VTX:** equal or better in every
  situation (+2 dB SINAD at 8 dB), so the WIDE profile stays.

## 12. Post-detection low-pass in the TX BitScrambler (`u85q_lp.py`)

FM detection noise rises with the video frequency, and the colour
subcarrier sits where it is largest. An ideal 3 MHz low-pass after the
detector (a soft black-and-white picture) moves the usable point by ~2 dB.
The question: does any such filter fit next to U85Q in the TX BitScrambler?

Constraints (ESP-IDF 6.0.2 `bsasm.py`, esp32c5 target): two bundles per
50 ns, both LUT16 lookups used by U85Q, one opcode per bundle (U85Q uses
`nop`, so both are free). In LUT16 mode counter B overlaps the LUT mux
region, so only counter A can be routed next to LUT bits. The counter
operand is output[31:16], shared with the LUT address output[25:16].

- A sliding box filter needs subtraction or a delay line: neither fits.
- Integrate-and-dump boxes need two readable counters, so they do not fit
  either. A box of 4 at 5 MS/s aliases.
- **What fits: a one-pole IIR with pole 1/2**, y <- (y >> 1) + (x >> 1), on
  the U85Q DAC code x. The controller does `LDCTIA`, the worker does
  `ADDCTIAH`, y lives in A[15:10], and the address bits below act as dither.
  - Same LUT, same two bundles, same 20 MS/s [y, y] output, one span of
    latency.
  - The controller carries the phase and x >> 1 to the worker in output
    bits 6..15. Bits 6 and 7 of the written byte drive no pin on the XIAO
    DAC; an 8-bit custom-board output would see them.

Evidence. `u85q_lp.py check` runs a small emulator on the ESP-IDF
assembler's own parse:
- it reproduces U85Q bit-exactly against `sim.u85q_codes`, which checks the
  emulator itself;
- it reproduces the generated `v5/main/u85q_lp.bsasm` bit-exactly against
  the arithmetic model, with 0 mismatches.

CI assembles the program for esp32c5 and runs the check. Board timing,
FIFO throughput and the goggle colour killer are not tested.

Picture model: `data/lowpass.json`, 4 seeds, 12 MHz filter. The range
limit is the worse of goggle lock (90 % of lines) and usable (luma SINAD
0 dB).

| deviation | | U85Q | U85Q-LP | change |
| --- | --- | ---: | ---: | ---: |
| 1.0 | usable | 2.4 dB | 0.6 dB | -1.7 dB |
| 1.0 | range limit | 2.4 dB | 0.8 dB (lock) | **-1.6 dB, ~1.2x** |
| 1.4 | usable | 2.1 dB | 0.8 dB | -1.3 dB |
| 1.4 | range limit | 2.1 dB | 1.7 dB (lock) | -0.4 dB |
| 1.0 / 1.4 | SINAD at 20 dB C/N | 12.5 / 13.9 dB | 10.3 / 10.8 dB | softer, less colour |

- **Crossover:** the low-pass wins below ~12 dB C/N at deviation 1.0 and
  below ~9 dB at deviation 1.4. Above that it costs sharpness.
- **Wide VTXs** become lock-limited, so most of their gain is lost.
- **Firmware selection is an open decision.** Program changes are refused
  while TX is live (operator rule 2026-10-11), and an in-flight switch on a
  C/N estimate would interrupt the picture for a few ms. The firmware does
  not select U85Q-LP.
- Pole 1/2 attenuates PAL chroma by only ~6 dB. Whether a goggle then drops
  colour is untested.

**Negative result: a filter that switches itself inside one program.** The
idea was to have one program with an LP loop and a bypass loop, and switch
between them on a mode bit in the LUT (taking x's LSB) that the CPU rewrites
live.
- The LP loop needs both opcodes, so it can only test the mode bit in a
  check span every 4 spans (8-instruction limit).
- In that span the IIR cannot update: it can only hold y or reset y = x.
- Arithmetic model, deviation 1.0, luma SINAD at 4 dB C/N (U85Q 2.40,
  U85Q-LP 5.09):
  - hold: 3.72 at 4 dB, and 7.86 instead of 12.5 at 20 dB;
  - reset: 3.10 at 4 dB.
- Both lose most of the gain; the 5 MHz check pattern injects noise.
- A 5-bit x on its own costs nothing (2.37 vs 2.40).

A dynamic low-pass therefore means a CPU-side program switch on the C/N
estimate, with hysteresis, or switching in the analog domain.

## 13. Seamless live kp (`kp_morph.py`, `main/kp_ladder.h`)

The U85Q loop gain kp is a dynamic setting already. `u85q.bsasm`,
`u85q_kp060` and `u85q_kp050` are the same program with different LUTs.
- The token bits of every word (the raw -> phase observation) are
  identical at every kp, and the 32 phase states mean the same angles. Only
  the (phase, token) -> (next phase, DAC) half differs: 352 to 824 of 1024
  entries.
- So every entry is valid in either table. A table in the middle of a
  rewrite is a valid tracker with a mixed kp, and the tracker state carries
  over. Nothing stops, flushes or resyncs.
- The C5 LUT port writes one entry per register write in the execution
  width (`lut_mode` 1, `lut_idx` = entry). `bitscrambler_load_lut()` is
  avoided: it switches the running program to a 32-bit LUT while it
  writes.

Evidence:
- `kp_morph.py check`: the generator (the C5VRX-4 untrained formula of
  PR #190) reproduces `data/tracker_luts.json` and all three `.bsasm` LUTs
  bit-exactly. Every ladder level (kp 0.85 .. 0.50 in 0.05 steps,
  `main/kp_tables.h`) keeps the token bits.
- `kp_morph.py study` (`data/kp_morph.json`, 4 seeds, C/N 4 / 8 / 20 dB)
  starts a live change mid-field: 0.85 -> 0.6, 0.6 -> 0.85, 0.85 -> 0.7 and
  0.7 -> 0.6, at 1 entry per us and 1 per 10 us.
  - The video error within 0.2 ms of the change stays at the error of the
    worse steady table, 0.70-1.04x on average.
  - The worst case is 1.16x, a full 0.6 -> 0.85 jump at 20 dB with the fast
    writer.
  - Goggle sync is never worse than steady.
  - The firmware takes one 0.05 step at a time, which is smaller than these
    jumps.

Firmware, off by default:
- `K` switches the ladder to AUTO (NVS `kp_auto`). The ladder maps the
  C/N estimate to a level: 12 dB and up gives kp 0.85, 4 dB and down gives
  the deepest level allowed by the deviation measured this session.
  - Allowed depth by deviation: up to 0.80 -> kp 0.50, up to 1.05 -> 0.60,
    up to 1.15 -> 0.70; wider or unknown stays at 0.85.
  - It takes one level per step, after 1 s of agreement, with at least
    0.5 s between steps. `test/test_kp_ladder.c` checks this, including no
    hopping under +-3 dB of estimate noise.
- `transport_kp_tick` writes up to 64 entries per 50 ms control window and
  reads each one back. `k` steps one level by hand for the bench test.
- Active only with the U85Q program (DEFAULT/WIDE profiles). On a WIDE
  VTX it stays at kp 0.85.

Expected effect at deviation 1.0 with the 12 MHz filter
(`data/matched.txt`): the range limit goes from 2.38 to 1.68 dB at kp 0.6,
with no loss close in (kp 0.85 there). Simulation only.

Bench gate before AUTO: run `k` steps under live video and watch `T`.
`kp_verify_fail` and `tx_empty` / `bs_overload` must stay 0 and the
picture must not flicker. Whether a CPU LUT write can collide with a
BitScrambler read is unknown until then.

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
python u85q_lp.py study data/lowpass.json      # U85Q vs U85Q-LP (section 12)
IDF_PATH=<esp-idf> python u85q_lp.py check     # emulator: U85Q and U85Q-LP bit-exact
```

Needs numpy, scipy and numba. `data/tracker_luts.json` holds the kp variant
LUTs (generated with the C5VRX-4 untrained generator of PR #190).

## Diagnostic cliff follow-up (PR #196, 2026-10-11)

Operator clarification: the image becomes static abruptly; the analog feed
remains, with no goggles NO SIGNAL indication. This does not establish whether
RF/FM threshold, tracking, filter/CFO or CVBS/goggle decoding is responsible.
The passive diagnostic fix separates all-window AFC sync evidence from
measurement coverage and conditional sync. It adds bounded timestamped pre/post
onset records at the existing nominal 50-ms control cadence, not extra live DSP.
These metrics cannot measure recognisable picture/luma quality or resolve every
sub-50-ms fade. Unknown evidence stays UNKNOWN; FAST_EVIDENCE_LOSS is not a
propagation diagnosis. C/N, noise figure and distance from the simulations are
model assumptions, not independently measured RF sensitivity. Identify the
actual supplied antenna before assigning it a band or loss.

See `v5/docs/FIELD_TEST.md` for repeated 0.5–1-dB conducted attenuation sweeps,
independent IQ/video/output thresholds, real-IQ floating/bit-exact replay and
outdoor antenna/height A/B/A. RF offset readback, analog BW/passband and DC
validity across profiles still require bench measurements. No measured range
gain or physical picture-cliff fix is claimed by the diagnostic code.
