#ifndef RP86_HOST_PROTOCOL_USB_H
#define RP86_HOST_PROTOCOL_USB_H

#include <stdbool.h>
#include <stdint.h>

#include "host_protocol/host_protocol.h"

#define RP86_HOST_PROTOCOL_USB_VID 0xCAFEu
#define RP86_HOST_PROTOCOL_USB_PID 0x4011u
#define RP86_HOST_PROTOCOL_USB_PRODUCT "RP86 Host Protocol CDC+HID"

void rp86_host_protocol_usb_init(void);
void rp86_host_protocol_usb_task(void);
bool rp86_host_protocol_cdc_write(const char *data, uint32_t length,
                              uint32_t timeout_us);
bool rp86_host_protocol_hid_take_record(
    uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE]);
bool rp86_host_protocol_hid_send_record(
    const uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE], uint32_t timeout_us);

#endif
