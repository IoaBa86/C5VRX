/* C5VRX-5 by Twotoz and the C5VRX contributors: Direct Gain on a moving
 * carrier (multipath fading), host model, actual dg_tick().
 *
 * Board blackbox 2026-10-19 (session 13, VTX on, handheld): close in, at a
 * C/N estimate of 15-20 dB, the gain swung over 10-30 steps within a second
 * with 255+ writes and overloads a second, and sync fell to 60-80 % (95 % in
 * the calm seconds); at the range edge the gain sat at G66-G81 instead of
 * the table maximum (1-2 dB in research/sensitivity_model.py). This drives
 * the real controller with the receiver model of test_gain_fade.c and a
 * Rician fading carrier (sum-of-sinusoids Jakes model) and reports writes a
 * second, the share of time with a usable picture (radius 2.5-7.5 cells
 * with the noise inside the window), clipped time, and the gain at the edge.
 *
 *   cc -O2 -std=c11 -I../main test_gain_track.c ../main/gain.c ../main/arc_phy.c -lm -o t && ./t
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gain.h"

unsigned char phy_param[2048];

#define TICK_US 200u
static direct_gain_t g;
static arc_gain_table_t table;
static double s_db[DG_STATES];
static unsigned s_fail, s_run;

static void check(const char *name, int ok)
{
    ++s_run;
    printf("%-4s %s\n", ok ? "ok" : "FAIL", name);
    s_fail += !ok;
}

static void build_model(void)
{
    arc_gain_table_from_bytes(&table, NULL, ARC_5G_GAIN_MAX);
    for (unsigned k = 0; k <= table.max_index; ++k) {
        arc_gain_tuple_t t;
        arc_gain_tuple_decode(&table, (uint8_t)k, &t);
        s_db[k] = 6.0 * t.rf_stage + 6.0 * log2(t.bb_code + 1.0) + 0.83 * (5 - (int)t.fine_code);
    }
}

static double radius(double level_db, unsigned k) { return level_db <= -200 ? 0 : pow(10.0, (level_db + s_db[k]) / 20.0); }
static double noise_radius(unsigned k) { return 1.95 * sqrt(2.0) * pow(10.0, (s_db[k] - s_db[table.max_index]) / 20.0); }

/* The receiver model of test_gain_fade.c (noise 1.95 cells per axis at the
 * table maximum, board 2026-10-19). */
static dg_observation_t observe(double level_db, unsigned k, uint64_t t)
{
    dg_observation_t o;
    memset(&o, 0, sizeof(o));
    o.observed_us = t;
    double r = radius(level_db, k), rn = noise_radius(k);
    double p50 = r * r + 0.69 * rn * rn;
    bool folded = r + rn > 7.5;
    o.p50 = (uint8_t)(p50 > 113 ? 113 : p50);
    /* Gaussian receiver noise has an exponential |z|^2 (P95/P50 = ln 20 /
     * ln 2 ~ 4.3), a carrier a constant one: board, G83, no VTX: P50 5-7,
     * P95 15-19. */
    const double p90 = r * r + 2.3 * rn * rn, p95 = r * r + 3.0 * rn * rn;
    o.p90 = (uint8_t)(p90 > 113 ? 113 : p90);
    o.p95 = (uint8_t)(p95 > 113 ? 113 : p95);
    o.clip_pm = (uint16_t)(folded ? fmin(1000.0, 150.0 * (r + rn) / 7.5) : (r + 2 * rn > 7.5 ? 10 : 0));
    o.rails = o.clip_pm ? 15u : 0u;          /* a carrier circle reaches every rail */
    double spread = r > 0.5 ? 0 : rn;
    o.origin_pm = (uint16_t)(r > 2.0 ? 20 : 1000.0 * exp(-(spread * spread + r * r) / 1.5));
    o.coherence = (uint8_t)(folded ? 30 : r > 2.5 ? 95 : r > 1.2 ? 70 : rn > 1.0 ? 45 : 10);
    o.span = 10;
    o.repeat_pm = 50;
    return o;
}

/* Picture SNR of one moment: carrier power over thermal plus quantisation
 * noise (a uniform 1-cell step: 1/12 per axis), 0 when the carrier folds out
 * of the 4-bit window. A radius of ~1.3 cells still gives 10 dB close in,
 * where the thermal noise is tiny; a fold destroys the sample. */
static double snr_db(double level_db, unsigned k)
{
    const double r = radius(level_db, k), rn = noise_radius(k);
    if (r + rn > 7.5) return -99.0;
    return 10.0 * log10(r * r / (rn * rn + 1.0 / 6.0) + 1e-9);
}
static bool usable(double level_db, unsigned k) { return snr_db(level_db, k) >= 10.0; }

/* Rician fading: a line-of-sight part and a Jakes multipath part. */
#define PATHS 12
static double s_phase[PATHS], s_cos[PATHS];
static uint32_t s_seed = 7;
static double urand(void) { s_seed = s_seed * 1664525u + 1013904223u; return (s_seed >> 8) / 16777216.0; }
static void fading_init(void)
{
    for (int p = 0; p < PATHS; ++p) { s_phase[p] = 6.2831853 * urand(); s_cos[p] = cos(6.2831853 * urand()); }
}
static double fading_db(uint64_t t_us, double doppler_hz, double k_factor)
{
    double re = 0, im = 0;
    for (int p = 0; p < PATHS; ++p) {
        const double a = 6.2831853 * doppler_hz * s_cos[p] * (double)t_us * 1e-6 + s_phase[p];
        re += cos(a); im += sin(a);
    }
    re /= sqrt((double)PATHS); im /= sqrt((double)PATHS);     /* unit mean power */
    const double los = sqrt(k_factor);
    const double h2 = ((los + re) * (los + re) + im * im) / (k_factor + 1.0);
    return 10.0 * log10(h2 > 1e-6 ? h2 : 1e-6);
}

typedef struct { double writes_s, usable_pct, clipped_pct, mean_gain, at_max_pct, threshold_pct; } result_t;

/* Mean level (dB, receiver model) with fading for `seconds`, after 1 s of
 * settling on the same conditions. */
static result_t run(double mean_db, double doppler, double k_factor, double seconds)
{
    dg_reset(&g, &table, table.max_index);
    g.learn_enabled = true;
    fading_init();
    uint64_t t = 1000;
    unsigned applied = g.current_gain;
    for (unsigned n = 0; n < 5000; ++n, t += TICK_US) {
        dg_observation_t o = observe(mean_db + fading_db(t, doppler, k_factor), applied, t);
        uint8_t target = dg_tick(&g, &o);
        if (target != applied) { applied = target; dg_sync_applied(&g, target, t); }
    }
    const uint32_t w0 = g.writes;
    unsigned ticks = 0, ok = 0, clipped = 0, at_max = 0, above = 0;
    uint64_t last_write = 0;
    double gain_sum = 0;
    for (; ticks < (unsigned)(seconds * 1e6 / TICK_US); ++ticks, t += TICK_US) {
        const double lv = mean_db + fading_db(t, doppler, k_factor);
        dg_observation_t o = observe(lv, applied, t);
        uint8_t target = dg_tick(&g, &o);
        if (target != applied) { applied = target; dg_sync_applied(&g, target, t); last_write = t; }
        /* A gain write is a PHY transient: ~0.4 ms of samples are garbage
         * (settle guard; every write a short transient, PR #177). */
        ok += usable(lv, applied) && (!last_write || t - last_write >= 400u);
        clipped += snr_db(lv, applied) < -90.0;
        above += snr_db(lv, applied) >= 3.0 && (!last_write || t - last_write >= 400u);
        at_max += applied == table.max_index;
        gain_sum += applied;
    }
    return (result_t){(g.writes - w0) / seconds, 100.0 * ok / ticks, 100.0 * clipped / ticks, gain_sum / ticks,
                      100.0 * at_max / ticks, 100.0 * above / ticks};
}

static void show(const char *name, result_t r)
{
    printf("     %-44s writes %7.1f/s  SNR>=10dB %5.1f%%  >=3dB %5.1f%%  clipped %5.1f%%  gain %5.1f  at max %5.1f%%\n",
           name, r.writes_s, r.usable_pct, r.threshold_pct, r.clipped_pct, r.mean_gain, r.at_max_pct);
    printf("R,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n", r.writes_s, r.usable_pct, r.threshold_pct, r.clipped_pct, r.mean_gain,
           r.at_max_pct);
}

int main(void)
{
    build_model();
    /* Levels as the carrier radius it would have at G83 (cells); the noise
     * radius there is 2.76 cells. */
    const double close_db = 20.0 * log10(4.5) - s_db[28];     /* healthy around G28, as on the board */
    const double mid_db = 20.0 * log10(4.5) - s_db[60];
    const double edge_db = 20.0 * log10(2.8) - s_db[table.max_index];   /* ~C/N 0 dB at the maximum */
    const result_t close_still = run(close_db, 0.5, 20.0, 5.0);
    const result_t close_hand = run(close_db, 30.0, 3.0, 5.0);
    const result_t close_rayleigh = run(close_db, 30.0, 0.0, 5.0);
    const result_t mid_hand = run(mid_db, 30.0, 3.0, 5.0);
    const result_t edge_hand = run(edge_db, 30.0, 3.0, 5.0);
    /* The usable edge: ~3 dB C/N at the table maximum, moving. */
    const result_t edge3 = run(20.0 * log10(3.9) - s_db[table.max_index], 30.0, 3.0, 5.0);
    show("close, steady (K 20, 0.5 Hz)", close_still);
    show("close, moving (K 3, 30 Hz)", close_hand);
    show("close, no line of sight (Rayleigh, 30 Hz)", close_rayleigh);
    show("mid range, moving (K 3, 30 Hz)", mid_hand);
    show("range edge, moving (K 3, 30 Hz)", edge_hand);
    show("usable edge ~3 dB C/N, moving", edge3);
    /* V7 against the V6 baseline of the same model (V6: 93.5 / 87.3 / 94.5 %
     * at >= 10 dB, 38.3 % above the threshold at the usable edge, 112-125
     * writes a second on a moving carrier). */
    check("steady carrier: no gain hunting (< 5 writes/s)", close_still.writes_s < 5.0);
    check("moving carrier: under half of V6's writes", close_hand.writes_s < 60.0 && close_rayleigh.writes_s < 60.0 &&
          mid_hand.writes_s < 60.0);
    check("moving carrier close in: picture SNR >= 10 dB at least as often as V6", close_hand.usable_pct >= 93.5);
    check("Rayleigh fading: within a point of V6", close_rayleigh.usable_pct >= 86.3);
    check("mid range: at least as often as V6", mid_hand.usable_pct >= 94.3);
    check("clipped <= 2 % of the time", close_hand.clipped_pct <= 2.0 && close_rayleigh.clipped_pct <= 2.0);
    check("usable edge: above the threshold at least as often as V6", edge3.threshold_pct >= 38.3);
    printf("%u/%u gain tracking checks passed\n", s_run - s_fail, s_run);
    return s_fail != 0;
}
