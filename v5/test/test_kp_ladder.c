/* C5VRX-5 by Twotoz and the C5VRX contributors: host test of the seamless
 * kp ladder policy (main/kp_ladder.h) and of the LUT tables it moves
 * between (main/kp_tables.h).
 *
 *   cc -O2 -std=c11 -I../main test_kp_ladder.c -o t && ./t
 */
#include <stdio.h>
#include <stdlib.h>
#include "kp_ladder.h"
#include "kp_tables.h"

static unsigned s_fail, s_run;
static void check(const char *name, int ok)
{
    ++s_run;
    printf("%-4s %s\n", ok ? "ok" : "FAIL", name);
    s_fail += !ok;
}

#define TICK_US 50000LL

/* Runs `seconds` of control windows at a C/N (x10) with estimate noise. */
static uint8_t run(kp_ladder_t *l, int cn_x10, int noise_x10, int dev, double seconds, int64_t *t, unsigned *changes)
{
    uint8_t last = l->level;
    for (int64_t end = *t + (int64_t)(seconds * 1e6); *t < end; *t += TICK_US) {
        int cn = cn_x10 + (noise_x10 ? rand() % (2 * noise_x10 + 1) - noise_x10 : 0);
        uint8_t lv = kp_ladder_update(l, cn, true, dev, *t);
        if (changes && lv != last) ++*changes;
        last = lv;
    }
    return l->level;
}

int main(void)
{
    char name[128];
    /* Tables: same token bits at every level, level 0 differs from 1. */
    int tokens_equal = 1, differ = 0;
    for (int lv = 1; lv < KP_LEVELS; ++lv)
        for (int k = 0; k < 1024; ++k) {
            tokens_equal &= (kp_tables[lv][k] >> 11) == (kp_tables[0][k] >> 11);
            differ += kp_tables[lv][k] != kp_tables[0][k];
        }
    check("every level keeps the token bits (seamless entry-by-entry change)", tokens_equal);
    check("levels differ in the tracker half", differ > 0);
    check("ladder and tables agree on the level count", KP_LEVELS == KP_LADDER_LEVELS);

    kp_ladder_t l;
    int64_t t = 0;
    /* Strong carrier: stays sharp. */
    kp_ladder_reset(&l);
    check("strong carrier, normal VTX: kp 0.85", run(&l, 200, 15, 100, 20, &t, NULL) == 0);
    /* Range edge, normal VTX: walks down to kp 0.60 one level at a time. */
    unsigned changes = 0;
    uint8_t lv = run(&l, 30, 15, 100, 20, &t, &changes);
    snprintf(name, sizeof(name), "range edge, deviation 1.0: kp 0.%02u in %u single steps", kp_level_x100[lv], changes);
    check(name, lv == 5 && changes == 5);
    /* Back close in: returns to sharp. */
    check("carrier strong again: back to kp 0.85", run(&l, 200, 15, 100, 20, &t, NULL) == 0);
    /* Low-deviation VTX may go to kp 0.50; wide VTX never leaves 0.85. */
    check("deviation 0.69 at the edge: kp 0.50", run(&l, 20, 10, 69, 20, &t, NULL) == 7);
    kp_ladder_reset(&l);
    check("deviation 1.4 at the edge: stays kp 0.85", run(&l, 20, 10, 140, 20, &t, NULL) == 0);
    check("deviation unknown at the edge: stays kp 0.85", run(&l, 20, 10, 0, 20, &t, NULL) == 0);
    /* Deviation becomes unknown (channel change) while calm: back at once. */
    kp_ladder_reset(&l);
    (void)run(&l, 30, 0, 100, 20, &t, NULL);
    lv = kp_ladder_update(&l, 30, true, 0, t);
    check("deviation lost while calm: level 0 immediately", lv == 0);
    /* No hopping: C/N noise of +-3 dB around a midpoint never moves more than
     * one level away, and changes are rare. */
    kp_ladder_reset(&l);
    (void)run(&l, 80, 0, 100, 10, &t, NULL);
    uint8_t mid = l.level;
    changes = 0;
    uint8_t lo = mid, hi = mid;
    for (int k = 0; k < 1200; ++k, t += TICK_US) {     /* 60 s */
        uint8_t v = kp_ladder_update(&l, 80 + rand() % 61 - 30, true, 100, t);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    snprintf(name, sizeof(name), "C/N 8 dB +-3 dB noise for 60 s: level %u..%u around %u", lo, hi, mid);
    check(name, hi - lo <= 2);
    /* Noise-like windows hold the level. */
    kp_ladder_reset(&l);
    (void)run(&l, 30, 0, 100, 20, &t, NULL);
    uint8_t held = l.level;
    for (int k = 0; k < 200; ++k, t += TICK_US) (void)kp_ladder_update(&l, 999, false, 100, t);
    check("carrier gone (noise-like windows): level held", l.level == held);
    /* Step spacing: never two steps closer than the gap. */
    kp_ladder_reset(&l);
    int64_t last = -KP_STEP_GAP_US, min_gap = 1LL << 62;
    uint8_t prev = 0;
    for (int k = 0; k < 600; ++k, t += TICK_US) {
        uint8_t v = kp_ladder_update(&l, k < 300 ? 20 : 200, true, 69, t);
        if (v != prev) { if (t - last < min_gap) min_gap = t - last; last = t; prev = v; }
    }
    check("steps at least KP_STEP_GAP_US apart", min_gap >= KP_STEP_GAP_US);
    printf("%s: %u checks, %u failures\n", s_fail ? "FAIL" : "PASS", s_run, s_fail);
    return s_fail ? 1 : 0;
}
