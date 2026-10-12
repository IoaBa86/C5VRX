/* C5VRX-5 by Twotoz and the C5VRX contributors: per-channel DC tables in NVS
 * (pure logic, host test test/test_dco_slots.c).
 *
 * The DC DAC codes are measured per gain row for one tuning. One stored
 * table meant that every channel change was followed by a new DC search at
 * the next boot. Tables are now kept per frequency in DCO_SLOTS fixed NVS
 * blobs ("dco0".."dco5", bounded NVS use, no erase needed) with a small
 * index of their frequencies and use order. A frequency without its own
 * table starts from the nearest stored one: the baseband offset per gain
 * row changes little across the band (calibration.c), and the live drift
 * nudges trim the rest. Measuring the own channel replaces the seed. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DCO_SLOTS          6u
#define DCO_INDEX_VERSION  1u

typedef struct {
    uint16_t version;
    uint16_t freq[DCO_SLOTS];   /* MHz, 0 = empty slot */
    uint16_t seq[DCO_SLOTS];    /* use order, higher = more recent */
    uint16_t next_seq;
} dco_index_t;

static inline void dco_index_init(dco_index_t *x)
{
    *x = (dco_index_t){.version = DCO_INDEX_VERSION, .next_seq = 1u};
}

static inline int dco_slot_of(const dco_index_t *x, uint16_t freq)
{
    for (unsigned k = 0; k < DCO_SLOTS; ++k)
        if (freq && x->freq[k] == freq) return (int)k;
    return -1;
}

/* Slot to store `freq` in: its own slot, else an empty one, else the least
 * recently used. */
static inline unsigned dco_slot_for_store(const dco_index_t *x, uint16_t freq)
{
    int own = dco_slot_of(x, freq);
    if (own >= 0) return (unsigned)own;
    unsigned best = 0;
    for (unsigned k = 0; k < DCO_SLOTS; ++k) {
        if (!x->freq[k]) return k;
        if (x->seq[k] < x->seq[best]) best = k;
    }
    return best;
}

/* Nearest stored frequency other than `freq` (ties: the more recent one),
 * -1 when nothing is stored. */
static inline int dco_nearest_slot(const dco_index_t *x, uint16_t freq)
{
    int best = -1;
    unsigned best_d = 0;
    for (unsigned k = 0; k < DCO_SLOTS; ++k) {
        if (!x->freq[k] || x->freq[k] == freq) continue;
        unsigned d = x->freq[k] > freq ? (unsigned)(x->freq[k] - freq) : (unsigned)(freq - x->freq[k]);
        if (best < 0 || d < best_d || (d == best_d && x->seq[k] > x->seq[best])) { best = (int)k; best_d = d; }
    }
    return best;
}

static inline void dco_index_touch(dco_index_t *x, unsigned slot, uint16_t freq)
{
    if (slot >= DCO_SLOTS) return;
    if (x->next_seq == UINT16_MAX) {            /* renumber, keeping the order */
        for (unsigned k = 0; k < DCO_SLOTS; ++k) x->seq[k] = x->freq[k] ? (uint16_t)(x->seq[k] / 2u) : 0u;
        x->next_seq = UINT16_MAX / 2u + 1u;
    }
    x->freq[slot] = freq;
    x->seq[slot] = x->next_seq++;
}
