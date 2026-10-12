/* C5VRX-5 by Twotoz and the C5VRX contributors: seamless kp ladder policy.
 *
 * The U85Q tracker loop gain kp sets the video bandwidth: a calmer tracker
 * (lower kp) extends the FM threshold at the range edge and costs sharpness
 * on a strong carrier (v5/research README sections 2 and 13). The ladder
 * picks a level (kp_tables.h) from the C/N estimate: sharp close in, calmer
 * far out. transport.c moves the live LUT to that level entry by entry, so
 * nothing stops or resyncs.
 *
 * - A lower kp only for a VTX whose deviation was measured this session:
 *   a calm tracker on a wide VTX loses the picture (matched.txt). Unknown or
 *   wide deviation keeps level 0 (kp 0.85, the plain U85Q).
 * - One level per step, after the target has held for KP_HOLD_US, and at
 *   most one step per KP_STEP_GAP_US. The C/N band is wide (12 -> 4 dB over
 *   the levels), so estimate noise does not hop between levels.
 * - Noise-like windows (no C/N estimate) hold the level: the carrier is gone
 *   or the window is unusable, and the next carrier is most likely as far.
 * The C/N estimate comes from the IQ window coherence, not from the tracker,
 * so a kp change cannot feed back into it. Pure logic: host test
 * test/test_kp_ladder.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define KP_LADDER_LEVELS   8           /* kp 0.85 .. 0.50 in 0.05 steps */
#define KP_CN_SHARP_X10    120         /* >= 12 dB C/N: level 0 */
#define KP_CN_CALM_X10     40          /* <= 4 dB C/N: the deepest allowed level */
#define KP_HOLD_US         1000000LL   /* the target must hold this long */
#define KP_STEP_GAP_US     500000LL    /* at least this between steps */

typedef struct {
    uint8_t level;          /* current target level, 0 = kp 0.85 */
    int8_t pending_dir;     /* direction of the target seen since pending_since */
    int64_t pending_since;
    int64_t last_step_us;
    uint32_t steps;
} kp_ladder_t;

static inline void kp_ladder_reset(kp_ladder_t *l)
{
    l->level = 0;
    l->pending_dir = 0;
    l->pending_since = 0;
    l->last_step_us = 0;
}

/* Deepest level allowed for a deviation measured this session (x100, <= 0
 * unknown). From the 12 MHz rows of v5/research/data/matched.txt: kp 0.5 is
 * best at deviation 0.69, kp 0.6 at 1.0; a deviation of 1.4 keeps kp 0.85. */
static inline uint8_t kp_ladder_max_level(int dev_x100)
{
    if (dev_x100 <= 0) return 0;
    if (dev_x100 <= 80) return 7;      /* kp 0.50 */
    if (dev_x100 <= 105) return 5;     /* kp 0.60 */
    if (dev_x100 <= 115) return 3;     /* kp 0.70 */
    return 0;
}

/* Level the C/N asks for, linear between the sharp and calm points. */
static inline uint8_t kp_ladder_target(int cn_x10, uint8_t max_level)
{
    if (!max_level || cn_x10 >= KP_CN_SHARP_X10) return 0;
    if (cn_x10 <= KP_CN_CALM_X10) return max_level;
    const int span = KP_CN_SHARP_X10 - KP_CN_CALM_X10;
    return (uint8_t)(((KP_CN_SHARP_X10 - cn_x10) * max_level + span / 2) / span);
}

/* One control window. cn_valid false = noise-like or unusable window.
 * Returns the level the LUT should move to. */
static inline uint8_t kp_ladder_update(kp_ladder_t *l, int cn_x10, bool cn_valid, int dev_x100, int64_t now_us)
{
    const uint8_t max_level = kp_ladder_max_level(dev_x100);
    if (l->level > max_level) {                 /* deviation changed or unknown: back at once */
        l->level = max_level;
        l->pending_dir = 0;
        l->last_step_us = now_us;
        ++l->steps;
        return l->level;
    }
    if (!cn_valid) { l->pending_dir = 0; return l->level; }
    const uint8_t target = kp_ladder_target(cn_x10, max_level);
    const int8_t dir = target > l->level ? 1 : target < l->level ? -1 : 0;
    if (dir != l->pending_dir) {
        l->pending_dir = dir;
        l->pending_since = now_us;
        return l->level;
    }
    if (dir && now_us - l->pending_since >= KP_HOLD_US && now_us - l->last_step_us >= KP_STEP_GAP_US) {
        l->level = (uint8_t)(l->level + dir);
        l->last_step_us = now_us;
        ++l->steps;
    }
    return l->level;
}
