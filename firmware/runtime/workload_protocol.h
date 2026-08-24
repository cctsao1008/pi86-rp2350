#ifndef PI86_WORKLOAD_PROTOCOL_H
#define PI86_WORKLOAD_PROTOCOL_H

#include <stdint.h>

/*
 * Stable Host -> RP2350 workload contract.
 *
 * The Host converts executable formats to a flat native V30 image.  The
 * RP2350 validates this manifest, places the image in its configured backing
 * resource, constructs the reset handoff, and supervises execution.  Nothing
 * in this structure describes PIO programs, GPIO words, or expected bus
 * transactions: those are private implementation details of the companion
 * runtime.
 */

enum {
    PI86_WORKLOAD_MAGIC = 0x57363850u, /* "P86W" in little endian */
    PI86_WORKLOAD_FORMAT_VERSION = 1u,
    PI86_V30_ADDRESS_SPACE_SIZE = 0x100000u,
};

typedef enum {
    PI86_WORKLOAD_FLAG_PERSISTENT = 1u << 0,
    PI86_WORKLOAD_FLAG_STDIO = 1u << 1,
    PI86_WORKLOAD_FLAG_SHARED_MEMORY = 1u << 2,
} pi86_workload_flags_t;

typedef enum {
    PI86_WORKLOAD_STATE_EMPTY = 0,
    PI86_WORKLOAD_STATE_RECEIVING = 1,
    PI86_WORKLOAD_STATE_READY = 2,
    PI86_WORKLOAD_STATE_RUNNING = 3,
    PI86_WORKLOAD_STATE_STOPPED = 4,
    PI86_WORKLOAD_STATE_EXITED = 5,
    PI86_WORKLOAD_STATE_FAULT = 6,
    PI86_WORKLOAD_STATE_TIMEOUT = 7,
} pi86_workload_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t load_address;
    uint16_t entry_segment;
    uint16_t entry_offset;
    uint16_t stack_segment;
    uint16_t stack_offset;
    uint32_t shared_base;
    uint32_t shared_size;
    uint32_t flags;
} pi86_workload_manifest_t;

typedef struct __attribute__((packed)) {
    uint32_t transfer_id;
    pi86_workload_manifest_t manifest;
} pi86_workload_begin_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t transfer_id;
    uint32_t offset;
    uint8_t data[];
} pi86_workload_data_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t transfer_id;
    uint32_t image_crc32;
} pi86_workload_commit_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t operation;
    uint8_t reserved[3];
    uint32_t workload_id;
} pi86_workload_control_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t workload_id;
    uint32_t state;
    uint32_t detail;
} pi86_workload_status_payload_t;

_Static_assert(sizeof(pi86_workload_manifest_t) == 40u,
               "workload manifest ABI changed");
_Static_assert(sizeof(pi86_workload_begin_payload_t) == 44u,
               "workload begin payload exceeds fixed Host record");
_Static_assert(sizeof(pi86_workload_commit_payload_t) == 8u,
               "workload commit ABI changed");
_Static_assert(sizeof(pi86_workload_control_payload_t) == 8u,
               "workload control ABI changed");
_Static_assert(sizeof(pi86_workload_status_payload_t) == 12u,
               "workload status ABI changed");

#endif
