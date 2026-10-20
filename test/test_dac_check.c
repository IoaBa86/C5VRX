/* C5VRX-5 by Twotoz and the C5VRX contributors: DAC self-test model. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dac_check.h"

/* Floating-point reference for the conductance divider. */
static double reference_mv(unsigned code, double vdd_mv)
{
    double on = 0.0, total = 1.0 / DAC_CHECK_LOAD_OHMS;
    for (unsigned k = 0; k < DAC_CHECK_BITS; ++k) {
        total += 1.0 / dac_check_ohms[k];
        if (code & (1u << k)) on += 1.0 / dac_check_ohms[k];
    }
    return vdd_mv * on / total;
}

int main(void)
{
    /* Steps: pattern, D4..D9, all, none, and back to the pattern. */
    dac_step_t s = DAC_STEP_PATTERN;
    const char *names[DAC_STEPS] = {"PATTERN", "D4", "D5", "D6", "D7", "D8", "D9", "ALL", "NONE"};
    const unsigned codes[DAC_STEPS] = {0, 1, 2, 4, 8, 16, 32, 63, 0};
    for (unsigned k = 0; k < DAC_STEPS; ++k) {
        assert(!strcmp(dac_check_step_name(s), names[k]));
        assert(dac_check_code(s) == codes[k]);
        s = dac_check_next(s);
    }
    assert(s == DAC_STEP_PATTERN);

    /* Expected levels: every code against the reference, within rounding. */
    for (unsigned code = 0; code < 64u; ++code)
        for (int vdd = 3000; vdd <= 3600; vdd += 100)
            assert(fabs(dac_check_expected_mv((uint8_t)code, vdd) - reference_mv(code, vdd)) <= 0.5 + 1e-9);
    /* The published table (README, docs/hardware/dac-build-and-test.md). */
    const int table[DAC_CHECK_BITS] = {31, 64, 125, 251, 533, 1044};
    for (unsigned k = 0; k < DAC_CHECK_BITS; ++k)
        assert(dac_check_expected_mv((uint8_t)(1u << k), DAC_CHECK_VDD_MV) == table[k]);
    assert(dac_check_expected_mv(63, DAC_CHECK_VDD_MV) == 2047);
    assert(dac_check_expected_mv(0, DAC_CHECK_VDD_MV) == 0);
    /* Superposition: the single bits add up to the full code (+-3 rounding). */
    int sum = 0;
    for (unsigned k = 0; k < DAC_CHECK_BITS; ++k) sum += dac_check_expected_mv((uint8_t)(1u << k), DAC_CHECK_VDD_MV);
    assert(sum - dac_check_expected_mv(63, DAC_CHECK_VDD_MV) <= 3 && dac_check_expected_mv(63, DAC_CHECK_VDD_MV) - sum <= 3);
    /* Each bit is roughly twice the one below: the network is binary. */
    for (unsigned k = 1; k < DAC_CHECK_BITS; ++k) {
        double ratio = (double)dac_check_expected_mv((uint8_t)(1u << k), DAC_CHECK_VDD_MV) /
                       dac_check_expected_mv((uint8_t)(1u << (k - 1u)), DAC_CHECK_VDD_MV);
        assert(ratio > 1.9 && ratio < 2.2);
    }

    /* Ramp: monotonic, every level from blanking to the top code, ends exact. */
    const unsigned width = 1597u;
    unsigned seen[64] = {0};
    uint8_t prev = 0;
    for (unsigned x = 0; x < width; ++x) {
        uint8_t c = dac_check_ramp_code(x, width);
        assert(c >= prev && c >= DAC_CHECK_RAMP_LOW && c <= DAC_CHECK_RAMP_HIGH);
        ++seen[c];
        prev = c;
    }
    assert(dac_check_ramp_code(0, width) == DAC_CHECK_RAMP_LOW);
    assert(dac_check_ramp_code(width - 1u, width) == DAC_CHECK_RAMP_HIGH);
    assert(dac_check_ramp_code(width + 50u, width) == DAC_CHECK_RAMP_HIGH);
    for (unsigned c = DAC_CHECK_RAMP_LOW; c <= DAC_CHECK_RAMP_HIGH; ++c) assert(seen[c] >= 30u);   /* ~0.9 us each */
    /* Every bit toggles somewhere on the ramp. */
    for (unsigned k = 0; k < DAC_CHECK_BITS; ++k) {
        int toggles = 0;
        for (unsigned c = DAC_CHECK_RAMP_LOW; c < DAC_CHECK_RAMP_HIGH; ++c) toggles += ((c ^ (c + 1u)) >> k) & 1u;
        assert(toggles > 0);
    }

    /* Bars: the halves differ in exactly their own bit, never below blanking. */
    for (unsigned k = 0; k < DAC_CHECK_BITS; ++k) {
        uint8_t base = dac_check_bar_base(k), top = dac_check_bar_top(k);
        assert((base ^ top) == (1u << k) && top > base);
        assert(base >= DAC_CHECK_RAMP_LOW && top <= DAC_CHECK_RAMP_HIGH);
    }
    puts("PASS: steps, expected levels and table, superposition, binary weights, ramp, bars");
    return 0;
}
