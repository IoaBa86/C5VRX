/* Noise-floor probe: the Wi-Fi PHY's own calibrated noise floor and packet
 * RSSI per 5 GHz channel (VTX off). Scratch diagnostic, not C5VRX firmware. */
#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
extern void phy_force_rx_gain(bool enable, uint8_t gain_idx);
extern void phy_disable_agc(void);
extern void phy_enable_agc(void);
extern void phy_rfagc_disable(void);
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define DUMP_CTRL 0x600a9004u
#define DUMP_PTR_MODE 0x600a9008u
#define DUMP_FORMAT 0x600a9018u
#define FE_PATH 0x600a20b4u
#define FE_ENABLE 0x600a0800u
#define SOURCE_CTRL 0x600a08ccu
#define SOURCE_MUX 0x600a70b8u
#define MODEM_CLOCK 0x600a9c04u
#define HP_SRAM_USAGE 0x60095004u
static const uint32_t s_regs[] = {DUMP_CTRL, DUMP_PTR_MODE, DUMP_FORMAT, FE_PATH, FE_ENABLE, SOURCE_CTRL, SOURCE_MUX,
                                  MODEM_CLOCK, HP_SRAM_USAGE};
static uint32_t s_saved[9];
static const int s_iq_pins[8] = {1, 0, 25, 7, 10, 5, 3, 4};
static const int s_dac_pins[8] = {23, 24, 11, 12, 8, 9, -1, -1};
static const uint8_t s_diag[8] = {4u, 5u, 6u, 9u, 14u, 15u, 16u, 19u};
static void save_regs(void) { for (int k = 0; k < 9; ++k) s_saved[k] = REG32(s_regs[k]); }
static void restore_regs(void)
{
    REG32(DUMP_CTRL) = s_saved[0] & ~0x80000000u;
    for (int k = 1; k < 9; ++k) REG32(s_regs[k]) = s_saved[k];
    REG32(DUMP_CTRL) = s_saved[0];
}
static void pins_off(void)
{
    for (int k = 0; k < 8; ++k) { gpio_reset_pin(s_iq_pins[k]); if (s_dac_pins[k] >= 0) gpio_reset_pin(s_dac_pins[k]); }
}
static void iq_pins_on(const int *pins)
{
    for (int k = 0; k < 8; ++k) {
        if (pins[k] < 0) continue;
        gpio_set_direction(pins[k], GPIO_MODE_INPUT_OUTPUT);
        esp_rom_gpio_connect_out_signal(pins[k], MODEM_DIAG0_IDX + s_diag[k], false, false);
    }
}
/* rf.c enable_continuous_modem() */
static void dump_regs_on(bool clocks)
{
    REG32(HP_SRAM_USAGE) = (REG32(HP_SRAM_USAGE) & 0xfffef0ffu) | 0x00010000u;
    REG32(SOURCE_CTRL) &= 0xff87ffffu;
    REG32(SOURCE_MUX) = (REG32(SOURCE_MUX) & 0xfffffff8u) | 1u;
    if (clocks) REG32(MODEM_CLOCK) = UINT32_MAX;
    REG32(FE_ENABLE) |= 4u;
    REG32(FE_PATH) &= ~1u;
    uint32_t v = REG32(DUMP_FORMAT);
    v = (v & 0xff03ffffu) | 0x006c0000u; REG32(DUMP_FORMAT) = v;
    v = (REG32(DUMP_FORMAT) & 0xfffc0fffu) | 0x0001a000u; REG32(DUMP_FORMAT) = v;
    v = (REG32(DUMP_FORMAT) & 0xfffff03fu) | 0x00000640u; REG32(DUMP_FORMAT) = v;
    v = (REG32(DUMP_FORMAT) & 0xffffffc0u) | 0x18u; REG32(DUMP_FORMAT) = v | 0x01000000u;
    REG32(DUMP_PTR_MODE) = (REG32(DUMP_PTR_MODE) & ~0x01fe0000u) | 0x00060000u;
    uint32_t ctrl = REG32(DUMP_CTRL);
    ctrl &= ~(0x80000000u | 0x00080000u | 0x00040000u);
    ctrl |= 0x00020000u;
    ctrl = (ctrl & ~0x0001ffffu) | 16384u;
    REG32(DUMP_CTRL) = ctrl;
    REG32(DUMP_CTRL) = ctrl | 0x80000000u;
}

static volatile int s_n, s_nf_sum, s_nf_min = 127, s_nf_max = -128, s_rssi_max = -128, s_rssi_min = 127;
static volatile int s_hist[64];

static void cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *p = buf;
    const int nf = p->rx_ctrl.noise_floor, r = p->rx_ctrl.rssi;
    ++s_n; s_nf_sum += nf;
    if (nf < s_nf_min) s_nf_min = nf;
    if (nf > s_nf_max) s_nf_max = nf;
    if (r > s_rssi_max) s_rssi_max = r;
    if (r < s_rssi_min) s_rssi_min = r;
    int b = (r + 110) / 2; if (b < 0) b = 0; if (b > 63) b = 63; ++s_hist[b];
}

void app_main(void)
{
    nvs_flash_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY);
    esp_wifi_set_promiscuous_rx_cb(cb);
    wifi_promiscuous_filter_t f = {.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL};
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous(true);
    /* Self-desense: each C5VRX receive-state ingredient on its own, same
     * channel, interleaved rounds. Weak packets (< -85 dBm) vanish first. */
    esp_wifi_set_channel(36, WIFI_SECOND_CHAN_NONE);
    vTaskDelay(pdMS_TO_TICKS(200));
    save_regs();
    static const char *const names[] = {"base_agc", "iq_pins", "dump_regs", "clocks_all", "dump+pins", "dac_pins",
                                        "forced_G83", "G83+dump+pins+dac"};
    for (int round = 0; round < 4; ++round) {
        for (int m = 0; m < 8; ++m) {
            restore_regs(); pins_off();
            phy_force_rx_gain(false, 0); phy_enable_agc();
            if (m == 1) iq_pins_on(s_iq_pins);
            if (m == 2) dump_regs_on(false);
            if (m == 3) REG32(MODEM_CLOCK) = UINT32_MAX;
            if (m == 4) { dump_regs_on(true); iq_pins_on(s_iq_pins); }
            if (m == 5) { dump_regs_on(true); iq_pins_on(s_dac_pins); }
            if (m >= 6) { phy_disable_agc(); phy_rfagc_disable(); phy_force_rx_gain(true, 83); }
            if (m == 7) { dump_regs_on(true); iq_pins_on(s_iq_pins); iq_pins_on(s_dac_pins); }
            vTaskDelay(pdMS_TO_TICKS(100));
            s_n = 0; s_nf_sum = 0; s_nf_min = 127; s_nf_max = -128; s_rssi_max = -128; s_rssi_min = 127;
            memset((void *)s_hist, 0, sizeof(s_hist));
            vTaskDelay(pdMS_TO_TICKS(2500));
            int weak = 0; for (int k = 0; k < 13; ++k) weak += s_hist[k];   /* < -84 dBm */
            const int n = s_n;
            printf("DESENSE round=%d mode=%-18s pkts=%4d weak=%3d nf=%.1f rssi_min=%d rssi_max=%d\n", round, names[m], n,
                   weak, n ? (double)s_nf_sum / n : 0.0, n ? s_rssi_min : 0, n ? s_rssi_max : 0);
        }
    }
    restore_regs(); pins_off();
    printf("NFPROBE done\n");
}
