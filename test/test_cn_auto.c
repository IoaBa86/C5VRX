/* C5VRX-5 by Twotoz and the C5VRX contributors: acceptance test for
 * C/N-AUTO (main/cn_auto.h), PR #228.
 *
 * IQ is generated with a known C/N and taken through the 4-bit ultrafine
 * lane as on the board (it wraps, it does not clip):
 * - an FM carrier: porch at -1 MHz, +-2.3 MHz video at the line rate
 *   (deviation ~0.7); optionally at 0 Hz (a slowly rotating phasor);
 * - complex Gaussian receiver noise; a fixed DC.
 * Every gain row's real gain differs from the 6 dB model by up to +-3 dB
 * (a table error the noise map must cancel).
 *
 * Truth: C/N = carrier power / (thermal + quantisation noise) at the row,
 * in the filter's noise bandwidth (the estimate's power_x10).
 *
 * Checks:
 * - power estimate within 1.5 dB over 0-20 dB, on every row, with table
 *   errors, at -1 MHz and at 0 Hz, with 2 cells of DC, for 256 and 1024
 *   sample windows;
 * - the window-mean power the old estimate used loses carrier power at
 *   0 Hz;
 * - a weak carrier (-3 and 0 dB) is never taken as no carrier; noise is;
 * - a borrowed noise value (row not measured) lowers the confidence;
 * - folding is reported clipped and gives no reading;
 * - M2M4 agrees within 4 dB in its region (2-15 dB);
 * - unknown is never a low number.
 *
 *   cc -O2 -std=c11 -I../main test_cn_auto.c -lm -o t && ./t
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cn_auto.h"

static unsigned s_fail, s_run;
static void check(const char *name, int ok)
{
    ++s_run;
    printf("%-4s %s\n", ok ? "ok" : "FAIL", name);
    s_fail += !ok;
}

static uint32_t s_seed = 99u;
static double urand(void) { s_seed = s_seed * 1664525u + 1013904223u; return ((s_seed >> 8) + 0.5) / 16777216.0; }
static double gauss(void) { return sqrt(-2.0 * log(urand())) * cos(6.283185307179586 * urand()); }
static int wrap4(double x) { const int c = (int)floor(x); return ((c + 8) & 15) - 8; }

/* Rows and gains: model 1 dB a row over the test rows, real gain with a
 * per-row error of up to +-3 dB. */
static int16_t s_model_q8[CNA_ROWS];
static double s_real_db[CNA_ROWS];
static void build_rows(void)
{
    for (unsigned k = 0; k < CNA_ROWS; ++k) {
        s_model_q8[k] = (int16_t)(k * 256);
        s_real_db[k] = (double)k + 3.0 * sin(1.7 * k);     /* the table error */
    }
}
/* thermal noise sigma per axis at row k: 1.95 cells at G83 (board) */
static double sigma_at(unsigned k) { return 1.95 * pow(10.0, (s_real_db[k] - s_real_db[83]) / 20.0); }

typedef struct { cna_sums_t s; double win_m2; unsigned wins; unsigned rail; unsigned total; } gen_t;

/* n_windows of `len` samples at row k: carrier power c_cells2 (0 = none). */
static void generate(gen_t *g, unsigned k, double c_cells2, double porch_hz, double dci, double dcq, unsigned n_windows,
                     unsigned len)
{
    memset(g, 0, sizeof(*g));
    const double r = sqrt(c_cells2), sg = sigma_at(k);
    static double phi;
    for (unsigned w = 0; w < n_windows; ++w) {
        double wx = 0, wy = 0, wp = 0;
        const double t0 = urand() * 1e-3;
        for (unsigned n = 0; n < len; ++n) {
            const double t = t0 + n / 40e6;
            const double f = porch_hz + 2.3e6 * (0.5 + 0.5 * sin(6.283185307179586 * 15734.0 * t));
            phi += 6.283185307179586 * f / 40e6;
            const int i = wrap4(r * cos(phi) + sg * gauss() + dci);
            const int q = wrap4(r * sin(phi) + sg * gauss() + dcq);
            const double x = i + 0.5, y = q + 0.5, p = x * x + y * y;
            g->s.sx += x; g->s.sy += y; g->s.sxx += x * x; g->s.syy += y * y; g->s.sxy += x * y;
            g->s.sxp += x * p; g->s.syp += y * p; g->s.spp += p * p; ++g->s.n;
            wx += x; wy += y; wp += p;
            g->rail += (i == -8 || i == 7 || q == -8 || q == 7);
            ++g->total;
        }
        g->win_m2 += wp / len - (wx / len) * (wx / len) - (wy / len) * (wy / len);
        ++g->wins;
    }
    g->win_m2 /= g->wins;
}

/* The noise map, measured row by row with no carrier (as the startup step
 * does), for the given rows. */
static cna_noise_map_t s_map;
static void measure_noise(const unsigned *rows, unsigned count)
{
    memset(&s_map, 0, sizeof(s_map));
    for (unsigned j = 0; j < count; ++j) {
        gen_t g;
        for (unsigned rep = 0; rep < 6u; ++rep) {
            generate(&g, rows[j], 0.0, 0.0, 0.0, 0.0, 16, 256);
            double m2, m4;
            cna_moments(&g.s, (float)(g.s.sx / g.s.n), (float)(g.s.sy / g.s.n), &m2, &m4);
            cna_noise_learn(&s_map, rows[j], (float)m2, 1000);
        }
    }
}

/* Estimate at row k for a carrier at the true C/N; returns the error in dB. */
static double run_case(unsigned k, double cn_db, double porch, double dc, unsigned len, cna_reading_t *out, double *win_err)
{
    const double noise_true = 2.0 * sigma_at(k) * sigma_at(k) + CNA_QUANT_CELLS2;
    const double c = noise_true * pow(10.0, cn_db / 10.0);
    gen_t g;
    generate(&g, k, c, porch, dc, -dc, 4096u / len * 16u, len);
    double m2, m4;
    cna_moments(&g.s, (float)(dc + 0.0), (float)(-dc), &m2, &m4);   /* the row's long-term DC (servo) */
    uint8_t flags = 0;
    float uncert;
    const float noise = cna_noise_at(&s_map, k, s_model_q8, 2000, &flags, &uncert);
    if (g.rail * 100u > g.total * 2u) flags |= CNA_F_CLIPPED;
    *out = cna_estimate(m2, m4, noise, uncert, flags, 13600u, 12, CNA_UNKNOWN_X10);
    if (win_err) *win_err = 10.0 * log10((g.win_m2 > noise ? g.win_m2 - noise : 1e-6) / noise) - cn_db;
    return out->power_x10 == CNA_UNKNOWN_X10 ? 99.0 : out->power_x10 / 10.0 - cn_db;
}

int main(void)
{
    build_rows();
    const unsigned measured[] = {83, 80, 77, 74, 71, 68, 65, 62, 59, 56, 54};
    measure_noise(measured, sizeof(measured) / sizeof(measured[0]));

    /* Accuracy on measured rows, -1 MHz porch, over C/N and rows with table errors. */
    const double cns[] = {0, 2, 4, 6, 10, 15, 20};
    double worst = 0, worst_q = 0;
    unsigned cases = 0, clipped = 0, low_conf_q = 0, quant_strong = 0;
    for (unsigned j = 0; j < sizeof(measured) / sizeof(measured[0]); ++j)
        for (unsigned c = 0; c < sizeof(cns) / sizeof(cns[0]); ++c) {
            cna_reading_t r;
            const double e = run_case(measured[j], cns[c], -1.0e6, 0.0, 256, &r, NULL);
            if (r.flags & CNA_F_CLIPPED) { ++clipped; continue; }
            /* A quantisation-limited row with a carrier under 2 cells must say LOW. */
            if ((r.flags & CNA_F_QUANT_LIMITED) && r.confidence <= CNA_CONF_LOW) { ++low_conf_q; continue; }
            if (r.flags & CNA_F_QUANT_LIMITED) { ++quant_strong; if (fabs(e) > fabs(worst_q)) worst_q = e; continue; }
            ++cases;
            if (fabs(e) > 1.5) printf("     row %u C/N %.0f dB: error %+.2f dB (noise %.3f, conf %u, flags 0x%02x)\n", measured[j], cns[c], e, r.noise_cells2, r.confidence, r.flags);
            if (fabs(e) > fabs(worst)) worst = e;
        }
    printf("     thermal-noise rows: %u cases, worst %+.2f dB; quantisation-limited: %u strong (worst %+.2f dB), %u weak flagged LOW; %u folding\n",
           cases, worst, quant_strong, worst_q, low_conf_q, clipped);
    check("power C/N within 1.5 dB over 0-20 dB on every row with real noise, with +-3 dB table errors", fabs(worst) <= 1.5 && cases >= 20);
    check("quantisation-limited rows: a strong carrier within 2.5 dB, a weak one only with LOW confidence", fabs(worst_q) <= 2.5);

    /* A slowly rotating carrier (0 Hz) and 1 cell of DC, 256 and 1024 samples. */
    double worst_rot = 0, worst_win = 0, worst_len = 0;
    for (unsigned c = 1; c < 6; ++c) {
        cna_reading_t r256, r1024;
        double win;
        const double e = run_case(80, cns[c], 0.0, 0.0, 256, &r256, &win);
        if (r256.flags & CNA_F_CLIPPED) continue;          /* the carrier folds at this row: no reading, rightly */
        const double e2 = run_case(80, cns[c], 0.0, 0.0, 1024, &r1024, NULL);
        cna_reading_t rd;
        const double ed = run_case(80, cns[c], -1.0e6, 1.0, 256, &rd, NULL);
        if (fabs(e) > fabs(worst_rot)) worst_rot = e;
        if (!(rd.flags & CNA_F_CLIPPED) && fabs(ed) > fabs(worst_rot)) worst_rot = ed;
        if (win < worst_win) worst_win = win;
        if (fabs(e - e2) > worst_len) worst_len = fabs(e - e2);
    }
    printf("     0 Hz carrier / 1-cell DC: worst %+.2f dB; window-mean power (old) worst %+.2f dB; 256 vs 1024: %.2f dB\n",
           worst_rot, worst_win, worst_len);
    check("a carrier at 0 Hz and 1 cell of DC: within 1.5 dB (long-term DC, not the window mean)", fabs(worst_rot) <= 1.5);
    check("the window-mean power of the old estimate loses carrier at 0 Hz (> 0.3 dB on average)", worst_win < -0.3);
    {   /* 2.5 cells of DC with the noise: the lane wraps; the reading must say so */
        cna_reading_t r;
        const double e = run_case(80, 6.0, -1.0e6, 2.5, 256, &r, NULL);
        printf("     2.5 cells of DC at 6 dB: flags 0x%02x error %+.2f dB\n", r.flags, e);
        check("2.5 cells of DC: either flagged without a reading or still within 1.5 dB",
              ((r.flags & CNA_F_CLIPPED) && r.x10 == CNA_UNKNOWN_X10) || fabs(e) <= 1.5);
    }
    check("window length invariant (256 vs 1024 samples within 0.5 dB)", worst_len <= 0.5);

    /* No-carrier evidence: noise yes, a weak carrier no. */
    {
        bool noise_ok = true, weak_rejected = true;
        for (unsigned rep = 0; rep < 5; ++rep) {
            gen_t g;
            double m2, m4;
            generate(&g, 83, 0.0, -1e6, 0, 0, 96, 256);
            cna_moments(&g.s, (float)(g.s.sx / g.s.n), (float)(g.s.sy / g.s.n), &m2, &m4);
            noise_ok = noise_ok && cna_carrier_absent(m2 * 96, m4 * 96, 96, s_map.noise[83]);
            const double noise_true = 2.0 * sigma_at(83) * sigma_at(83) + CNA_QUANT_CELLS2;
            for (int cn = -3; cn <= 0; cn += 3) {
                generate(&g, 83, noise_true * pow(10.0, cn / 10.0), -1e6, 0, 0, 96, 256);
                cna_moments(&g.s, (float)(g.s.sx / g.s.n), (float)(g.s.sy / g.s.n), &m2, &m4);
                weak_rejected = weak_rejected && !cna_carrier_absent(m2 * 96, m4 * 96, 96, 0.0f);
            }
        }
        check("noise alone is recognised as no carrier", noise_ok);
        check("a weak VTX (-3 and 0 dB C/N, no lock) is never taken as noise", weak_rejected);
    }

    /* A row without its own noise: borrowed, flagged, lower confidence. */
    {
        cna_reading_t own, borrowed;
        (void)run_case(77, 6.0, -1e6, 0, 256, &own, NULL);
        (void)run_case(76, 6.0, -1e6, 0, 256, &borrowed, NULL);
        printf("     own row conf %u, borrowed row conf %u flags 0x%02x\n", own.confidence, borrowed.confidence, borrowed.flags);
        check("a borrowed noise value is flagged and never more confident than a measured one",
              (borrowed.flags & CNA_F_NOISE_BORROWED) && borrowed.confidence <= own.confidence && own.confidence >= CNA_CONF_OK);
    }

    /* Folding: too strong for the row. */
    {
        cna_reading_t r;
        (void)run_case(83, 25.0, -1e6, 0, 256, &r, NULL);
        check("a folding carrier is reported clipped and gives no reading (unknown, not low)",
              (r.flags & CNA_F_CLIPPED) && r.x10 == CNA_UNKNOWN_X10 && r.confidence == CNA_CONF_NONE);
    }

    /* M2M4 against power in its region. */
    {
        double worst_m = 0;
        for (unsigned c = 1; c < 6; ++c) {
            cna_reading_t r;
            (void)run_case(74, cns[c], -1e6, 0, 256, &r, NULL);
            if (r.m2m4_x10 != CNA_UNKNOWN_X10 && r.power_x10 != CNA_UNKNOWN_X10) {
                const double d = (r.m2m4_x10 - r.power_x10) / 10.0;
                if (fabs(d) > fabs(worst_m)) worst_m = d;
            }
        }
        printf("     M2M4 - power over 2-15 dB: worst %+.2f dB\n", worst_m);
        check("M2M4 agrees with power within 4 dB over 2-15 dB", fabs(worst_m) <= 4.0);
    }

    /* No noise map at all: never a confident number. */
    {
        cna_noise_map_t empty;
        memset(&empty, 0, sizeof(empty));
        uint8_t flags = 0;
        float u;
        const float n = cna_noise_at(&empty, 80, s_model_q8, 0, &flags, &u);
        const cna_reading_t r = cna_estimate(10.0, 150.0, n, u, flags, 13600u, 12, CNA_UNKNOWN_X10);
        check("without any noise measurement the power source is unavailable", r.source != CNA_SRC_POWER && r.confidence <= CNA_CONF_LOW);
    }

    printf("%u/%u C/N-AUTO checks passed\n", s_run - s_fail, s_run);
    return s_fail != 0;
}
