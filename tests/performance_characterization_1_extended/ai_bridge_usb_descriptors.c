#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#include "ai_bridge_usb.h"

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC,
    STRID_HID,
};

#define EPNUM_CDC_NOTIF 0x81u
#define EPNUM_CDC_OUT   0x02u
#define EPNUM_CDC_IN    0x82u
#define EPNUM_HID_OUT   0x03u
#define EPNUM_HID_IN    0x83u

#define CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = PI86_AI_BRIDGE_USB_VID,
    .idProduct = PI86_AI_BRIDGE_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(PI86_BRIDGE_MESSAGE_SIZE)
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_NONE,
                             sizeof hid_report_descriptor, EPNUM_HID_OUT,
                             EPNUM_HID_IN, PI86_BRIDGE_MESSAGE_SIZE, 1),
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
    (void)language_id;
    static uint16_t descriptor[40];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2u + 1u];
    const char *text = NULL;

    if (index == STRID_LANGID) {
        descriptor[1] = 0x0409u;
        descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4u);
        return descriptor;
    }
    if (index == STRID_SERIAL) {
        if (serial[0] == '\0')
            pico_get_unique_board_id_string(serial, sizeof serial);
        text = serial;
    } else if (index == STRID_MANUFACTURER) {
        text = "pi86-rp2350";
    } else if (index == STRID_PRODUCT) {
        text = PI86_AI_BRIDGE_USB_PRODUCT;
    } else if (index == STRID_CDC) {
        text = "Physical validation CDC";
    } else if (index == STRID_HID) {
        text = "64-byte V30 AI Bridge";
    } else {
        return NULL;
    }

    size_t count = strlen(text);
    if (count > 39u) count = 39u;
    for (size_t i = 0u; i < count; ++i)
        descriptor[i + 1u] = (uint8_t)text[i];
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) |
                               (2u * count + 2u));
    return descriptor;
}
