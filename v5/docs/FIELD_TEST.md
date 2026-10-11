# C5VRX-5 field and range test protocol

C5VRX-5 by Twotoz and the C5VRX contributors. This is the test protocol for PR #196
(operator report, 2026-10-11): there is much less static than before, but
usable range has barely increased. Video was good indoors through walls but
poor in an open field. Fewer visible artifacts are not the same as a lower
usable-video C/N threshold. This protocol separates five things:
propagation, antenna, receiver sensitivity, chain configuration, and a real
demodulator cliff. Firmware only observes, so the tests take no risk with
flight video.

## What the firmware reports

- **`D`: RANGE_DIAG health report.** It shows:
  - channel, plus a warning above 5885 MHz (R8, E6-E8 are outside
    Espressif's documented C5 range);
  - the requested and read-back filter, the filter class and the
    calibration result;
  - the active profile with its reason, plus the pending and provisional
    deviation;
  - AFC state;
  - the gain tuple (index, RF stage/code, BB, fine, top-stage start,
    at-max);
  - sampling phase, and whether DC is held for the current gain;
  - IQ statistics and transport faults;
  - verdict lines and the dropouts of this boot.
- **`R`: one `FIELD_DIAG` line per second.** Each line holds: picture share
  (AFC-measured windows with burst-confirmed sync), gain/max/RF stage,
  observer p50/coherence/origin/clip, AFC offset, and chain flags.
- **`FIELD_DIAG_EVENT`** is printed at every dropout, with no command
  needed. It gives the evidence category and the record of the last second
  with a picture next to the first second without one.
  - `CALIBRATION_OR_PHY`: filter not read back, sampling phase unsettled,
    no DC for the gain in use, or a DMA/BitScrambler fault.
  - `POSSIBLE_PROPAGATION_FADE`: the carrier had >= 6 gain steps of
    headroom and was gone within ~2 s.
  - `AT_RF_FLOOR`: already at the gain maximum with a weak carrier, then
    noise-like IQ.
  - `DEMOD_CLIFF`: the IQ is still coherent and carrier-like, but sync is
    lost.
  - `UNKNOWN`: none of the above fits.

These categories are hypotheses for the next test. They are not a measured
cause, not dBm, and not range.

## Record every run

Write these down; the firmware cannot infer them: distance, RX and TX
antenna type and polarisation (linear/RHCP/LHCP), antenna heights, VTX
power and channel, whether line of sight is really clear, and the firmware
build. Capture the serial log with `R` on, then compare runs:

```
python3 v5/tools/field_diag_compare.py run_a.log run_b.log run_a2.log
```

Repeat each comparison A/B/A (or ABBA) so that drift and fading average out.

## Steps

1. **Chain sanity before anything else.** Press `D` with the VTX on and
   check:
   - filter `applied=1` with class normal or narrow;
   - `sphase=settled`, `dc_ok=1`, faults not increasing.

   If any of these fails, fix it first, by rebooting with the VTX off or
   using CALIBRATE on the ground. Comparing RF setups is meaningless
   otherwise.
2. **Indoor vs outdoor at the same distance.** Use the same VTX, power,
   channel and antennas.
3. **Antenna height (ground reflection / Fresnel).** Outdoors, keep the VTX
   still and run: receiver at normal height, then raised 1-2 m, then normal
   again. Also move the receiver sideways 0.25-1 m.
   - A large, repeatable change with height or position points to a
     spatial null.
   - The fix is placement and clearance, and later spatial diversity with a
     second receive path. A different LUT cannot remove a null.
4. **Antenna A/B.** Compare the stock XIAO antenna with a known-good 5.8 GHz
   antenna of matched polarisation, in the same place. Linear vs circular
   polarisation alone costs about 3 dB, and RHCP vs LHCP costs much more.
5. **Channel A/B.** Compare a channel at or below 5885 MHz (e.g. 5.6-5.8
   GHz) with an upper channel above it.
6. **Conducted attenuation (the only dB result).** Connect the VTX through
   enough fixed attenuation and a stepped attenuator into the receiver
   using 50-ohm connections. Never connect a VTX directly.
   - For each firmware (C5VRX-3 main, C5VRX-5 default, PR #196 matched) and
     a reference receiver, find the attenuation where the picture stops
     being usable.
   - Use the same objective criterion every time: `picture_pct`, plus your
     own view of the picture.
   - Randomise the order and repeat each setting.
7. **IQ lane bench test (optional, on the ground).** `L` compares raw IQ on
   the ultrafine, fine and coarse lanes at the same gain, filter and
   channel (ABBA order, video stops ~3 s).
   - These are raw statistics only: U85Q is made for ultrafine.
   - Production stays ultrafine unless another lane reproducibly wins on
     the conducted attenuation threshold without losing picture quality.

## Reading the result

- **Height or position changes the picture** → propagation (ground
  reflection, Fresnel zone). Fix placement; long term, use antenna
  diversity.
- **Same loss in the conducted test, independent of height** → receiver
  sensitivity or configuration. Look at the `D` gain tuple at the edge,
  and compare raw IQ (`I` snapshots) with a reference receiver.
- **Dropouts are mostly `DEMOD_CLIFF`** (coherent IQ, no sync) → a
  demodulator, AFC or sync problem that IQ captures can show. Only this
  case justifies more DSP work.
- **Dropouts are mostly `AT_RF_FLOOR`** → the signal is below the receiver
  noise. Improve the RF link (antenna, height, polarisation, VTX power),
  not the LUT.
- **The other antenna fixes it** → antenna mismatch, polarisation, or a
  connector problem.

Claim a range improvement only from a reproducible conducted attenuation
margin, or from a lower dropout rate under comparable logged conditions.
