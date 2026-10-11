/* C5VRX-5 by Twotoz and the C5VRX contributors: receive profile per channel.
 *
 * The VTX deviation sets the best filter width and tracker loop gain
 * (v5/research, simulation, usable-picture threshold):
 *   deviation 0.69: 10 MHz + kp 0.5, 3.2 dB better than 14 MHz + kp 0.85;
 *   deviation 1.0:  10 MHz + kp 0.6, 1.7 dB better;
 *   deviation 1.4:  12 MHz + kp 0.85 (the default) is already best.
 * The narrow profiles hurt a wide VTX badly, so a profile is only taken
 * from a clear measurement, kept per channel in NVS, and chosen before live
 * TX starts. It never changes under live video: a different profile is
 * applied only while the menu or a status screen owns TX (startup,
 * channel change, manual calibration). Anything uncertain keeps DEFAULT.
 *
 * Deviation: the AFC measures the sync tip and the burst-free porch of
 * burst-confirmed lines; their distance is deviation x 1.914 MHz (40 of 140
 * IRE over 6.7 MHz). A decision needs three batches of 32 estimates, each
 * with a spread <= 8 % and all medians within 5 % of each other, on a
 * centred (AFC locked), coherent carrier in the healthy gain band. The
 * estimator and thresholds are pure (profile_logic.h) and host-tested. */
#include "receiver.h"
#include "profile_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const s_names[PROFILES] = {"DEFAULT 12MHZ KP0.85", "MATCHED 10MHZ KP0.6", "MATCHED 10MHZ KP0.5"};
static uint16_t s_dev_table[FPV_CHANNELS];    /* deviation x100 per channel, 0 = unknown */
static volatile int s_profile = PROFILE_DEFAULT;

/* Measurement state, written by the control task through profile_observe(). */
static volatile bool s_measuring;
static dev_estimator_t s_dev;

const char *profile_name(int p) { return p >= 0 && p < PROFILES ? s_names[p] : "?"; }
int profile_active(void) { return s_profile; }
int profile_deviation_x100(size_t channel) { return channel < FPV_CHANNELS ? s_dev_table[channel] : 0; }

int profile_for_deviation(int dev_x100)
{
    rf_filter_t narrow;
    return profile_class(dev_x100, filter_setting(true, &narrow));
}

/* Filter class and tracker of a profile. Only while TX is not live: before
 * transport_start or with the menu / a status screen owning TX. */
bool profile_set(int p)
{
    if (p < 0 || p >= PROFILES) return false;
    rf_filter_t f;
    if (!filter_setting(p != PROFILE_DEFAULT, &f)) {
        if (p != PROFILE_DEFAULT) return false;
        f = rf_filter();            /* uncalibrated: keep what is set */
    }
    int tracker = p == PROFILE_KP050 ? TRACKER_KP050 : p == PROFILE_KP060 ? TRACKER_KP060 : TRACKER_KP085;
    if (!transport_set_tracker(tracker)) return false;   /* refuses while TX is live */
    rf_filter_t now = rf_filter();
    if (now.code != f.code || now.skirt != f.skirt || now.bw20 != f.bw20) rf_set_filter(f);
    s_profile = p;
    return true;
}

void profile_load(void)
{
    if (!blob_load("vtxdev", s_dev_table, sizeof(s_dev_table))) memset(s_dev_table, 0, sizeof(s_dev_table));
}

/* Boot, before live TX: the stored deviation of the current channel. */
void profile_boot(void)
{
    int p = profile_for_deviation(s_dev_table[rf_channel_index()]);
    if (!profile_set(p)) (void)profile_set(PROFILE_DEFAULT);
    printf("PROFILE boot channel=%s deviation_x100=%u profile=%s\n", rf_channel()->name,
           s_dev_table[rf_channel_index()], s_names[s_profile]);
}

/* After a channel change: the stored profile of the new channel, through a
 * TX-owned transition (the retune already interrupts the picture). */
void profile_channel_changed(void)
{
    int p = profile_for_deviation(s_dev_table[rf_channel_index()]);
    if (p == s_profile) return;
    if (s_menu_active) { if (!profile_set(p)) (void)profile_set(PROFILE_DEFAULT); return; }
    status_screen("CHANNEL", rf_channel()->name, profile_name(p), -1);
    if (!profile_set(p)) (void)profile_set(PROFILE_DEFAULT);
    status_screen_end();
}

void profile_measure_begin(void)
{
    dev_reset(&s_dev);
    s_measuring = true;
}

void profile_measure_end(void) { s_measuring = false; }
int profile_measure_result(void) { return s_dev.result_x100; }

/* One burst-confirmed AFC window; gates on a usable, centred carrier. */
void profile_observe(const afc2_result_t *r)
{
    if (!s_measuring || s_dev.result_x100 >= 0 || !r->sync_pairs || !r->porch_pairs || !s_afc_locked) return;
    if (s_obs_p50 < 13 || s_obs_p50 > 32 || s_obs_coherence < 80 || s_obs_clip_pm || s_gain.state != DG_HOLD) return;
    int32_t span = r->polarity < 0 ? r->porch_khz - r->sync_khz : r->sync_khz - r->porch_khz;
    unsigned rejected = s_dev.rejected;
    (void)dev_add(&s_dev, span);
    if (s_dev.rejected != rejected) printf("PROFILE batch rejected (spread or disagreement), count=%u\n", s_dev.rejected);
}

/* Store a measured deviation for the current channel and select its
 * profile through a TX-owned transition. */
void profile_commit(int dev_x100)
{
    size_t ch = rf_channel_index();
    if (dev_x100 <= 0 || dev_x100 > 400) return;
    if (s_dev_table[ch] != dev_x100) {
        s_dev_table[ch] = (uint16_t)dev_x100;
        (void)blob_store("vtxdev", s_dev_table, sizeof(s_dev_table));
    }
    int p = profile_for_deviation(dev_x100);
    char line[48];
    snprintf(line, sizeof(line), "VTX DEVIATION %d.%02d", dev_x100 / 100, dev_x100 % 100);
    status_screen("VTX MATCHED", line, profile_name(p), 100);
    if (!profile_set(p)) (void)profile_set(PROFILE_DEFAULT);
    vTaskDelay(pdMS_TO_TICKS(1200));
    status_screen_end();
    printf("PROFILE channel=%s deviation_x100=%d profile=%s\n", rf_channel()->name, dev_x100, s_names[s_profile]);
}
