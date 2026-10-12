/* C5VRX-5 by Twotoz and the C5VRX contributors: seamless kp ladder policy.
 *
 * The U85Q tracker loop gain kp sets the video bandwidth: a calmer tracker
 * (lower kp) extends the FM threshold at the range edge; on a strong carrier
 * kp 0.75-0.80 is slightly better than 0.85 too (v5/research README section
 * 14). The ladder picks a level (kp_tables.h) from the C/N estimate and the
 * deviation class measured this session; transport.c moves the live LUT to
 * that level entry by entry, so nothing stops or resyncs.
 *
 * - The deep levels only for a VTX whose deviation was measured this
 *   session: a calm tracker on a wide VTX loses the picture. Unknown or wide
 *   deviation uses the wide row (kp 0.85 near the floor, 0.80 above).
 * - One level per step, after the target has held for KP_HOLD_US, and at
 *   most one step per KP_STEP_GAP_US, so estimate noise does not hop between
 *   levels. Toward a sharper kp (lower level) the hold and gap are shorter:
 *   near the floor a calm tracker loses lock first, so a collapsing carrier
 *   must get back up quickly.
 * - A noise-like window (CN_UNKNOWN_X10: coherence at or below plain noise)
 *   is the floor, not missing evidence: it targets the curve's 0 dB point.
 *   Only an unusable window (retune, gain write, menu) holds the level.
 * The C/N estimate comes from the IQ window coherence, not from the tracker,
 * so a kp change cannot feed back into it. Pure logic: host test
 * test/test_kp_ladder.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cn_estimate.h"

#define KP_LADDER_LEVELS   8           /* kp 0.85 .. 0.50 in 0.05 steps */
#define KP_HOLD_US         1000000LL   /* toward a calmer kp: the target must hold this long */
#define KP_STEP_GAP_US     500000LL    /* ... and at least this between steps */
#define KP_HOLD_UP_US      300000LL    /* toward a sharper kp (better lock near the floor) */
#define KP_STEP_GAP_UP_US  200000LL

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

/* Best kp (x100) per VTX deviation class at C/N 0, 2, 4, 8, 12, 20 dB:
 * v5/research/best_combo.py, 8 seeds, 12 MHz (data/best_combo.json). The
 * best SINAD whose goggle lock stays within 0.02 of the best, kept off the
 * deepest kp near 0-2 dB where a calm tracker loses lock first. kp 0.80
 * beats 0.85 on a strong carrier for every deviation (+0.25..0.9 dB SINAD).
 * Unknown deviation uses the wide row. */
enum { KP_CLASS_LOW, KP_CLASS_NORMAL, KP_CLASS_MID, KP_CLASS_WIDE, KP_CLASSES };
static const int16_t kp_cn_points_x10[6] = {0, 20, 40, 80, 120, 200};
static const uint8_t kp_curve_x100[KP_CLASSES][6] = {
    {70, 55, 50, 50, 50, 75},   /* deviation <= 0.80 */
    {75, 60, 55, 55, 65, 75},   /* <= 1.05 */
    {80, 70, 65, 65, 70, 80},   /* <= 1.15 (interpolated, no own run) */
    {85, 85, 80, 80, 80, 80},   /* wider or unknown */
};

static inline int kp_ladder_class(int dev_x100)
{
    if (dev_x100 <= 0) return KP_CLASS_WIDE;
    if (dev_x100 <= 80) return KP_CLASS_LOW;
    if (dev_x100 <= 105) return KP_CLASS_NORMAL;
    if (dev_x100 <= 115) return KP_CLASS_MID;
    return KP_CLASS_WIDE;
}

/* kp (x100) the curve asks for at a C/N, linear between the points. */
static inline int kp_ladder_curve_x100(int cls, int cn_x10)
{
    const uint8_t *k = kp_curve_x100[cls];
    if (cn_x10 <= kp_cn_points_x10[0]) return k[0];
    for (int i = 1; i < 6; ++i)
        if (cn_x10 <= kp_cn_points_x10[i])
            return k[i - 1] + (k[i] - k[i - 1]) * (cn_x10 - kp_cn_points_x10[i - 1]) /
                              (kp_cn_points_x10[i] - kp_cn_points_x10[i - 1]);
    return k[5];
}

/* Ladder level (0 = kp 0.85, 0.05 per level) nearest to a kp. */
static inline uint8_t kp_ladder_level_for(int kp_x100)
{
    int lv = (85 - kp_x100 + 2) / 5;
    return (uint8_t)(lv < 0 ? 0 : lv >= KP_LADDER_LEVELS ? KP_LADDER_LEVELS - 1 : lv);
}

static inline uint8_t kp_ladder_target(int cn_x10, int dev_x100)
{
    return kp_ladder_level_for(kp_ladder_curve_x100(kp_ladder_class(dev_x100), cn_x10));
}

/* One control window. window_ok false = unusable window (hold); a usable
 * noise-like window passes cn_x10 = CN_UNKNOWN_X10 and means the floor.
 * Returns the level the LUT should move to. */
static inline uint8_t kp_ladder_update(kp_ladder_t *l, int cn_x10, bool window_ok, int dev_x100, int64_t now_us)
{
    /* Deeper than the wide row ever goes (deviation lost on a channel
     * change, or measured wide): back to that row's deepest level at once. */
    const uint8_t safe = kp_ladder_class(dev_x100) == KP_CLASS_WIDE ? kp_ladder_level_for(80) : KP_LADDER_LEVELS - 1;
    if (l->level > safe) {
        l->level = safe;
        l->pending_dir = 0;
        l->last_step_us = now_us;
        ++l->steps;
        return l->level;
    }
    if (!window_ok) { l->pending_dir = 0; return l->level; }
    const uint8_t target = kp_ladder_target(cn_x10 <= CN_UNKNOWN_X10 ? 0 : cn_x10, dev_x100);
    const int8_t dir = target > l->level ? 1 : target < l->level ? -1 : 0;
    if (dir != l->pending_dir) {
        l->pending_dir = dir;
        l->pending_since = now_us;
        return l->level;
    }
    const int64_t hold = dir < 0 ? KP_HOLD_UP_US : KP_HOLD_US, gap = dir < 0 ? KP_STEP_GAP_UP_US : KP_STEP_GAP_US;
    if (dir && now_us - l->pending_since >= hold && now_us - l->last_step_us >= gap) {
        l->level = (uint8_t)(l->level + dir);
        l->last_step_us = now_us;
        ++l->steps;
    }
    return l->level;
}
