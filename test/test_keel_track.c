/* C5VRX-5 by Twotoz and the C5VRX contributors: KEEL-1 search harness.
 * The real dg_tick() (main/gain.c) on a moving carrier, with what the
 * plain tracking model (test_gain_track.c) leaves out: every gain row has its
 * own DC, and the live servo removes it only while the gain stays on that row.
 *
 * Receiver model (board 2026-10-19):
 * - noise 1.95 cells per axis at G83;
 * - Gaussian noise peaks (P95 ~ 4.3 x P50);
 * - 4-bit window +-7.5 cells;
 * - each gain write costs WRITE_COST_US of picture (blackbox: ~2.5 ms).
 *
 * DC model:
 * - each row starts with a DC of DC0_CELLS (random direction, 0.5-1.5 x);
 *   C5VRX-4 measured ~1.1 cell on uncorrected rows;
 * - the servo shrinks it by e every DC_TAU_US of settled dwell on that row,
 *   down to DC_FLOOR_CELLS;
 * - the DC adds to the noise (a shifted circle demodulates with a phase
 *   error) and to folding (the carrier plus noise plus DC must stay inside
 *   the window).
 * The DC numbers are not measured per row: the search reports how results
 * move with them.
 *
 * Scenarios: close / mid / edge levels, K 0/3/10, 5-80 Hz Doppler, sustained
 * steps, VTX off and on. Score: the share of time with a usable picture,
 * picture SNR >= 10 dB close in and mid, >= 3 dB at the edge.
 *
 *   cc -O2 -std=c11 -I../main test_keel_track.c ../main/gain.c ../main/arc_phy.c -lm -o t && ./t
 *   (-DKEEL_LATTICE_Q8=1536 for 6 dB home rows; -DDC_TAU_US=..., -DDC0_CELLS=...)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gain.h"

unsigned char phy_param[2048];

#define TICK_US 200u
#ifndef WRITE_COST_US
#define WRITE_COST_US 2000u
#endif
#ifndef DC_TAU_US
#define DC_TAU_US 100000.0
#endif
#ifndef DC0_CELLS
#define DC0_CELLS 1.0
#endif
#ifndef DC_FLOOR_CELLS
#define DC_FLOOR_CELLS 0.1
#endif

static direct_gain_t g;
static arc_gain_table_t table;
static double s_db[DG_STATES], s_dc0[DG_STATES], s_dwell[DG_STATES];
static uint32_t s_seed = 7;
static double urand(void) { s_seed = s_seed * 1664525u + 1013904223u; return (s_seed >> 8) / 16777216.0; }

static void build_model(void)
{
    arc_gain_table_from_bytes(&table, NULL, ARC_5G_GAIN_MAX);
    for (unsigned k = 0; k <= table.max_index; ++k) {
        arc_gain_tuple_t t;
        arc_gain_tuple_decode(&table, (uint8_t)k, &t);
        s_db[k] = 6.0 * t.rf_stage + 6.0 * log2(t.bb_code + 1.0) + 0.83 * (5 - (int)t.fine_code);
    }
}
static void dc_reset(uint32_t seed)
{
    s_seed = seed;
    for (unsigned k = 0; k < DG_STATES; ++k) { s_dc0[k] = DC0_CELLS * (0.5 + urand()); s_dwell[k] = 0; }
}
static double dc_cells(unsigned k)
{
    const double d = s_dc0[k] * exp(-s_dwell[k] / DC_TAU_US);
    return d > DC_FLOOR_CELLS ? d : DC_FLOOR_CELLS;
}
static double radius(double level_db, unsigned k) { return level_db <= -200 ? 0 : pow(10.0, (level_db + s_db[k]) / 20.0); }
static double noise_radius(unsigned k) { return 1.95 * sqrt(2.0) * pow(10.0, (s_db[k] - s_db[table.max_index]) / 20.0); }

static dg_observation_t observe(double level_db, unsigned k, uint64_t t)
{
    dg_observation_t o;
    memset(&o, 0, sizeof(o));
    o.observed_us = t;
    const double r = radius(level_db, k), rn = noise_radius(k), d = dc_cells(k);
    const double p50 = r * r + 0.69 * rn * rn, p90 = r * r + 2.3 * rn * rn, p95 = r * r + 3.0 * rn * rn;
    o.p50 = (uint8_t)(p50 > 113 ? 113 : p50);
    o.p90 = (uint8_t)(p90 > 113 ? 113 : p90);
    o.p95 = (uint8_t)(p95 > 113 ? 113 : p95);
    const bool folded = r + rn + d > 7.5;
    const bool dc_touch = !folded && 2.0 * rn + d > 7.5;   /* the cloud pushed against one rail */
    o.clip_pm = (uint16_t)(folded ? fmin(1000.0, 150.0 * (r + rn + d) / 7.5) : dc_touch ? 120 : (r + 2 * rn + d > 7.5 ? 10 : 0));
    o.rails = folded ? 15u : dc_touch ? 5u : 0u;
    const double spread = r > 0.5 ? 0 : rn;
    o.origin_pm = (uint16_t)(r > 2.0 ? 20 : 1000.0 * exp(-(spread * spread + r * r) / 1.5));
    o.coherence = (uint8_t)(folded ? 30 : r > 2.5 ? 95 : r > 1.2 ? 70 : rn > 1.0 ? 45 : 10);
    o.span = 10;
    o.repeat_pm = 50;
    return o;
}

static double snr_db(double level_db, unsigned k)
{
    const double r = radius(level_db, k), rn = noise_radius(k), d = dc_cells(k);
    if (r + rn + d > 7.5) return -99.0;
    return 10.0 * log10(r * r / (rn * rn + 1.0 / 6.0 + d * d) + 1e-9);
}

#define PATHS 12
static double s_phase[PATHS], s_cos[PATHS];
static void fading_init(uint32_t seed)
{
    uint32_t keep = s_seed;
    s_seed = seed;
    for (int p = 0; p < PATHS; ++p) { s_phase[p] = 6.2831853 * urand(); s_cos[p] = cos(6.2831853 * urand()); }
    s_seed = keep;
}
static double fading_db(uint64_t t_us, double doppler_hz, double k_factor)
{
    double re = 0, im = 0;
    for (int p = 0; p < PATHS; ++p) {
        const double a = 6.2831853 * doppler_hz * s_cos[p] * (double)t_us * 1e-6 + s_phase[p];
        re += cos(a); im += sin(a);
    }
    re /= sqrt((double)PATHS); im /= sqrt((double)PATHS);
    const double los = sqrt(k_factor);
    const double h2 = ((los + re) * (los + re) + im * im) / (k_factor + 1.0);
    return 10.0 * log10(h2 > 1e-6 ? h2 : 1e-6);
}

typedef struct {
    const char *name;
    double level_db;          /* mean level, as dB over the G83 noise-free unit radius */
    double doppler, k;
    double step_db;           /* a sustained step half way (0 none, -300 VTX off) */
    double threshold_db;      /* picture SNR counted as usable */
    double weight;
} scenario_t;

typedef struct { double usable_pct, writes_s, mean_dc; } result_t;

static result_t run(const scenario_t *sc, uint32_t seed)
{
    dc_reset(seed);
    fading_init(seed * 31u + 7u);
    dg_reset(&g, &table, table.max_index);
    g.learn_enabled = true;
    uint64_t t = 1000, last_write = 0;
    unsigned applied = g.current_gain;
    const unsigned settle_ticks = 5000u, ticks = 25000u;   /* 1 s settle, 5 s measured */
    unsigned ok = 0;
    uint32_t w0 = 0;
    double dc_sum = 0;
    for (unsigned n = 0; n < settle_ticks + ticks; ++n, t += TICK_US) {
        if (n == settle_ticks) w0 = g.writes;
        double lv = sc->level_db + fading_db(t, sc->doppler, sc->k);
        if (sc->step_db != 0 && n >= settle_ticks + ticks / 2u) lv = sc->step_db <= -300 ? -300 : lv + sc->step_db;
        dg_observation_t o = observe(lv, applied, t);
        const uint8_t target = dg_tick(&g, &o);
        if (target != applied) { applied = target; dg_sync_applied(&g, target, t); last_write = t; }
        else if (!last_write || t - last_write >= 400u) s_dwell[applied] += TICK_US;   /* the servo learns settled rows */
        if (n >= settle_ticks) {
            const bool fresh = !last_write || t - last_write >= WRITE_COST_US;
            ok += fresh && snr_db(lv, applied) >= sc->threshold_db;
            dc_sum += dc_cells(applied);
        }
    }
    return (result_t){100.0 * ok / ticks, (g.writes - w0) / 5.0, dc_sum / ticks};
}

int main(void)
{
    build_model();
    const double top = s_db[table.max_index];
    const double close = 20.0 * log10(4.5) - s_db[28], mid = 20.0 * log10(4.5) - s_db[60];
    const double edge0 = 20.0 * log10(2.8) - top, edge3 = 20.0 * log10(3.9) - top, edge6 = 20.0 * log10(5.5) - top;
    const scenario_t sc[] = {
        {"close, walking (K 10, 5 Hz)", close, 5, 10, 0, 10, 1},
        {"close, moving (K 3, 30 Hz)", close, 30, 3, 0, 10, 1},
        {"close, Rayleigh 30 Hz", close, 30, 0, 0, 10, 1},
        {"close, fast (K 3, 80 Hz)", close, 80, 3, 0, 10, 1},
        {"mid, moving (K 3, 30 Hz)", mid, 30, 3, 0, 10, 1},
        {"mid, Rayleigh 30 Hz", mid, 30, 0, 0, 10, 1},
        {"mid, -12 dB behind an obstacle", mid, 30, 3, -12, 10, 1},
        {"edge 6 dB C/N (K 3, 30 Hz)", edge6, 30, 3, 0, 3, 2},
        {"edge 3 dB C/N (K 3, 30 Hz)", edge3, 30, 3, 0, 3, 2},
        {"edge 3 dB C/N, Rayleigh", edge3, 30, 0, 0, 3, 2},
        {"edge 0 dB C/N (K 3, 30 Hz)", edge0, 30, 3, 0, 3, 2},
        {"edge 3 dB C/N, walking (K 10, 5 Hz)", edge3, 5, 10, 0, 3, 2},
    };
    const unsigned n = sizeof(sc) / sizeof(sc[0]);
    double score = 0, weights = 0, writes = 0, edge_writes = 0, edge3_usable = 0;
    for (unsigned k = 0; k < n; ++k) {
        result_t a = {0}, b;
        for (uint32_t seed = 1; seed <= 3; ++seed) {   /* three DC/fading draws */
            b = run(&sc[k], seed);
            a.usable_pct += b.usable_pct / 3; a.writes_s += b.writes_s / 3; a.mean_dc += b.mean_dc / 3;
        }
        printf("     %-38s usable %5.1f%%  writes %5.1f/s  DC %.2f cells\n", sc[k].name, a.usable_pct, a.writes_s, a.mean_dc);
        printf("S,%u,%.2f,%.2f,%.3f\n", k, a.usable_pct, a.writes_s, a.mean_dc);
        score += sc[k].weight * a.usable_pct; weights += sc[k].weight; writes += a.writes_s / n;
        if (k >= 8u) edge_writes += a.writes_s / (n - 8u);   /* the 3 dB and 0 dB edge scenarios */
        if (k == 8u) edge3_usable = a.usable_pct;
    }
    printf("SCORE %.2f (weighted usable %%; edge counts twice), writes %.1f/s\n", score / weights, writes);
#ifndef NO_CHECKS
    /* KEEL-1 against the sweep (servo 0.1 s): V7 56.1, V6 52.5. */
    unsigned fail = 0;
    if (score / weights < 57.0) { printf("FAIL score %.2f < 57 (V7 56.1)\n", score / weights); ++fail; }
    if (edge_writes > 3.0) { printf("FAIL edge writes %.1f/s > 3\n", edge_writes); ++fail; }
    if (edge3_usable < 39.0) { printf("FAIL edge 3 dB usable %.1f < 39 %%\n", edge3_usable); ++fail; }
    printf("%s: KEEL-1 checks\n", fail ? "FAIL" : "PASS");
    return fail != 0;
#else
    (void)edge_writes; (void)edge3_usable;
    return 0;
#endif
}
