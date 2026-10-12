/* C5VRX-5 by Twotoz and the C5VRX contributors: shared anti-flicker gate. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define KP_SWITCH_GAP_US 5000000LL
#define KP_SWITCH_CONFIRM_US 2000000LL

typedef struct {
    bool switched, candidate;
    int64_t last_switch_us, candidate_since_us;
    int level;
    bool lp;
    uint32_t context;
} kp_switch_guard_t;

static inline bool kp_switch_ready(const kp_switch_guard_t *g, int64_t now)
{
    return !g->switched || now - g->last_switch_us >= KP_SWITCH_GAP_US;
}

static inline void kp_switch_mark(kp_switch_guard_t *g, int64_t now)
{
    g->switched = true;
    g->last_switch_us = now;
}

/* Require an unchanged kp/program pair and context with usable evidence.
 * Invalid windows break confirmation; they never count as stable evidence. */
static inline bool kp_switch_confirm(kp_switch_guard_t *g, int level, bool lp,
                                     uint32_t context, bool valid, int64_t now)
{
    if (!valid) { g->candidate = false; return false; }
    if (!g->candidate || g->level != level || g->lp != lp || g->context != context) {
        g->candidate = true;
        g->level = level;
        g->lp = lp;
        g->context = context;
        g->candidate_since_us = now;
        return false;
    }
    return now - g->candidate_since_us >= KP_SWITCH_CONFIRM_US;
}
