#ifndef RP86_SHARED_MAILBOX_H
#define RP86_SHARED_MAILBOX_H

#include <stdint.h>

#include "memory/backing.h"

enum {
    RP86_SHARED_MAILBOX_BASE = 0x3F000u,
    RP86_SHARED_MAILBOX_SIZE = 0x1000u,
    RP86_SHARED_MAILBOX_DATA_OFFSET = 32u,
    RP86_SHARED_MAILBOX_DATA_SIZE =
        RP86_SHARED_MAILBOX_SIZE - RP86_SHARED_MAILBOX_DATA_OFFSET,
    RP86_SHARED_MAILBOX_MAGIC = 0x4D363852u, /* "R86M" */
    RP86_SHARED_MAILBOX_VERSION = 1u,
};

typedef enum {
    RP86_SHARED_MAILBOX_OWNER_NONE = 0,
    RP86_SHARED_MAILBOX_OWNER_HOST = 1,
    RP86_SHARED_MAILBOX_OWNER_PROCESSOR = 2,
} rp86_shared_mailbox_owner_t;

typedef enum {
    RP86_SHARED_MAILBOX_EMPTY = 0,
    RP86_SHARED_MAILBOX_REQUEST_READY = 1,
    RP86_SHARED_MAILBOX_PROCESSING = 2,
    RP86_SHARED_MAILBOX_RESULT_READY = 3,
    RP86_SHARED_MAILBOX_ERROR = 4,
} rp86_shared_mailbox_status_t;

/* owner is the commit word. The current owner writes every other field and
 * the data area first, then transfers ownership with one final word write. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t owner;
    uint16_t status;
    uint32_t generation;
    uint16_t request_length;
    uint16_t response_length;
    uint32_t flags;
    uint32_t reserved[2];
} rp86_shared_mailbox_header_t;

bool rp86_shared_mailbox_init(rp86_memory_backing_t *backing);

_Static_assert(sizeof(rp86_shared_mailbox_header_t) ==
               RP86_SHARED_MAILBOX_DATA_OFFSET,
               "shared mailbox header ABI changed");

#endif
