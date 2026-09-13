#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "host_protocol/host_protocol.h"

int main(void) {
    rp86_host_protocol_message_t record = {0};
    const uint16_t valid[] = {0u, 8u, 52u};
    const uint16_t invalid[] = {53u, 60000u, 65535u};

    assert(!rp86_host_protocol_payload_length_valid(NULL));
    for (size_t i = 0u; i < sizeof valid / sizeof valid[0]; ++i) {
        record.length = valid[i];
        assert(rp86_host_protocol_payload_length_valid(&record));
    }
    for (size_t i = 0u; i < sizeof invalid / sizeof invalid[0]; ++i) {
        record.length = invalid[i];
        assert(!rp86_host_protocol_payload_length_valid(&record));
    }
    return 0;
}
