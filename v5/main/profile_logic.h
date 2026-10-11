/* C5VRX-5 by Twotoz and the C5VRX contributors: pure deviation estimator
 * and profile choice of profile.c (host test: v5/test/test_profile.c).
 *
 * The AFC gives, per burst-confirmed window, the sync tip and porch
 * frequency; their distance is deviation x 1.914 MHz (40 of 140 IRE over
 * 6.7 MHz). A decision needs three batches of 32 estimates, each with a
 * spread (MAD) <= 8 % of its median, and all batch medians within 5 % of
 * each other. A batch that fails restarts the count, so a burst of bad
 * windows can delay a decision but never shape it. */
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

enum { PROFILE_DEFAULT, PROFILE_KP060, PROFILE_KP050, PROFILES };

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

/* Profile for a measured deviation; anything unknown keeps DEFAULT. */
static inline int profile_class(int dev_x100, bool narrow_filter_known)
{
    if (dev_x100 <= 0 || !narrow_filter_known) return PROFILE_DEFAULT;
    return dev_x100 <= DEV_KP050_MAX ? PROFILE_KP050 : dev_x100 <= DEV_KP060_MAX ? PROFILE_KP060 : PROFILE_DEFAULT;
}
