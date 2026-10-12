/* C5VRX-5 by Twotoz and the C5VRX contributors: host test of the per-channel
 * DC table slots (main/dco_slots.h).
 *
 *   cc -O2 -std=c11 -I../main test_dco_slots.c -o t && ./t
 */
#include <stdio.h>
#include "dco_slots.h"

static unsigned s_fail, s_run;
static void check(const char *name, int ok)
{
    ++s_run;
    printf("%-4s %s\n", ok ? "ok" : "FAIL", name);
    s_fail += !ok;
}

static void store(dco_index_t *x, uint16_t f) { dco_index_touch(x, dco_slot_for_store(x, f), f); }

int main(void)
{
    dco_index_t x;
    dco_index_init(&x);
    check("empty index: nothing to seed from", dco_nearest_slot(&x, 5865) < 0 && dco_slot_of(&x, 5865) < 0);
    store(&x, 5865);
    check("own channel found after a store", dco_slot_of(&x, 5865) == 0);
    check("another channel seeds from the only table", dco_nearest_slot(&x, 5732) == 0);
    store(&x, 5732);
    store(&x, 5917);
    check("nearest: 5740 seeds from 5732", x.freq[dco_nearest_slot(&x, 5740)] == 5732);
    check("nearest: 5880 seeds from 5865", x.freq[dco_nearest_slot(&x, 5880)] == 5865);
    check("nearest never returns the own frequency", x.freq[dco_nearest_slot(&x, 5865)] != 5865);
    check("re-storing a channel keeps its slot", dco_slot_for_store(&x, 5732) == (unsigned)dco_slot_of(&x, 5732));
    /* Fill all slots, then a new channel evicts the least recently stored. */
    store(&x, 5658); store(&x, 5695); store(&x, 5806);
    check("six channels fill six slots", dco_slot_of(&x, 5806) >= 0 && dco_slot_of(&x, 5865) >= 0);
    store(&x, 5865);                         /* 5865 used again: now the most recent */
    store(&x, 5362);                         /* evicts the oldest: 5732 */
    check("LRU eviction drops the least recently stored channel", dco_slot_of(&x, 5732) < 0 &&
          dco_slot_of(&x, 5865) >= 0 && dco_slot_of(&x, 5362) >= 0);
    /* Sequence renumbering keeps the use order. */
    x.next_seq = UINT16_MAX;
    store(&x, 5917);
    store(&x, 5945);                         /* evicts the oldest after renumbering */
    check("renumbering keeps the most recent channels", dco_slot_of(&x, 5917) >= 0 && dco_slot_of(&x, 5945) >= 0 &&
          dco_slot_of(&x, 5362) >= 0);
    printf("%s: %u checks, %u failures\n", s_fail ? "FAIL" : "PASS", s_run, s_fail);
    return s_fail ? 1 : 0;
}
