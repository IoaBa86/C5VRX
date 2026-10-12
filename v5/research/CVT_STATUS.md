# U85-CVT investigation checkpoint

C5VRX by Twotoz and the C5VRX contributors; extends the U85Q generator
and counter pipeline already recorded in the project research ledger.

The 2026-10-12 search evaluates 20 analytical, immutable transition laws
on 36 identical-Q4 cases (720 results). Layouts use 28+4 angular/origin
tokens, 24+8 angular/low-radius tokens, or 32 angular tokens with a
phase-error-dependent law. Confidence here is a radius cue, not measured
RF C/N. No CPU per-sample processing or live LUT write is added.

No candidate passes every search gate. The 32-token `sync_bias` candidate
has one lock gate violation and mean -1.04 dB / worst -2.87 dB SINAD versus
the appropriate existing MATCHED baseline. Selecting the fewest violations
does not qualify a candidate for production. Generated CVT and CVT with
fixed LP remain behind `C5VRX5_EXPERIMENT_U85_CVT`, OFF by default. Bench V
requires that build, saves AUTO OFF, and returns to the preceding tracker.

The fixed post-detection LP still consumes both spare opcode slots:
LDCTIA loads the previous half output and ADDCTIAH adds the new half output
every span. Observation-dependent phase gain in a LUT is **not** a variable
post-detection IIR. An exact variable-alpha implementation fitting the
current two bundles has not been established. The previously rejected
periodic LP/bypass filter remains rejected.
See `CVT_HARDWARE.md` for the schedule and the limits of the direct encodings.

Held-out validation is now complete: 160 two-field captures and 3360
same-Q4 results. Plain 24+8 CVT averages -0.89 dB versus MATCHED; plain
32-token CVT averages -1.09 dB. Their fixed-LP alternatives average +0.81
and +0.60 dB on the weak/stress subset, but cost -2.07/-2.54 dB on the
strong subset. Against the eligible fixed-kp/LP oracle they average
-1.11/-1.35 dB. These do not satisfy the proposed universal acceptance.
See `data/cvt_validation_summary.json` and `data/cvt_validation.svg`.

Five further 64-state/16-token variants test the alternative allocation of
the same ten address bits (180 search results). This plain two-bundle
tracker differs from the older four-bundle Phase6 detector. All five have
strong/lock gate violations, with mean -0.94 to -1.66 dB versus MATCHED.
No current fixed-LP implementation is claimed for that six-bit state layout:
the existing controller packs five phase bits and five x/2 bits into the
ten bits left beside y. See `cvt_precision.py` and `data/cvt_precision.json`.

In progress / still required:

- [done] Bit-exact ESP-IDF assembler/emulator check of 20 expanded candidates,
  plus original U85Q/LP whole-stream and both-direction switch checks.
- [done] Held-out two-field study: seeds 211..214, dense -2..6 dB and 10/20 dB,
  PAL/NTSC, four deviations, CFO +/-1 MHz, DC, and 1/5/20 ms fades; all eight
  fixed kp values with/without LP and floating same-Q4 reference.
- [done] Bit-exact check of the five alternative 64-state layouts.
- Further analytical CVT changes only if validation supports them; report
  failures and compare against the fixed-kp/LP oracle separately.
- MATCHED-profile ladder/LP hardware tests; retain the profile RF filter.
- Sampling phase: DC rows now load before scanning and are explicitly
  applied while the gain worker is paused. The phasefix3 board boot settled,
  but one of two subsequent explicit scans did not. The repair is therefore
  **not complete**; startup success alone is insufficient evidence.
  The status-screen exit restarts RX after the old scan and changes the TX
  noise context. The next repair moves final qualification after that exit
  and makes explicit bench scans use the actual live pipeline; retest pending.
  Phasefix4 also failed repeat-scan acceptance. Diagnostic normalized HF
  energy and DC-centre logging were added. Phasefix6 tested a normalized
  gate: boot settled but subsequent scans still failed. That policy is now
  gated by `C5VRX5_EXPERIMENT_SPHASE_NORMALIZED`, OFF by default. Production
  retains the absolute gate; DC loading and final-live scan ordering remain.
- Repeat boots/scans and live phase monitoring, final production build
  with experiments OFF, app-only flash and reopened console.
- Physical moving-video, colour, 75-ohm voltage, HDZero and conducted
  attenuation acceptance remain unavailable while the operator's VTX is OFF.

See `../docs/PR196_TASK_PLAN.md` for the full agreed work and remaining
acceptance criteria. Automatic OUTPUT remains passive research.
