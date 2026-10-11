/* C5VRX-5 by Twotoz and the C5VRX contributors: host test of the VTX
 * deviation estimator and profile choice (main/profile_logic.h).
 *
 * The narrow profiles (10 MHz, kp 0.6 / 0.5) hurt a wide VTX badly, so the
 * test tries hard to push wide VTXs into them: estimate noise, outlier
 * windows (gain transitions, multipath, a wrong sync pick), a step in the
 * middle of the measurement. A wide VTX (deviation >= 1.15) must never
 * leave DEFAULT and a medium one (>= 0.95) must never reach kp 0.5;
 * clean narrow VTXs must still get their profile.
 *
 *   cc -O2 -std=c11 -I../main test_profile.c -lm -o test_profile && ./test_profile
 */
#include <math.h>
#include <stdio.h>
#include "profile_logic.h"

static uint64_t s_rng = 0x2545f4914f6cdd1dull;
static double uniform(void)
{
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 7; s_rng ^= s_rng << 17;
    return ((s_rng >> 11) + 0.5) / 9007199254740992.0;
}
static double gauss(void) { return sqrt(-2.0 * log(uniform())) * cos(6.283185307179586 * uniform()); }

typedef struct {
    double dev, noise_pct, outlier_frac, step_to;   /* step_to > 0: deviation changes halfway */
} scenario_t;

/* Feeds spans until a decision or 2000 windows (a startup budget of about
 * 15 s of burst-confirmed AFC windows). Returns the profile, -1 = none. */
static int run(const scenario_t *sc, int *dev_out)
{
    dev_estimator_t e;
    dev_reset(&e);
    for (unsigned w = 0; w < 2000u; ++w) {
        double dev = sc->step_to > 0 && w >= 48u ? sc->step_to : sc->dev;
        double span = dev * DEV_SYNC_KHZ * (1.0 + sc->noise_pct / 100.0 * gauss());
        if (uniform() < sc->outlier_frac) span = 300.0 + 3500.0 * uniform();   /* anything */
        int r = dev_add(&e, (int32_t)span);
        if (r >= 0) { *dev_out = r; return profile_class(r, true); }
    }
    *dev_out = -1;
    return -1;
}

static int s_fail;
static void check(const char *name, bool ok, const char *detail)
{
    printf("%-4s %-58s %s\n", ok ? "ok" : "FAIL", name, detail);
    s_fail += !ok;
}

int main(void)
{
    static const char *const names[] = {"DEFAULT", "KP0.6", "KP0.5"};
    char name[96], detail[96];
    /* Wide and medium VTXs, adversarial conditions, 2000 trials each. */
    const double devs[] = {0.95, 1.0, 1.15, 1.2, 1.4, 1.8};
    const double noises[] = {2.0, 5.0, 8.0, 15.0};
    const double outliers[] = {0.0, 0.1, 0.3};
    for (unsigned a = 0; a < 6u; ++a)
        for (unsigned b = 0; b < 4u; ++b)
            for (unsigned c = 0; c < 3u; ++c) {
                scenario_t sc = {devs[a], noises[b], outliers[c], 0.0};
                unsigned count[3] = {0}, none = 0, worst = PROFILE_DEFAULT;
                for (unsigned t = 0; t < 2000u; ++t) {
                    int dev, p = run(&sc, &dev);
                    if (p < 0) { ++none; continue; }
                    ++count[p];
                    if (p == PROFILE_KP050 || (p == PROFILE_KP060 && worst == PROFILE_DEFAULT)) worst = (unsigned)p;
                }
                bool ok = devs[a] >= 1.15 ? count[PROFILE_KP060] + count[PROFILE_KP050] == 0
                                          : count[PROFILE_KP050] == 0;
                snprintf(name, sizeof(name), "dev=%.2f noise=%2.0f%% outliers=%2.0f%%", devs[a], noises[b],
                         outliers[c] * 100);
                snprintf(detail, sizeof(detail), "default=%u kp06=%u kp05=%u undecided=%u", count[0], count[1],
                         count[2], none);
                check(name, ok, detail);
            }
    /* A VTX that changes mid-measurement (channel neighbour, camera switch)
     * must not yield a mixed, narrower result. */
    for (unsigned t = 0, bad = 0;; ++t) {
        if (t == 2000u) { check("dev 1.40 -> 0.69 halfway: never narrow from mixed data", bad == 0, ""); break; }
        scenario_t sc = {1.4, 3.0, 0.0, 0.69};
        int dev, p = run(&sc, &dev);
        /* A decision must come from the new VTX only (all three batches). */
        if (p >= 0 && dev > 75) ++bad;
    }
    /* Clean narrow VTXs still get their profile. */
    const struct { double dev; int want; } clean[] = {{0.69, PROFILE_KP050}, {0.75, PROFILE_KP050},
                                                     {1.0, PROFILE_KP060}, {1.02, PROFILE_KP060}, {1.4, PROFILE_DEFAULT}};
    for (unsigned a = 0; a < 5u; ++a) {
        unsigned hit = 0;
        for (unsigned t = 0; t < 500u; ++t) {
            scenario_t sc = {clean[a].dev, 3.0, 0.05, 0.0};
            int dev;
            hit += run(&sc, &dev) == clean[a].want;
        }
        snprintf(name, sizeof(name), "clean dev=%.2f -> %s", clean[a].dev, names[clean[a].want]);
        snprintf(detail, sizeof(detail), "%u/500", hit);
        check(name, hit >= 490u, detail);
    }
    /* No narrow filter calibrated: always DEFAULT. */
    check("no narrow filter -> DEFAULT", profile_class(69, false) == PROFILE_DEFAULT, "");
    check("unknown deviation -> DEFAULT", profile_class(0, true) == PROFILE_DEFAULT, "");
    printf("%s (%d failures)\n", s_fail ? "FAILED" : "PASSED", s_fail);
    return s_fail != 0;
}
