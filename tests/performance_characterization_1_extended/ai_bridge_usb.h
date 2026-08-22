#ifndef PI86_AI_BRIDGE_USB_H
#define PI86_AI_BRIDGE_USB_H

#include <stdbool.h>
#include <stdint.h>

#include "ai_bridge/bridge_protocol.h"

#define PI86_AI_BRIDGE_USB_VID 0xCAFEu
#define PI86_AI_BRIDGE_USB_PID 0x4011u
#define PI86_AI_BRIDGE_USB_PRODUCT "pi86-rp2350 AI Bridge CDC+HID"

void pi86_ai_bridge_usb_init(void);
void pi86_ai_bridge_usb_task(void);
bool pi86_ai_bridge_hid_take_record(
    uint8_t record[PI86_BRIDGE_MESSAGE_SIZE]);
bool pi86_ai_bridge_hid_send_record(
    const uint8_t record[PI86_BRIDGE_MESSAGE_SIZE], uint32_t timeout_us);

#endif
