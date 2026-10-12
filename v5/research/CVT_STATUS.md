# U85-CVT investigation checkpoint

C5VRX by Twotoz and the C5VRX contributors; extends the U85Q generator
and counter pipeline already recorded in the project research ledger.

The 2026-10-12 search evaluates 17 analytical, immutable transition laws
on 36 identical-Q4 cases (612 results). Layouts use 28+4 angular/origin
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

In progress / still required:

- Bit-exact ESP-IDF assembler/emulator check of all expanded candidates.
- Held-out two-field study: seeds 211..214, dense -2..6 dB and 10/20 dB,
  PAL/NTSC, four deviations, CFO +/-1 MHz, DC, and 1/5/20 ms fades; all eight
  fixed kp values with/without LP and floating same-Q4 reference.
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
- Repeat boots/scans and live phase monitoring, final production build
  with experiments OFF, app-only flash and reopened console.
- Physical moving-video, colour, 75-ohm voltage, HDZero and conducted
  attenuation acceptance remain unavailable while the operator's VTX is OFF.

See `../docs/PR196_TASK_PLAN.md` for the full agreed work and remaining
acceptance criteria. Automatic OUTPUT remains passive research.
