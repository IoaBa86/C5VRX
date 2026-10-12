/* C5VRX-5 by Twotoz and the C5VRX contributors: sampling-phase verdict
 * (pure logic, host test test/test_sphase.c).
 *
 * PARLIO RX and MODEM_DIAG run from the same PLL with a phase fixed at
 * reset; a read that lands on a MODEM_DIAG transition mixes old and new bits
 * (zerowidth/C5VRX PR #3). The fourth-difference excess (dsp.h) rises for
 * such reads, but also for intrinsic broadband noise: at the table-maximum
 * gain without a carrier, or with DAC/TX self-noise in another TX context,
 * the cleanest position itself can read above the absolute limit.
 * Board 2026-10-12: boots settled while repeated explicit scans reported
 * `unsettled` (PR #196 CVT_STATUS.md).
 *
 * The positions repeat every three RX ticks, so a scan over nine slips
 * sees every position three times. A mid-transition read is only evidence
 * when the positions differ: the scan's worst-best contrast must exceed
 * twice the margin. Without contrast the excess is intrinsic noise, every
 * position is equivalent and slipping cannot help: NOISE_LIMITED, no repair.
 * With contrast, the current position must lie within the margin of the
 * best one; the absolute limit remains the fast path that needs no scan. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SPHASE_LIMIT       300   /* milli-cell^2, absolute excess (dsp.h) */
#define SPHASE_NORM_LIMIT  600   /* per mille of white HF4/HF2 (experimental gate) */

typedef enum { SPHASE_SETTLED, SPHASE_NOISE_LIMITED, SPHASE_UNSETTLED } sphase_verdict_t;

typedef struct {
    bool valid;          /* a scan measured at least two positions */
    unsigned positions;
    int best, worst;     /* excess over the scanned positions */
} sphase_scan_t;

static inline int sphase_margin(int best)
{
    int m = best / 4;
    return m > 100 ? m : 100;
}

/* The scanned positions differ by more than measurement scatter. */
static inline bool sphase_contrast(const sphase_scan_t *s)
{
    return s->valid && s->worst - s->best > 2 * sphase_margin(s->best);
}

static inline void sphase_scan_reset(sphase_scan_t *s)
{
    s->valid = false;
    s->positions = 0;
    s->best = 0x7fffffff;
    s->worst = -1;
}

static inline void sphase_scan_add(sphase_scan_t *s, int excess)
{
    if (excess < s->best) s->best = excess;
    if (excess > s->worst) s->worst = excess;
    s->valid = ++s->positions >= 2u;
}

static inline sphase_verdict_t sphase_classify(int excess, int limit, const sphase_scan_t *s)
{
    if (excess < limit) return SPHASE_SETTLED;
    if (!s || !s->valid) return SPHASE_UNSETTLED;
    if (!sphase_contrast(s)) return SPHASE_NOISE_LIMITED;
    return excess <= s->best + sphase_margin(s->best) ? SPHASE_SETTLED : SPHASE_UNSETTLED;
}

static inline const char *sphase_verdict_name(sphase_verdict_t v)
{
    return v == SPHASE_SETTLED ? "settled" : v == SPHASE_NOISE_LIMITED ? "noise_limited" : "unsettled";
}

/* Fourth-difference energy as per mille of white noise: a white sequence
 * has HF4/HF2 = 70/6 (sums of squared [1,-4,6,-4,1] and [1,-2,1] weights);
 * band-limited IQ reads far lower, mid-transition reads move toward white.
 * Independent of the absolute noise level. */
static inline int sphase_white_permille(uint64_t d4, uint64_t d2)
{
    if (!d2) return 0;
    double r = ((double)d4 / 70.0) / ((double)d2 / 6.0);
    return (int)(1000.0 * r + 0.5);
}
