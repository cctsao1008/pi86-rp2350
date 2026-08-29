#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "memory/backing.h"
#include "memory/memory_service.h"
#include "memory/shared_mailbox.h"

typedef struct __attribute__((packed)) {
    uint8_t operation;
    uint8_t reserved[3];
    uint32_t address;
    uint32_t length;
} request_header_t;

int main(void) {
    static uint8_t storage[RP86_SHARED_MAILBOX_BASE +
                           RP86_SHARED_MAILBOX_SIZE];
    rp86_memory_backing_t backing;
    rp86_memory_backing_init_direct(&backing, "TEST", 0u,
                                    storage, sizeof storage);
    assert(rp86_shared_mailbox_init(&backing));

    rp86_shared_mailbox_header_t mailbox;
    memcpy(&mailbox, storage + RP86_SHARED_MAILBOX_BASE, sizeof mailbox);
    assert(mailbox.magic == RP86_SHARED_MAILBOX_MAGIC);
    assert(mailbox.owner == RP86_SHARED_MAILBOX_OWNER_HOST);
    assert(mailbox.status == RP86_SHARED_MAILBOX_EMPTY);

    rp86_memory_service_t service;
    rp86_memory_service_init(&service, &backing);
    rp86_host_protocol_message_t request = {
        .version = RP86_HOST_PROTOCOL_VERSION,
        .type = RP86_HOST_PROTOCOL_MESSAGE_MEMORY_REQUEST,
        .sequence = 7u,
    };
    rp86_host_protocol_message_t reply;
    const request_header_t write = {
        .operation = RP86_MEMORY_WRITE, .address = 0x1234u, .length = 3u,
    };
    memcpy(request.payload, &write, sizeof write);
    memcpy(request.payload + sizeof write, "abc", 3u);
    request.length = sizeof write + 3u;
    assert(rp86_memory_service_handle(&service, &request, &reply));
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_OK);
    assert(memcmp(storage + 0x1234u, "abc", 3u) == 0);

    const request_header_t read = {
        .operation = RP86_MEMORY_READ, .address = 0x1234u, .length = 3u,
    };
    memset(request.payload, 0, sizeof request.payload);
    memcpy(request.payload, &read, sizeof read);
    request.length = sizeof read;
    request.sequence = 8u;
    assert(rp86_memory_service_handle(&service, &request, &reply));
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_OK);
    assert(reply.sequence == 8u);
    assert(reply.length == sizeof read + 3u);
    assert(memcmp(reply.payload + sizeof read, "abc", 3u) == 0);

    return 0;
}
