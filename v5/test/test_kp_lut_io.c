/* C5VRX-5 by Twotoz and the C5VRX contributors: CPU LUT port regression.
 * Model the documented 512 x 32-bit host port independently of LUT16. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kp_lut_io.h"
#include "kp_tables.h"

typedef struct {
    uint32_t ram[512];
    unsigned writes;
    int fail_word;
} port_t;

static bool write_word(void *ctx, unsigned word, uint32_t value)
{
    port_t *port = ctx;
    assert(word < 512);
    ++port->writes;
    if ((int)word == port->fail_word) return false;
    port->ram[word] = value;
    return true;
}

int main(void)
{
    uint16_t shadow[1024], want[1024];
    port_t port = {.fail_word = -1};
    uint16_t cursor = 0;
    uint32_t fail = 0;
    memcpy(shadow, kp_tables[0], sizeof(shadow));
    for (unsigned word = 0; word < 512; ++word)
        port.ram[word] = (uint32_t)shadow[2 * word] | ((uint32_t)shadow[2 * word + 1] << 16);

    /* Odd-only edits preserve their even neighbour, including final entry. */
    memcpy(want, shadow, sizeof(want));
    want[1] ^= 0x55;
    want[1023] ^= 0xaa;
    uint32_t first = port.ram[0], last = port.ram[511];
    assert(kp_lut_update(shadow, want, &cursor, 1, write_word, &port, &fail) == 1);
    assert((port.ram[0] & 0xffff) == (first & 0xffff));
    assert((port.ram[0] >> 16) == want[1]);
    assert(port.ram[511] == last);
    assert(kp_lut_update(shadow, want, &cursor, 1, write_word, &port, &fail) == 1);
    assert((port.ram[511] & 0xffff) == (last & 0xffff));
    assert((port.ram[511] >> 16) == want[1023]);

    /* A failed write cannot advance the software level. It consumes budget
     * and succeeds on retry when the port accepts it. */
    want[0] ^= 0x11;
    port.fail_word = 0;
    uint16_t old = shadow[0];
    assert(kp_lut_update(shadow, want, &cursor, 1, write_word, &port, &fail) == 1);
    assert(fail == 1 && shadow[0] == old && memcmp(shadow, want, sizeof(shadow)));
    port.fail_word = -1;
    assert(kp_lut_update(shadow, want, &cursor, 1, write_word, &port, &fail) == 1);
    assert(!memcmp(shadow, want, sizeof(shadow)));

    /* Exercise all actual ladder tables both ways with bounded batches. */
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned k = 0; k < KP_LEVELS; ++k) {
            unsigned level = pass ? KP_LEVELS - 1 - k : k;
            for (unsigned batch = 0; batch < 512 && memcmp(shadow, kp_tables[level], sizeof(shadow)); ++batch)
                assert(kp_lut_update(shadow, kp_tables[level], &cursor, 7, write_word, &port, &fail) <= 7);
            assert(!memcmp(shadow, kp_tables[level], sizeof(shadow)));
            for (unsigned word = 0; word < 512; ++word) {
                uint32_t expected = (uint32_t)kp_tables[level][2 * word] |
                                    ((uint32_t)kp_tables[level][2 * word + 1] << 16);
                assert(port.ram[word] == expected);
            }
        }
    unsigned writes = port.writes;
    assert(kp_lut_update(shadow, kp_tables[0], &cursor, 0, write_word, &port, &fail) == 0);
    assert(port.writes == writes && fail == 1);
    puts("PASS: host-word addressing, both halves, bounded writes, failure retry, all kp tables");
    return 0;
}
