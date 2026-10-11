/* C5VRX-5 by Twotoz and the C5VRX contributors: passive field diagnostics.
 *
 * Nothing here touches the receive chain: no retune, gain, filter, DC,
 * lane or tracker write, and no extra IQ copy. The control task hands over
 * per 50 ms window it already processed whether the AFC could measure it
 * and whether it held burst-confirmed sync; video_pct counts the measured
 * windows only. The gain observer's statistics are read as they are; each
 * second becomes one diag_record_t (diag_logic.h) in a 64-entry ring. When
 * the picture drops out, the seconds before it are classified once. All
 * printing happens in the console task (diag_poll), never in the control
 * task, so a slow or absent USB host cannot stall AFC or the observers.
 *
 *   FIELD_DIAG        one line per second, after console R (stream toggle)
 *   FIELD_DIAG_EVENT  dropout start (category) and end (duration), always
 *   RANGE_DIAG        health report, console D
 *
 * v5/tools/field_diag_compare.py compares two captured logs. */
#include "receiver.h"
#include <stdio.h>
#include <string.h>
#include "diag_logic.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "phy.h"
#include "rf.h"

#define DIAG_HISTORY   64u
#define DIAG_LOOKBACK  8u      /* seconds handed to the classifier */
#define DIAG_EVENTS    4u

typedef struct {
    uint32_t t_s, duration_s;
    int category;
    diag_record_t last_ok, drop;
    bool open;
} diag_event_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static diag_record_t s_hist[DIAG_HISTORY];
static uint32_t s_seq;                  /* records written */
static diag_event_t s_events[DIAG_EVENTS];
static uint32_t s_event_seq;
static uint32_t s_dropouts[DIAG_CATEGORIES];
static volatile bool s_stream;

/* Accumulator of the current second (control task only). */
static struct {
    int64_t start_us;
    unsigned windows, measured, video, gain_sum;
    long coh, p50, origin, clip;
    uint32_t faults;
    bool owned;           /* menu / calibration / scan owned TX or gain */
} s_acc;
static bool s_in_dropout;
static uint32_t s_seen_faults;

static uint8_t chain_flags(uint8_t gain, uint32_t faults)
{
    uint8_t f = 0;
    if (!rf_filter_applied()) f |= DIAG_F_FILTER_MISMATCH;
    if (!calibration_sphase_settled()) f |= DIAG_F_SPHASE_BAD;
    if (!calibration_dc_ok(gain)) f |= DIAG_F_NO_DC;
    if (faults) f |= DIAG_F_FAULT;
    if (s_afc_locked) f |= DIAG_F_AFC_LOCKED;
    if (!rf_freq_in_spec(rf_frequency_mhz())) f |= DIAG_F_OUT_OF_SPEC;
    return f;
}

static void close_second(int64_t now)
{
    const arc_gain_table_t *table = rf_gain_table();
    diag_record_t r = {0};
    r.t_s = (uint32_t)(now / 1000000);
    r.video_pct = s_acc.measured ? (uint8_t)(s_acc.video * 100u / s_acc.measured) : 0u;
    r.gain = (uint8_t)((s_acc.gain_sum + s_acc.windows / 2u) / s_acc.windows);
    r.gain_max = table->max_index;
    arc_gain_tuple_t tuple;
    r.rf_stage = arc_gain_tuple_decode(table, r.gain, &tuple) ? tuple.rf_stage : 0xff;
    r.coherence = (int16_t)(s_acc.coh / (long)s_acc.windows);
    r.p50 = (int16_t)(s_acc.p50 / (long)s_acc.windows);
    r.origin_pm = (int16_t)(s_acc.origin / (long)s_acc.windows);
    r.clip_pm = (int16_t)(s_acc.clip / (long)s_acc.windows);
    r.offset_khz = (int16_t)rf_offset_khz();
    r.flags = chain_flags(r.gain, s_acc.faults);

    taskENTER_CRITICAL(&s_lock);
    s_hist[s_seq % DIAG_HISTORY] = r;
    ++s_seq;
    taskEXIT_CRITICAL(&s_lock);

    if (!s_in_dropout && diag_video_lost(&r)) {
        diag_record_t look[DIAG_LOOKBACK];
        unsigned n = s_seq < DIAG_LOOKBACK ? s_seq : DIAG_LOOKBACK;
        for (unsigned k = 0; k < n; ++k) look[k] = s_hist[(s_seq - n + k) % DIAG_HISTORY];
        int last_ok = -1;
        for (int k = (int)n - 2; k >= 0; --k) if (diag_video_ok(&look[k])) { last_ok = k; break; }
        if (last_ok < 0) return;            /* no picture before: not a dropout */
        s_in_dropout = true;
        diag_event_t e = {.t_s = r.t_s, .category = diag_classify(look, n), .last_ok = look[last_ok], .drop = r,
                          .open = true};
        taskENTER_CRITICAL(&s_lock);
        s_events[s_event_seq % DIAG_EVENTS] = e;
        ++s_event_seq;
        ++s_dropouts[e.category];
        taskEXIT_CRITICAL(&s_lock);
    } else if (s_in_dropout && diag_video_ok(&r)) {
        s_in_dropout = false;
        taskENTER_CRITICAL(&s_lock);
        diag_event_t *e = &s_events[(s_event_seq - 1u) % DIAG_EVENTS];
        e->duration_s = r.t_s - e->t_s;
        e->open = false;
        taskEXIT_CRITICAL(&s_lock);
    }
}

void diag_window(bool measured, bool video_valid)
{
    const int64_t now = esp_timer_get_time();
    /* A gap (scan, stalled task) is not a second of reception. */
    if (!s_acc.start_us || now - s_acc.start_us > 2000000) {
        memset(&s_acc, 0, sizeof(s_acc));
        s_acc.start_us = now;
        s_in_dropout = false;
    }
    if (s_menu_active || s_scan_active || receiver_paused()) s_acc.owned = true;
    ++s_acc.windows;
    s_acc.measured += measured;
    s_acc.video += measured && video_valid;
    s_acc.gain_sum += s_current_gain;
    s_acc.coh += s_obs_coherence;
    s_acc.p50 += s_obs_p50;
    s_acc.origin += s_obs_origin_pm;
    s_acc.clip += s_obs_clip_pm;
    if (now - s_acc.start_us < 1000000) return;
    const uint32_t faults = transport_fault_count();
    s_acc.faults = faults - s_seen_faults;
    s_seen_faults = faults;
    /* A second the menu, a calibration or the scanner owned says nothing
     * about the RF link; it also ends any dropout bookkeeping. */
    if (s_acc.owned) s_in_dropout = false;
    else close_second(now);
    memset(&s_acc, 0, sizeof(s_acc));
    s_acc.start_us = now;
}

/* ---- Console side --------------------------------------------------------- */
static void print_record(const char *tag, const diag_record_t *r)
{
    printf("%s t=%lu video_pct=%u gain=%u max=%u rf_stage=%u p50=%d coherence=%d origin_pm=%d clip_pm=%d "
           "offset_khz=%d afc=%u filter_ok=%u sphase_ok=%u dc_ok=%u fault=%u in_spec=%u\n", tag,
           (unsigned long)r->t_s, r->video_pct, r->gain, r->gain_max, r->rf_stage, r->p50, r->coherence, r->origin_pm,
           r->clip_pm, r->offset_khz, !!(r->flags & DIAG_F_AFC_LOCKED), !(r->flags & DIAG_F_FILTER_MISMATCH),
           !(r->flags & DIAG_F_SPHASE_BAD), !(r->flags & DIAG_F_NO_DC), !!(r->flags & DIAG_F_FAULT),
           !(r->flags & DIAG_F_OUT_OF_SPEC));
}

static void print_event(const diag_event_t *e)
{
    printf("FIELD_DIAG_EVENT dropout t=%lu category=%s duration_s=%s%lu channel=%s freq=%u profile=%s\n",
           (unsigned long)e->t_s, diag_category_name(e->category), e->open ? ">=" : "",
           (unsigned long)(e->open ? (uint32_t)(esp_timer_get_time() / 1000000) - e->t_s : e->duration_s),
           rf_channel()->name, rf_frequency_mhz(), profile_name(profile_active()));
    print_record("FIELD_DIAG_EVENT last_ok", &e->last_ok);
    print_record("FIELD_DIAG_EVENT drop", &e->drop);
}

void diag_toggle_stream(void)
{
    s_stream = !s_stream;
    printf("FIELD_DIAG stream=%u (one line per second; R toggles)\n", s_stream);
}

void diag_poll(void)
{
    static uint32_t printed_seq, printed_events, printed_closed;
    uint32_t seq, events;
    diag_record_t r;
    diag_event_t e;
    for (;;) {
        taskENTER_CRITICAL(&s_lock);
        seq = s_seq;
        if (seq - printed_seq > DIAG_HISTORY) printed_seq = seq - DIAG_HISTORY;
        bool have = printed_seq != seq;
        if (have) r = s_hist[printed_seq % DIAG_HISTORY];
        taskEXIT_CRITICAL(&s_lock);
        if (!have) break;
        ++printed_seq;
        if (s_stream) print_record("FIELD_DIAG", &r);
    }
    taskENTER_CRITICAL(&s_lock);
    events = s_event_seq;
    bool fresh = printed_events != events;
    if (fresh) e = s_events[(events - 1u) % DIAG_EVENTS];
    bool closed = !fresh && events && printed_closed != events && !s_events[(events - 1u) % DIAG_EVENTS].open;
    if (closed) e = s_events[(events - 1u) % DIAG_EVENTS];
    taskEXIT_CRITICAL(&s_lock);
    if (fresh) { printed_events = events; print_event(&e); }
    else if (closed) {
        printed_closed = events;
        printf("FIELD_DIAG_EVENT recovered t=%lu category=%s duration_s=%lu\n", (unsigned long)(e.t_s + e.duration_s),
               diag_category_name(e.category), (unsigned long)e.duration_s);
    }
}

/* One-command health report: what is really active, what is missing, and
 * plain-language verdicts. Read-only. */
void diag_report(void)
{
    const uint16_t mhz = rf_frequency_mhz();
    const rf_filter_t f = rf_filter();
    rf_filter_t normal, narrow;
    const bool have_normal = filter_setting(false, &normal), have_narrow = filter_setting(true, &narrow);
    const char *cls = have_narrow && rf_filter_equal(f, narrow) ? "narrow" :
                      have_normal && rf_filter_equal(f, normal) ? "normal" : "uncalibrated";
    const arc_gain_table_t *table = rf_gain_table();
    const uint8_t g = s_current_gain;
    arc_gain_tuple_t t = {0};
    const bool tuple_ok = arc_gain_tuple_decode(table, g, &t);
    const uint8_t top = arc_gain_highest_rf_stage_start(table);

    printf("RANGE_DIAG channel=%s freq=%u in_spec=%u%s\n", rf_channel()->name, mhz, rf_freq_in_spec(mhz),
           rf_freq_in_spec(mhz) ? "" : " WARNING above the documented C5 range (5180-5885 MHz), not characterised");
    printf("RANGE_DIAG filter requested code=%d skirt=%u bw=%s class=%s applied=%u (read back code=%d skirt=%d) "
           "calibration=%s\n", f.code, f.skirt, f.bw20 ? "BW20" : "BW40", cls, rf_filter_applied(), phy_filter_code(),
           phy_filter_skirt(), calibration_filter_result());
    profile_print();
    printf("RANGE_DIAG afc locked=%u offset_khz=%d cfo_khz=%d\n", s_afc_locked, rf_offset_khz(), s_cfo_khz);
    printf("RANGE_DIAG gain index=%u max=%u rf_stage=%u rf_code=%u bb=%u fine=%u top_rf_stage_from=%u at_max=%u "
           "state=%u\n", g, table->max_index, tuple_ok ? t.rf_stage : 0xffu, tuple_ok ? t.rf_code : 0u,
           tuple_ok ? t.bb_code : 0u, tuple_ok ? t.fine_code : 0u, top, g >= table->max_index, (unsigned)s_gain.state);
    printf("RANGE_DIAG chain sphase=%s dc_ok=%u dc_held=%u lane=%s faults=%lu\n", calibration_sphase_state(),
           calibration_dc_ok(g), phy_dco_held(), rf_lane_name(rf_lane()), (unsigned long)transport_fault_count());
    printf("RANGE_DIAG iq p50=%d coherence=%d origin_pm=%d clip_pm=%d video_recent=%u\n", s_obs_p50, s_obs_coherence,
           s_obs_origin_pm, s_obs_clip_pm,
           s_last_video_us && esp_timer_get_time() - s_last_video_us < 2000000);

    /* Verdicts: evidence, not a measured cause. */
    bool any = false;
#define VERDICT(cond, text) do { if (cond) { printf("RANGE_DIAG verdict=%s\n", text); any = true; } } while (0)
    VERDICT(!have_normal, "NOT_CALIBRATED filter (safe chip default in use; CALIBRATE or reboot with a quiet channel)");
    VERDICT(!rf_filter_applied(), "CALIBRATION_OR_PHY filter not read back as requested");
    VERDICT(!calibration_sphase_settled(), "CALIBRATION_OR_PHY sampling phase not settled (repaired at boot only)");
    VERDICT(!calibration_dc_ok(g), "NOT_CALIBRATED DC correction missing for this gain (VTX off at boot fills it)");
    VERDICT(!rf_freq_in_spec(mhz), "UNKNOWN out-of-spec channel: compare with a channel <= 5885 MHz");
    VERDICT(g >= table->max_index && s_obs_coherence <= DIAG_NOISE_COHERENCE,
            "POSSIBLE_RF_FLOOR table maximum and noise-like IQ (no carrier, or below the receiver noise)");
    VERDICT(g + DIAG_FADE_HEADROOM <= table->max_index && s_obs_coherence >= DIAG_GOOD_COHERENCE,
            "OK strong carrier with gain headroom");
    if (!any) printf("RANGE_DIAG verdict=UNKNOWN no specific evidence\n");
#undef VERDICT

    /* History: last 60 s and the dropouts of this boot. */
    diag_record_t h[DIAG_HISTORY];
    diag_event_t ev[DIAG_EVENTS];
    uint32_t seq, events, drops[DIAG_CATEGORIES];
    taskENTER_CRITICAL(&s_lock);
    seq = s_seq;
    events = s_event_seq;
    memcpy(h, s_hist, sizeof(h));
    memcpy(ev, s_events, sizeof(ev));
    memcpy(drops, s_dropouts, sizeof(drops));
    taskEXIT_CRITICAL(&s_lock);
    unsigned n = seq < 60u ? seq : 60u, ok = 0, max_gain = 0;
    for (unsigned k = 0; k < n; ++k) {
        const diag_record_t *r = &h[(seq - 1u - k) % DIAG_HISTORY];
        ok += diag_video_ok(r);
        max_gain += r->gain + 1u >= r->gain_max;
    }
    printf("RANGE_DIAG history seconds=%u video_ok_pct=%u at_max_gain_pct=%u dropouts=%lu", n, n ? ok * 100u / n : 0u,
           n ? max_gain * 100u / n : 0u, (unsigned long)events);
    for (int c = 0; c < DIAG_CATEGORIES; ++c) printf(" %s=%lu", diag_category_name(c), (unsigned long)drops[c]);
    printf("\n");
    for (uint32_t k = events > DIAG_EVENTS ? events - DIAG_EVENTS : 0; k < events; ++k) print_event(&ev[k % DIAG_EVENTS]);
}
