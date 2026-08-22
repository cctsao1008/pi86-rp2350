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
    PI86_BRIDGE_MESSAGE_ERROR = 0x7F,
} pi86_bridge_message_type_t;

typedef enum {
    PI86_BRIDGE_STATUS_OK = 0,
    PI86_BRIDGE_STATUS_BAD_VERSION = 1,
    PI86_BRIDGE_STATUS_BAD_LENGTH = 2,
    PI86_BRIDGE_STATUS_BUSY = 3,
} pi86_bridge_status_t;

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
