#include <assert.h>
#include <stdint.h>
#include <string.h>

#define PI86_SPSC_RECORD_BARRIER() ((void)0)
#include "runtime/spsc_record_slot.h"

static void fill(uint8_t record[PI86_BRIDGE_MESSAGE_SIZE], uint8_t seed) {
    for (uint32_t i = 0u; i < PI86_BRIDGE_MESSAGE_SIZE; ++i)
        record[i] = (uint8_t)(seed + i);
}

int main(void) {
    pi86_spsc_record_slot_t slot = {0};
    uint8_t first[PI86_BRIDGE_MESSAGE_SIZE];
    uint8_t second[PI86_BRIDGE_MESSAGE_SIZE];
    uint8_t output[PI86_BRIDGE_MESSAGE_SIZE] = {0};
    fill(first, 0x10u);
    fill(second, 0x80u);

    assert(!pi86_spsc_record_try_take(&slot, output));
    assert(pi86_spsc_record_try_publish(&slot, first));

    /* Full-slot publication must fail without overwriting the first record. */
    assert(!pi86_spsc_record_try_publish(&slot, second));
    assert(pi86_spsc_record_drops(&slot) == 1u);
    assert(pi86_spsc_record_try_take(&slot, output));
    assert(memcmp(output, first, sizeof output) == 0);

    /* The same slot must accept and return a second record in one boot. */
    memset(output, 0, sizeof output);
    assert(pi86_spsc_record_try_publish(&slot, second));
    assert(pi86_spsc_record_try_take(&slot, output));
    assert(memcmp(output, second, sizeof output) == 0);
    assert(!pi86_spsc_record_try_take(&slot, output));
    assert(pi86_spsc_record_drops(&slot) == 1u);
    return 0;
}
