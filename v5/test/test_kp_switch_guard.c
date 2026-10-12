/* C5VRX-5 by Twotoz and the C5VRX contributors: anti-flicker regressions. */
#include <assert.h>
#include <stdio.h>
#include "kp_switch_guard.h"

int main(void)
{
    kp_switch_guard_t physical = {0}, candidate = {0};
    assert(kp_switch_ready(&physical, 0));
    kp_switch_mark(&physical, 100);
    assert(!kp_switch_ready(&physical, 100));
    assert(!kp_switch_ready(&physical, 100 + KP_SWITCH_GAP_US - 1));
    assert(kp_switch_ready(&physical, 100 + KP_SWITCH_GAP_US));
    /* Every actual operation shares this clock, including failed restarts
     * and manual requests: changing target or resetting the policy cannot
     * bypass the backend's five-second gap. */
    kp_switch_mark(&physical, 100 + KP_SWITCH_GAP_US);
    candidate = (kp_switch_guard_t){0};
    assert(!kp_switch_ready(&physical, 100 + KP_SWITCH_GAP_US + 1));

    assert(!kp_switch_confirm(&candidate, 1, false, 7, true, 0));
    assert(!kp_switch_confirm(&candidate, 1, false, 7, true, KP_SWITCH_CONFIRM_US - 1));
    assert(kp_switch_confirm(&candidate, 1, false, 7, true, KP_SWITCH_CONFIRM_US));
    assert(!kp_switch_confirm(&candidate, 1, true, 7, true, KP_SWITCH_CONFIRM_US));
    assert(!kp_switch_confirm(&candidate, 1, false, 7, true, KP_SWITCH_CONFIRM_US + 50000));
    /* Oscillation at a threshold never reaches confirmation. */
    for (int64_t t = 3000000; t < 20000000; t += 50000)
        assert(!kp_switch_confirm(&candidate, (int)((t / 50000) & 1), false, 7, true, t));
    assert(!kp_switch_confirm(&candidate, 2, true, 7, true, 20000000));
    assert(!kp_switch_confirm(&candidate, 2, true, 7, false, 21999999));
    assert(!kp_switch_confirm(&candidate, 2, true, 7, true, 22000000));
    assert(kp_switch_confirm(&candidate, 2, true, 7, true, 24000000));
    assert(!kp_switch_confirm(&candidate, 2, true, 8, true, 24000000));
    assert(!kp_switch_confirm(&candidate, 2, true, 8, true, 25999999));
    assert(kp_switch_confirm(&candidate, 2, true, 8, true, 26000000));
    puts("PASS: shared five-second cooldown, two-second confirmation, jitter, invalid windows, contexts");
    return 0;
}
