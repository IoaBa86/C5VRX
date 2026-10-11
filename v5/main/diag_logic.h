/* C5VRX-5 by Twotoz and the C5VRX contributors: pure part of the passive
 * field diagnostics (diag.c; host test v5/test/test_diag.c).
 *
 * diag.c summarises each second of live reception from statistics the
 * receiver already computes (control-window AFC result, gain observer)
 * into one record. When the picture drops out, the records just before
 * and at the dropout are classified into an evidence category. The
 * categories are hypotheses for the operator, not a proven physical
 * cause, and none of them is a calibrated dBm or range figure:
 *
 *  CALIBRATION_OR_PHY        the chain itself was not in its intended state
 *                            (filter not applied, sampling phase unsettled,
 *                            no DC correction for the gain in use, DMA or
 *                            BitScrambler faults);
 *  POSSIBLE_PROPAGATION_FADE the carrier had clear analog-gain headroom and
 *                            disappeared within ~2 s, faster than distance
 *                            explains (ground-reflection null, body or
 *                            antenna shadowing, polarisation);
 *  AT_RF_FLOOR               already at the gain table maximum with a weak
 *                            carrier before the dropout, and noise-like IQ
 *                            at it: the signal sank into the receiver noise;
 *  DEMOD_CLIFF               the IQ still has carrier-like phase coherence
 *                            while the picture is lost: tracker, AFC or sync
 *                            loss rather than missing RF;
 *  UNKNOWN                   none of the above fits. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    DIAG_UNKNOWN,
    DIAG_CALIBRATION_OR_PHY,
    DIAG_PROPAGATION_FADE,
    DIAG_AT_RF_FLOOR,
    DIAG_DEMOD_CLIFF,
    DIAG_CATEGORIES
};

static inline const char *diag_category_name(int c)
{
    static const char *const names[DIAG_CATEGORIES] = {"UNKNOWN", "CALIBRATION_OR_PHY",
        "POSSIBLE_PROPAGATION_FADE", "AT_RF_FLOOR", "DEMOD_CLIFF"};
    return c >= 0 && c < DIAG_CATEGORIES ? names[c] : "?";
}

/* Chain-state flags of a record; any of them set at a dropout means
 * CALIBRATION_OR_PHY. */
#define DIAG_F_FILTER_MISMATCH 0x01u   /* requested filter not read back */
#define DIAG_F_SPHASE_BAD      0x02u   /* sampling phase not settled */
#define DIAG_F_NO_DC           0x04u   /* no DC correction held for the gain */
#define DIAG_F_FAULT           0x08u   /* new DMA/BitScrambler fault */
#define DIAG_F_CHAIN           0x0fu
/* Informational, not a chain fault. */
#define DIAG_F_AFC_LOCKED      0x10u
#define DIAG_F_OUT_OF_SPEC     0x20u   /* channel above the documented C5 range */

typedef struct {
    uint32_t t_s;           /* uptime, s */
    uint8_t video_pct;      /* AFC-measured windows with burst-confirmed sync, % */
    uint8_t gain, gain_max; /* mean gain index of the second, table maximum */
    uint8_t rf_stage;       /* vendor RF stage of the gain */
    int16_t p50;            /* observer envelope median (ultrafine cells^2 x16) */
    int16_t coherence;      /* observer phase coherence, % */
    int16_t origin_pm, clip_pm;
    int16_t offset_khz;     /* AFC tuning offset */
    uint8_t flags;
} diag_record_t;

/* Thresholds (observer coherence: receiver noise at the table maximum
 * reads 36..61 %, a usable carrier >= 80 %, firmware-exact simulation of
 * PR #196). */
#define DIAG_VIDEO_OK_PCT     50u   /* a second with a picture */
#define DIAG_VIDEO_LOST_PCT   5u    /* a second without (no confirmed sync) */
#define DIAG_NOISE_COHERENCE  65    /* at or below: noise-like */
#define DIAG_GOOD_COHERENCE   80
#define DIAG_FADE_HEADROOM    6u    /* gain steps below the maximum */

static inline bool diag_video_ok(const diag_record_t *r) { return r->video_pct >= DIAG_VIDEO_OK_PCT; }
static inline bool diag_video_lost(const diag_record_t *r) { return r->video_pct <= DIAG_VIDEO_LOST_PCT; }

/* rec[0..n-1] oldest first; rec[n-1] is the first second without a
 * picture. Needs at least one second with a picture before it. */
static inline int diag_classify(const diag_record_t *rec, unsigned n)
{
    if (n < 2u) return DIAG_UNKNOWN;
    const diag_record_t *drop = &rec[n - 1u];
    int last_ok = -1;
    for (int k = (int)n - 2; k >= 0; --k)
        if (diag_video_ok(&rec[k])) { last_ok = k; break; }
    if (last_ok < 0) return DIAG_UNKNOWN;
    const diag_record_t *ok = &rec[last_ok];
    /* Chain state in the seconds from the last picture to the dropout. */
    for (unsigned k = (unsigned)last_ok; k < n; ++k)
        if (rec[k].flags & DIAG_F_CHAIN) return DIAG_CALIBRATION_OR_PHY;
    /* Picture lost within ~2 s while the gain still had headroom: the
     * carrier fell faster than any distance change explains. */
    bool fast = (int)n - 1 - last_ok <= 2;
    if (fast && ok->gain + DIAG_FADE_HEADROOM <= ok->gain_max) return DIAG_PROPAGATION_FADE;
    bool at_max_before = ok->gain + 1u >= ok->gain_max;
    if (at_max_before && ok->coherence < DIAG_GOOD_COHERENCE + 10 && drop->coherence <= DIAG_NOISE_COHERENCE)
        return DIAG_AT_RF_FLOOR;
    if (drop->coherence >= DIAG_GOOD_COHERENCE) return DIAG_DEMOD_CLIFF;
    return DIAG_UNKNOWN;
}
