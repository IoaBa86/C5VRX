/* C5VRX-5 by Twotoz and the C5VRX contributors: pure deviation estimator
 * and profile choice of profile.c (host test: v5/test/test_profile.c).
 *
 * The AFC gives, per burst-confirmed window, the sync tip and porch
 * frequency; their distance is deviation x 1.914 MHz (40 of 140 IRE over
 * 6.7 MHz). A decision needs three batches of 32 estimates, each with a
 * spread (MAD) <= 8 % of its median, and all batch medians within 5 % of
 * each other. A batch that fails restarts the count, so a burst of bad
 * windows can delay a decision but never shape it.
 *
 * Profiles: DEFAULT (normal >= 12 MHz class, kp 0.85), KP060 / KP050
 * (narrow >= 10 MHz class, calmer tracker) for low deviations, WIDE (wide
 * >= 16 MHz class, kp 0.85) for deviations >= 1.6. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DEV_SYNC_KHZ     1914
#define DEV_BATCH        32u
#define DEV_BATCHES      3u
#define DEV_MAD_PCT      8
#define DEV_AGREE_PCT    5
/* Profile boundaries (deviation x100) with margin below the simulated
 * break-even points 0.85 / 1.15 (v5/research). */
#define DEV_KP050_MAX    80
#define DEV_KP060_MAX    105
/* Wide VTXs: the 12 MHz default clips their FM spectrum. Range limit,
 * bit-exact U85Q (v5/research/data/wide.txt): deviation 1.7 3.64 dB at
 * 12 MHz vs 3.26 at 16; deviation 2.0 8.03 vs 5.27; deviation 1.4 still
 * best at 12 (2.08 vs 2.54). Break-even ~1.55; WIDE from 1.60. A wrong WIDE
 * on a narrow VTX costs ~0.5-0.8 dB, far less than a wrong narrow profile
 * on a wide one, so the highest-batch rule (errs wide) is kept. */
#define DEV_WIDE_MIN     160

/* Values are stable (NVS-independent, but logged and compared). */
enum { PROFILE_DEFAULT, PROFILE_KP060, PROFILE_KP050, PROFILE_WIDE, PROFILES };

typedef struct {
    int32_t span[DEV_BATCH], medians[DEV_BATCHES];
    unsigned n, batches;
    int result_x100;       /* -1 until decided */
    unsigned rejected;     /* batches dropped for spread or disagreement */
} dev_estimator_t;

static inline void dev_reset(dev_estimator_t *e)
{
    memset(e, 0, sizeof(*e));
    e->result_x100 = -1;
}

static inline int32_t dev_median(int32_t *v, unsigned n)
{
    for (unsigned a = 1; a < n; ++a)
        for (unsigned b = a; b > 0 && v[b - 1] > v[b]; --b) { int32_t t = v[b]; v[b] = v[b - 1]; v[b - 1] = t; }
    return n & 1u ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

/* One sync-to-porch span (kHz, > 0). Returns the deviation x100 once
 * decided, else -1. */
static inline int dev_add(dev_estimator_t *e, int32_t span_khz)
{
    if (e->result_x100 >= 0) return e->result_x100;
    if (span_khz <= 0) return -1;
    e->span[e->n++] = span_khz;
    if (e->n < DEV_BATCH) return -1;
    e->n = 0;
    int32_t v[DEV_BATCH], d[DEV_BATCH];
    memcpy(v, e->span, sizeof(v));
    int32_t med = dev_median(v, DEV_BATCH);
    for (unsigned k = 0; k < DEV_BATCH; ++k) d[k] = abs(e->span[k] - med);
    int32_t mad = dev_median(d, DEV_BATCH);
    if (mad * 100 > med * DEV_MAD_PCT) { e->batches = 0; ++e->rejected; return -1; }
    e->medians[e->batches++] = med;
    if (e->batches < DEV_BATCHES) return -1;
    int32_t lo = e->medians[0], hi = e->medians[0];
    for (unsigned k = 1; k < DEV_BATCHES; ++k) {
        if (e->medians[k] < lo) lo = e->medians[k];
        if (e->medians[k] > hi) hi = e->medians[k];
    }
    if ((hi - lo) * 100 > lo * DEV_AGREE_PCT) { e->batches = 0; ++e->rejected; return -1; }
    /* The highest batch: errs toward the wide (safe) profile. */
    e->result_x100 = (int)(hi * 100 / DEV_SYNC_KHZ);
    return e->result_x100;
}

/* Profile for a measured deviation; anything unknown, or a profile whose
 * filter class was not calibrated, keeps DEFAULT. */
static inline int profile_class(int dev_x100, bool narrow_filter_known, bool wide_filter_known)
{
    if (dev_x100 <= 0) return PROFILE_DEFAULT;
    if (dev_x100 >= DEV_WIDE_MIN) return wide_filter_known ? PROFILE_WIDE : PROFILE_DEFAULT;
    if (!narrow_filter_known) return PROFILE_DEFAULT;
    return dev_x100 <= DEV_KP050_MAX ? PROFILE_KP050 : dev_x100 <= DEV_KP060_MAX ? PROFILE_KP060 : PROFILE_DEFAULT;
}
