#ifndef PI86_AI_BRIDGE_PROTOCOL_H
#define PI86_AI_BRIDGE_PROTOCOL_H

#include <stdint.h>

/* Provider-neutral Core1/Core0 and USB message ABI.
 *
 * Every record is exactly 64 bytes and all multibyte fields are little-endian
 * on the wire.  The fixed size makes SPSC ownership transfer bounded and lets
 * a future HID report carry one complete bridge record without fragmentation.
 */
#define PI86_BRIDGE_PROTOCOL_VERSION 1u
#define PI86_BRIDGE_MESSAGE_SIZE     64u
#define PI86_BRIDGE_PAYLOAD_SIZE     52u

typedef enum {
    PI86_BRIDGE_MESSAGE_HELLO = 1,
    PI86_BRIDGE_MESSAGE_TEXT = 2,
    PI86_BRIDGE_MESSAGE_ACK = 3,
    PI86_BRIDGE_MESSAGE_COMMAND = 4,
    PI86_BRIDGE_MESSAGE_RESULT = 5,
    PI86_BRIDGE_MESSAGE_HEARTBEAT = 6,
    PI86_BRIDGE_MESSAGE_WORKLOAD_BEGIN = 0x20,
    PI86_BRIDGE_MESSAGE_WORKLOAD_DATA = 0x21,
    PI86_BRIDGE_MESSAGE_WORKLOAD_COMMIT = 0x22,
    PI86_BRIDGE_MESSAGE_WORKLOAD_CONTROL = 0x23,
    PI86_BRIDGE_MESSAGE_WORKLOAD_STATUS = 0x24,
    PI86_BRIDGE_MESSAGE_WORKLOAD_RESULT = 0x25,
    PI86_BRIDGE_MESSAGE_RUNTIME_CONTROL = 0x30,
    PI86_BRIDGE_MESSAGE_RUNTIME_STATUS = 0x31,
    PI86_BRIDGE_MESSAGE_ERROR = 0x7F,
} pi86_bridge_message_type_t;

typedef enum {
    PI86_BRIDGE_STATUS_OK = 0,
    PI86_BRIDGE_STATUS_BAD_VERSION = 1,
    PI86_BRIDGE_STATUS_BAD_LENGTH = 2,
    PI86_BRIDGE_STATUS_BUSY = 3,
    PI86_BRIDGE_STATUS_TIMEOUT = 4,
    PI86_BRIDGE_STATUS_BAD_SEQUENCE = 5,
    PI86_BRIDGE_STATUS_SERVICE_UNAVAILABLE = 6,
    PI86_BRIDGE_STATUS_BAD_CRC = 7,
    PI86_BRIDGE_STATUS_BAD_STATE = 8,
    PI86_BRIDGE_STATUS_BAD_WORKLOAD = 9,
} pi86_bridge_status_t;

typedef enum {
    PI86_WORKLOAD_CONTROL_RUN = 1,
    PI86_WORKLOAD_CONTROL_STOP = 2,
    PI86_WORKLOAD_CONTROL_RESTART = 3,
    PI86_WORKLOAD_CONTROL_STATUS = 4,
} pi86_workload_control_operation_t;

typedef enum {
    PI86_RUNTIME_CONTROL_ENTER_BOOTLOADER = 1,
    PI86_RUNTIME_CONTROL_SELFTEST = 2,
} pi86_runtime_control_operation_t;

enum {
    /* A retry reuses the original live sequence. It never authorizes a
     * duplicate native execution; the receiver either reports in-flight
     * state or replays a complete cached result for that sequence. */
    PI86_BRIDGE_FLAG_RETRY = 1u << 0,
};

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t sequence;
    uint16_t length;
    uint16_t status;
    uint8_t payload[PI86_BRIDGE_PAYLOAD_SIZE];
} pi86_bridge_message_t;

_Static_assert(sizeof(pi86_bridge_message_t) == PI86_BRIDGE_MESSAGE_SIZE,
               "AI Bridge ABI must remain one 64-byte record");

#endif
