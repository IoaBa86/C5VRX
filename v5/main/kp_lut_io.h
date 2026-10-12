/* C5VRX-5 by Twotoz and the C5VRX contributors: host-word LUT updates.
 * ESP-IDF's bitscrambler_ll_lutmem_write addresses 32-bit CPU words;
 * lut_mode controls the execution width, not the host access width. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define KP_LUT_ENTRIES 1024u
#define KP_LUT_WORDS (KP_LUT_ENTRIES / 2u)

typedef bool (*kp_lut_write_fn)(void *ctx, unsigned word, uint32_t value);

static inline uint32_t kp_lut_word(const uint16_t *table, unsigned word)
{
    return (uint32_t)table[2u * word] | ((uint32_t)table[2u * word + 1u] << 16);
}

/* Bound attempts (including failures), preserve both halves, and acknowledge
 * only verified words. A failed word remains dirty and is retried next pass. */
static inline unsigned kp_lut_update(uint16_t *shadow, const uint16_t *want,
                                     uint16_t *cursor, unsigned budget,
                                     kp_lut_write_fn write_word, void *ctx,
                                     uint32_t *verify_fail)
{
    unsigned attempts = 0;
    for (unsigned scanned = 0; scanned < KP_LUT_WORDS && attempts < budget; ++scanned) {
        unsigned word = *cursor;
        *cursor = (uint16_t)((word + 1u) % KP_LUT_WORDS);
        uint32_t value = kp_lut_word(want, word);
        if (kp_lut_word(shadow, word) == value) continue;
        ++attempts;
        if (!write_word(ctx, word, value)) {
            ++*verify_fail;
            continue;
        }
        shadow[2u * word] = want[2u * word];
        shadow[2u * word + 1u] = want[2u * word + 1u];
    }
    return attempts;
}
