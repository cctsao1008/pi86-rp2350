#include <assert.h>
#include <stdint.h>
#include <string.h>

#define RP86_SPSC_RECORD_BARRIER() ((void)0)
#include "runtime/spsc_record_slot.h"

static void fill(uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE], uint8_t seed) {
    for (uint32_t i = 0u; i < RP86_HOST_PROTOCOL_MESSAGE_SIZE; ++i)
        record[i] = (uint8_t)(seed + i);
}

int main(void) {
    rp86_spsc_record_slot_t slot = {0};
    uint8_t first[RP86_HOST_PROTOCOL_MESSAGE_SIZE];
    uint8_t second[RP86_HOST_PROTOCOL_MESSAGE_SIZE];
    uint8_t output[RP86_HOST_PROTOCOL_MESSAGE_SIZE] = {0};
    fill(first, 0x10u);
    fill(second, 0x80u);

    assert(!rp86_spsc_record_try_take(&slot, output));
    assert(rp86_spsc_record_try_publish(&slot, first));

    /* Full-slot publication must fail without overwriting the first record. */
    assert(!rp86_spsc_record_try_publish(&slot, second));
    assert(rp86_spsc_record_drops(&slot) == 1u);
    assert(rp86_spsc_record_try_take(&slot, output));
    assert(memcmp(output, first, sizeof output) == 0);

    /* The same slot must accept and return a second record in one boot. */
    memset(output, 0, sizeof output);
    assert(rp86_spsc_record_try_publish(&slot, second));
    assert(rp86_spsc_record_try_take(&slot, output));
    assert(memcmp(output, second, sizeof output) == 0);
    assert(!rp86_spsc_record_try_take(&slot, output));
    assert(rp86_spsc_record_drops(&slot) == 1u);
    return 0;
}
