#ifndef RP86_HOST_PROTOCOL_H
#define RP86_HOST_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Provider-neutral Core1/Core0 and USB message ABI.
 *
 * Every record is exactly 64 bytes and all multibyte fields are little-endian
 * on the wire.  The fixed size makes SPSC ownership transfer bounded and lets
 * a HID report carry one complete Host Protocol record without fragmentation.
 */
#define RP86_HOST_PROTOCOL_VERSION 1u
#define RP86_HOST_PROTOCOL_MESSAGE_SIZE     64u
#define RP86_HOST_PROTOCOL_PAYLOAD_SIZE     52u

typedef enum {
    RP86_HOST_PROTOCOL_MESSAGE_HELLO = 1,
    RP86_HOST_PROTOCOL_MESSAGE_TEXT = 2,
    RP86_HOST_PROTOCOL_MESSAGE_ACK = 3,
    RP86_HOST_PROTOCOL_MESSAGE_COMMAND = 4,
    RP86_HOST_PROTOCOL_MESSAGE_RESULT = 5,
    RP86_HOST_PROTOCOL_MESSAGE_HEARTBEAT = 6,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_BEGIN = 0x20,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_DATA = 0x21,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_COMMIT = 0x22,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_CONTROL = 0x23,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_STATUS = 0x24,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_RESULT = 0x25,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_TIMEOUT_REQUEST = 0x26,
    RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_TIMEOUT_RESULT = 0x27,
    RP86_HOST_PROTOCOL_MESSAGE_RUNTIME_CONTROL = 0x30,
    RP86_HOST_PROTOCOL_MESSAGE_RUNTIME_STATUS = 0x31,
    RP86_HOST_PROTOCOL_MESSAGE_FILESYSTEM_REQUEST = 0x40,
    RP86_HOST_PROTOCOL_MESSAGE_FILESYSTEM_RESULT = 0x41,
    RP86_HOST_PROTOCOL_MESSAGE_MEMORY_REQUEST = 0x50,
    RP86_HOST_PROTOCOL_MESSAGE_MEMORY_RESULT = 0x51,
    RP86_HOST_PROTOCOL_MESSAGE_DIAGNOSTICS_REQUEST = 0x60,
    RP86_HOST_PROTOCOL_MESSAGE_DIAGNOSTICS_RESULT = 0x61,
    RP86_HOST_PROTOCOL_MESSAGE_ERROR = 0x7F,
} rp86_host_protocol_message_type_t;

typedef enum {
    RP86_HOST_PROTOCOL_STATUS_OK = 0,
    RP86_HOST_PROTOCOL_STATUS_BAD_VERSION = 1,
    RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH = 2,
    RP86_HOST_PROTOCOL_STATUS_BUSY = 3,
    RP86_HOST_PROTOCOL_STATUS_TIMEOUT = 4,
    RP86_HOST_PROTOCOL_STATUS_BAD_SEQUENCE = 5,
    RP86_HOST_PROTOCOL_STATUS_SERVICE_UNAVAILABLE = 6,
    RP86_HOST_PROTOCOL_STATUS_BAD_CRC = 7,
    RP86_HOST_PROTOCOL_STATUS_BAD_STATE = 8,
    RP86_HOST_PROTOCOL_STATUS_BAD_WORKLOAD = 9,
    RP86_HOST_PROTOCOL_STATUS_IO_ERROR = 10,
    RP86_HOST_PROTOCOL_STATUS_NOT_FOUND = 11,
    RP86_HOST_PROTOCOL_STATUS_INVALID_PATH = 12,
    RP86_HOST_PROTOCOL_STATUS_NO_SPACE = 13,
} rp86_host_protocol_status_t;

typedef enum {
    RP86_WORKLOAD_CONTROL_RUN = 1,
    RP86_WORKLOAD_CONTROL_STOP = 2,
    RP86_WORKLOAD_CONTROL_RESTART = 3,
    RP86_WORKLOAD_CONTROL_STATUS = 4,
} rp86_workload_control_operation_t;

typedef enum {
    RP86_RUNTIME_CONTROL_ENTER_BOOTLOADER = 1,
    RP86_RUNTIME_CONTROL_SELFTEST = 2,
    RP86_RUNTIME_CONTROL_REBOOT = 3,
} rp86_runtime_control_operation_t;

/* Host filesystem service. Each request/reply still occupies one complete
 * 64-byte Host Protocol record. Larger files are transferred as sequence-bound
 * 40-byte chunks; the RP2350 remains the sole FatFs and NOR owner. */
typedef enum {
    RP86_FILESYSTEM_LIST = 1,
    RP86_FILESYSTEM_DF = 2,
    RP86_FILESYSTEM_READ = 3,
    RP86_FILESYSTEM_WRITE_BEGIN = 4,
    RP86_FILESYSTEM_WRITE_DATA = 5,
    RP86_FILESYSTEM_WRITE_COMMIT = 6,
} rp86_filesystem_operation_t;

typedef enum {
    RP86_MEMORY_READ = 1,
    RP86_MEMORY_WRITE = 2,
} rp86_memory_operation_t;

enum { RP86_MEMORY_DATA_BYTES = 40u };

enum {
    RP86_WORKLOAD_TIMEOUT_GET = 0u,
    RP86_WORKLOAD_TIMEOUT_SET = 1u,
    RP86_WORKLOAD_TIMEOUT_MAX_MS = 86400000u,
};

/* Request: two little-endian uint32 values (operation, timeout_ms).
 * SET zero disables; GET requires zero value. All records remain 64 bytes. */
typedef struct {
    uint32_t timeout_ms;
    uint32_t remaining_ms;
    uint32_t workload_id;
    uint32_t boot_id;
    uint32_t armed;
    uint32_t reserved[8];
} rp86_workload_timeout_payload_t;

_Static_assert(sizeof(rp86_workload_timeout_payload_t) == 52u,
               "timeout result must fit one Host record");

/* Read-only stopped-executor snapshot. Request payload: exact workload_id
 * (uint32_t, zero selects current). No clock or bus operation is performed. */
enum {
    RP86_DIAGNOSTICS_CYCLE_VALID = 1u << 0,
    RP86_DIAGNOSTICS_DATA_VALID = 1u << 1,
    RP86_DIAGNOSTICS_NO_CYCLE = 1u << 2,
    RP86_DIAGNOSTICS_UNMAPPED = 1u << 3,
    RP86_DIAGNOSTICS_INVALID_LANE = 1u << 4,
    RP86_DIAGNOSTICS_PAD_MISMATCH = 1u << 5,
    RP86_DIAGNOSTICS_CLOCK_FAILURE = 1u << 6,
    RP86_DIAGNOSTICS_INTERRUPT_ACK = 1u << 7,
};

typedef struct {
    uint32_t workload_id;
    uint32_t boot_id;
    uint32_t lifecycle;
    uint32_t completion_reason;
    uint32_t cycles;
    uint32_t last_address;
    uint32_t last_data;
    uint32_t cycle_type; /* processor_bus_cycle_type_t; valid only with CYCLE_VALID */
    uint32_t lanes;
    uint32_t flags;
    uint32_t reserved[3];
} rp86_diagnostics_payload_t;

_Static_assert(sizeof(rp86_diagnostics_payload_t) == 52u,
               "diagnostics must fit one 64-byte Host record");

enum {
    RP86_FILESYSTEM_FLAG_EOF = 1u << 0,
    RP86_FILESYSTEM_FLAG_DIRECTORY = 1u << 1,
    RP86_FILESYSTEM_FLAG_TRUNCATED = 1u << 2,
    RP86_FILESYSTEM_READ_DATA_BYTES = 40u,
    RP86_FILESYSTEM_WRITE_DATA_BYTES = 40u,
    RP86_FILESYSTEM_LIST_NAME_BYTES = 42u,
    RP86_FILESYSTEM_READ_PATH_BYTES = 44u,
    RP86_FILESYSTEM_WRITE_PATH_BYTES = 36u,
};

enum {
    /* A retry reuses the original live sequence. It never authorizes a
     * duplicate native execution; the receiver either reports in-flight
     * state or replays a complete cached result for that sequence. */
    RP86_HOST_PROTOCOL_FLAG_RETRY = 1u << 0,
};

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t sequence;
    uint16_t length;
    uint16_t status;
    uint8_t payload[RP86_HOST_PROTOCOL_PAYLOAD_SIZE];
} rp86_host_protocol_message_t;

static inline bool rp86_host_protocol_payload_length_valid(
    const rp86_host_protocol_message_t *record) {
    return record != NULL &&
           record->length <= RP86_HOST_PROTOCOL_PAYLOAD_SIZE;
}

/* Native service completion witness carried at the start of a HEARTBEAT or
 * RESULT payload. cpu_sequence is produced by the physical 8086-class
 * processor and observed from committed I/O writes. command_sequence is a
 * reserved native-counter field and is currently zero. boot_id is assigned by
 * the RP2350 when it releases processor RESET. */
#define RP86_NATIVE_WITNESS_MAGIC_0 'P'
#define RP86_NATIVE_WITNESS_MAGIC_1 '8'
#define RP86_NATIVE_WITNESS_MAGIC_2 '6'
#define RP86_NATIVE_WITNESS_MAGIC_3 'N'
#define RP86_NATIVE_WITNESS_VERSION 1u

enum {
    RP86_NATIVE_WITNESS_PROCESSOR_MASK = 3u << 0,
    RP86_NATIVE_WITNESS_PROCESSOR_INTEL_8086 = 1u << 0,
    RP86_NATIVE_WITNESS_PROCESSOR_NEC_V30 = 2u << 0,
};

typedef struct {
    uint8_t magic[4];
    uint8_t version;
    uint8_t service_type;
    uint16_t flags;
    uint32_t boot_id;
    uint32_t cpu_sequence;
    uint32_t command_sequence;
} rp86_native_service_witness_t;

_Static_assert(sizeof(rp86_host_protocol_message_t) == RP86_HOST_PROTOCOL_MESSAGE_SIZE,
               "Host Protocol ABI must remain one 64-byte record");
_Static_assert(sizeof(rp86_native_service_witness_t) == 20u,
               "native service witness ABI changed");

#endif
