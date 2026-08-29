#include "memory/shared_mailbox.h"

#include <string.h>

bool rp86_shared_mailbox_init(rp86_memory_backing_t *backing) {
    const rp86_shared_mailbox_header_t header = {
        .magic = RP86_SHARED_MAILBOX_MAGIC,
        .version = RP86_SHARED_MAILBOX_VERSION,
        .header_size = sizeof header,
        .owner = RP86_SHARED_MAILBOX_OWNER_HOST,
        .status = RP86_SHARED_MAILBOX_EMPTY,
    };
    if (!rp86_memory_backing_fill(backing, RP86_SHARED_MAILBOX_BASE, 0u,
                                  RP86_SHARED_MAILBOX_SIZE) ||
        !rp86_memory_backing_write(backing, RP86_SHARED_MAILBOX_BASE,
                                   &header, sizeof header))
        return false;
    rp86_memory_backing_publish(backing);
    return true;
}
